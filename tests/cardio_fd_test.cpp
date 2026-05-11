// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>

#if CARDIO_HAS_POSIX_FD
#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_POSIX_FD
static bool has_event(cardio::fd_event events, cardio::fd_event expected) {
  return (events & expected) != cardio::fd_event::none;
}

static void make_pipe(int (&fds)[2]) {
  CHECK_EQ(::pipe(fds), 0);
}

static void make_socket_pair(int (&fds)[2]) {
  CHECK_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
}

static void close_fd(int& fd) {
  if (fd >= 0) {
    CHECK_EQ(::close(fd), 0);
    fd = -1;
  }
}

static void write_byte(int fd) {
  const auto value = char{'x'};
  CHECK_EQ(::write(fd, &value, 1), static_cast<ssize_t>(1));
}

static cardio::promise<void> await_canceled_fd_async(
    cardio::promise<cardio::fd_event>& child,
    bool& canceled) {
  try {
    (void)co_await child;
  } catch (const cardio::canceled_exception&) {
    canceled = true;
  }
}

static bool wait_until_true(const std::atomic<bool>& flag) {
  for (auto retry = 0; retry < 5000; ++retry) {
    if (flag.load(std::memory_order_acquire)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return flag.load(std::memory_order_acquire);
}

template <typename T>
static bool wait_until_ready(const cardio::promise<T>& promise) {
  for (auto retry = 0; retry < 5000; ++retry) {
    if (promise.is_ready()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return promise.is_ready();
}

//-----------------------------------------------------------------------------------------------

static void fd_event_bitmask_operators_work() {
  auto events = cardio::fd_event::none;
  events |= cardio::fd_event::read;
  events |= cardio::fd_event::write;

  CHECK(has_event(events, cardio::fd_event::read));
  CHECK(has_event(events, cardio::fd_event::write));

  events &= cardio::fd_event::read;

  CHECK(has_event(events, cardio::fd_event::read));
  CHECK(!has_event(events, cardio::fd_event::write));
}

//-----------------------------------------------------------------------------------------------

static void read_readiness_resolves_after_write() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto p =
      cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!p.is_ready());

  write_byte(fds[1]);
  dispatcher.park();

  CHECK(p.is_ready());
  CHECK(has_event(p.unsafe_result(), cardio::fd_event::read));

  close_fd(fds[0]);
  close_fd(fds[1]);
}

//-----------------------------------------------------------------------------------------------

static void write_readiness_resolves_for_writable_pipe() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto p = cardio::from_fd(
      fds[1], cardio::fd_event::write);
  CHECK(!p.is_ready());

  dispatcher.park();

  CHECK(p.is_ready());
  CHECK(has_event(p.unsafe_result(), cardio::fd_event::write));

  close_fd(fds[0]);
  close_fd(fds[1]);
}

//-----------------------------------------------------------------------------------------------

static void combined_interests_return_combined_events() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_socket_pair(fds);

  write_byte(fds[1]);

  auto p = cardio::from_fd(
      fds[0], cardio::fd_event::read | cardio::fd_event::write);
  dispatcher.park();

  const auto events = p.unsafe_result();
  CHECK(has_event(events, cardio::fd_event::read));
  CHECK(has_event(events, cardio::fd_event::write));

  close_fd(fds[0]);
  close_fd(fds[1]);
}

//-----------------------------------------------------------------------------------------------

static void hangup_readiness_reports_hangup() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto p =
      cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!p.is_ready());

  close_fd(fds[1]);
  dispatcher.park();

  CHECK(p.is_ready());
  CHECK(has_event(p.unsafe_result(), cardio::fd_event::hangup));

  close_fd(fds[0]);
}

//-----------------------------------------------------------------------------------------------

static void closed_fd_readiness_reports_error() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);

  const auto closed_fd = fds[0];
  close_fd(fds[0]);
  close_fd(fds[1]);

  auto p = cardio::from_fd(
      closed_fd, cardio::fd_event::read);
  dispatcher.park();

  CHECK(p.is_ready());
  CHECK(has_event(p.unsafe_result(), cardio::fd_event::error));
}

//-----------------------------------------------------------------------------------------------

static void invalid_arguments_create_rejected_promises() {
  test_dispatcher_host dispatcher;

  auto negative = cardio::from_fd(
      -1, cardio::fd_event::read);
  CHECK(negative.is_ready());

  try {
    (void)negative.unsafe_result();
    CHECK(false);
  } catch (const std::invalid_argument&) {
  }

  auto no_interests = cardio::from_fd(
      0, cardio::fd_event::none);
  CHECK(no_interests.is_ready());

  try {
    (void)no_interests.unsafe_result();
    CHECK(false);
  } catch (const std::invalid_argument&) {
  }
}

//-----------------------------------------------------------------------------------------------

static void destroyed_pending_fd_promise_unregisters_wait() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);

  {
    auto p = cardio::from_fd(
        fds[0], cardio::fd_event::read);
    CHECK(!p.is_ready());
  }

  dispatcher.park();

  close_fd(fds[0]);
  close_fd(fds[1]);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> await_fd_async(
    int fd,
    bool& continued,
    cardio::fd_event& result) {
  auto events =
      co_await cardio::from_fd(
          fd, cardio::fd_event::read);
  continued = true;
  result = events;
}

static void fd_completion_resumes_awaiting_coroutine() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto continued = false;
  auto result = cardio::fd_event::none;

  auto p = await_fd_async(fds[0], continued, result);
  (void)p;
  CHECK(!continued);

  write_byte(fds[1]);
  dispatcher.park();

  CHECK(continued);
  CHECK(has_event(result, cardio::fd_event::read));

  close_fd(fds[0]);
  close_fd(fds[1]);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> set_flag_after_dispatch_async(
    std::atomic<bool>& flag) {
  co_await cardio::resolved();
  flag.store(true, std::memory_order_release);
}

static void queued_continuation_wakes_fd_poll_worker() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto ran = std::atomic<bool>{false};

  auto pending =
      cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!pending.is_ready());

  auto worker = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  auto queued = set_flag_after_dispatch_async(ran);
  (void)queued;
  const auto ran_before_fallback = wait_until_true(ran);

  write_byte(fds[1]);
  worker.join();

  CHECK(ran_before_fallback);
  CHECK(pending.is_ready());

  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void posted_callback_wakes_fd_poll_worker() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto ran = std::atomic<bool>{false};

  auto pending =
      cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!pending.is_ready());

  auto worker = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    ran.store(true, std::memory_order_release);
  });
  const auto ran_before_fallback = wait_until_true(ran);

  write_byte(fds[1]);
  worker.join();

  CHECK(ran_before_fallback);
  CHECK(pending.is_ready());

  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void newly_registered_fd_wait_wakes_existing_poll_snapshot() {
  test_dispatcher_host dispatcher;
  int first_fds[2]{-1, -1};
  int second_fds[2]{-1, -1};
  make_pipe(first_fds);
  make_pipe(second_fds);

  auto first = cardio::from_fd(
      first_fds[0], cardio::fd_event::read);
  CHECK(!first.is_ready());

  auto worker = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  auto second = cardio::from_fd(
      second_fds[0], cardio::fd_event::read);
  write_byte(second_fds[1]);
  const auto second_ready_before_fallback = wait_until_ready(second);

  write_byte(first_fds[1]);
  worker.join();

  CHECK(second_ready_before_fallback);
  CHECK(first.is_ready());
  CHECK(second.is_ready());

  close_fd(first_fds[0]);
  close_fd(first_fds[1]);
  close_fd(second_fds[0]);
  close_fd(second_fds[1]);
}

static void unregistering_fd_wait_wakes_poll_worker_to_exit() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto returned = std::atomic<bool>{false};
  auto pending =
      std::optional<cardio::promise<cardio::fd_event>>{};

  pending.emplace(
      cardio::from_fd(fds[0], cardio::fd_event::read));
  CHECK(!pending->is_ready());

  auto worker = std::thread([&] {
    park_current_dispatcher(dispatcher);
    returned.store(true, std::memory_order_release);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  pending.reset();
  const auto returned_before_fallback = wait_until_true(returned);
  if (!returned_before_fallback) {
    write_byte(fds[1]);
  }

  worker.join();

  CHECK(returned_before_fallback);

  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void queued_continuation_wakes_one_of_two_fd_workers() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto ran = std::atomic<bool>{false};

  auto pending =
      cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!pending.is_ready());

  auto first = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  auto second = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  auto queued = set_flag_after_dispatch_async(ran);
  (void)queued;
  const auto ran_before_fallback = wait_until_true(ran);

  write_byte(fds[1]);
  first.join();
  second.join();

  CHECK(ran_before_fallback);
  CHECK(pending.is_ready());

  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void posted_callback_wakes_fd_poll_worker_while_work_runs() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto active = std::atomic<bool>{false};
  auto release = std::atomic<bool>{false};
  auto posted = std::atomic<bool>{false};

  auto pending =
      cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!pending.is_ready());

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    active.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  auto first = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  auto second = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  CHECK(wait_until_true(active));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    posted.store(true, std::memory_order_release);
  });
  const auto posted_before_fallback = wait_until_true(posted);

  release.store(true, std::memory_order_release);
  write_byte(fds[1]);
  first.join();
  second.join();

  CHECK(posted_before_fallback);
  CHECK(pending.is_ready());

  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void gentle_shutdown_returns_without_pending_fd_wait() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto pending =
      cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!pending.is_ready());

  group.shutdown();
  dispatcher.park();

  CHECK(!pending.is_ready());

  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void gentle_shutdown_collects_ready_fd_wait() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto pending =
      cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!pending.is_ready());

  write_byte(fds[1]);
  group.shutdown();
  dispatcher.park();

  CHECK(pending.is_ready());
  CHECK(has_event(pending.unsafe_result(), cardio::fd_event::read));

  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void fd_wait_can_be_canceled_by_timeout() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto source = cardio::cancellations::timeout(20);
  auto pending = cardio::from_fd(
      fds[0], cardio::fd_event::read, source.get_cancellation());
  auto canceled = false;
  auto parent = await_canceled_fd_async(pending, canceled);
  (void)parent;
  CHECK(!pending.is_ready());

  dispatcher.park();

  CHECK(source.get_cancellation().is_cancellation_requested());
  CHECK(pending.is_ready());
  CHECK(canceled);

  close_fd(fds[0]);
  close_fd(fds[1]);
}

//-----------------------------------------------------------------------------------------------

int main() {
  fd_event_bitmask_operators_work();
  read_readiness_resolves_after_write();
  write_readiness_resolves_for_writable_pipe();
  combined_interests_return_combined_events();
  hangup_readiness_reports_hangup();
  closed_fd_readiness_reports_error();
  invalid_arguments_create_rejected_promises();
  destroyed_pending_fd_promise_unregisters_wait();
  fd_completion_resumes_awaiting_coroutine();
  queued_continuation_wakes_fd_poll_worker();
  posted_callback_wakes_fd_poll_worker();
  newly_registered_fd_wait_wakes_existing_poll_snapshot();
  unregistering_fd_wait_wakes_poll_worker_to_exit();
  queued_continuation_wakes_one_of_two_fd_workers();
  posted_callback_wakes_fd_poll_worker_while_work_runs();
  gentle_shutdown_returns_without_pending_fd_wait();
  gentle_shutdown_collects_ready_fd_wait();
  fd_wait_can_be_canceled_by_timeout();

  std::puts("cardio_fd_test: PASS");
  return 0;
}
#else
int main() {
  std::puts("cardio_fd_test: SKIP");
  return 0;
}
#endif
