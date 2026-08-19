// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <android/looper.h>

#include <atomic>
#include <cstdio>
#include <latch>
#include <thread>
#include <type_traits>
#include <unistd.h>

static_assert(std::is_base_of_v<cardio::dispatcher, cardio::dispatcher_host_android>);
static_assert(std::is_default_constructible_v<cardio::dispatcher_host_android>);
static_assert(std::is_constructible_v<
    cardio::dispatcher_host_android,
    cardio::dispatcher_group&>);

//-----------------------------------------------------------------------------------------------

static bool has_event(cardio::fd_event events, cardio::fd_event expected) {
  return (events & expected) != cardio::fd_event::none;
}

static void make_pipe(int (&fds)[2]) {
  CHECK_EQ(::pipe(fds), 0);
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

static cardio::promise<void> wait_for_timer(
    bool& completed,
    std::thread::id& completion_thread) {
  co_await cardio::promises::delay(5);
  completed = true;
  completion_thread = std::this_thread::get_id();
}

static cardio::promise<void> wait_for_fd(
    int fd,
    bool& completed,
    std::thread::id& completion_thread,
    cardio::fd_event& result) {
  result = co_await cardio::from_fd(fd, cardio::fd_event::read);
  completed = true;
  completion_thread = std::this_thread::get_id();
}

static cardio::promise<void> observe_fd_cancellation(
    cardio::promise<cardio::fd_event>& target,
    bool& canceled) {
  try {
    (void)co_await target;
  } catch (const cardio::canceled_exception&) {
    canceled = true;
  }
}

static void posted_work_runs_and_reports_android_feature() {
  cardio::dispatcher_host_android dispatcher;
  auto ran = false;

  CHECK(
      (dispatcher.get_feature() & cardio::dispatcher_feature::android) !=
      cardio::dispatcher_feature::none);
  CHECK(
      (dispatcher.get_feature() & cardio::dispatcher_feature::posix) !=
      cardio::dispatcher_feature::none);

  cardio::internal::dangerous_schedule_later__(
      &dispatcher, [&] { ran = true; });
  dispatcher.park();

  CHECK(ran);
}

static void timer_resumes_on_owner_thread() {
  cardio::dispatcher_host_android dispatcher;
  const auto owner_thread = std::this_thread::get_id();
  auto completed = false;
  auto completion_thread = std::thread::id{};

  auto run = wait_for_timer(completed, completion_thread);
  (void)run;

  dispatcher.park();

  CHECK(completed);
  CHECK_EQ(completion_thread, owner_thread);
}

static void fd_readiness_resumes_on_owner_thread() {
  cardio::dispatcher_host_android dispatcher;
  const auto owner_thread = std::this_thread::get_id();
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto completed = false;
  auto completion_thread = std::thread::id{};
  auto result = cardio::fd_event::none;

  auto run = wait_for_fd(
      fds[0], completed, completion_thread, result);
  (void)run;

  write_byte(fds[1]);
  dispatcher.park();

  CHECK(completed);
  CHECK_EQ(completion_thread, owner_thread);
  CHECK(has_event(result, cardio::fd_event::read));
  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void duplicate_fd_waits_complete_together() {
  cardio::dispatcher_host_android dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto first = cardio::from_fd(fds[0], cardio::fd_event::read);
  auto second = cardio::from_fd(fds[0], cardio::fd_event::read);
  write_byte(fds[1]);

  dispatcher.park();

  CHECK(first.is_ready());
  CHECK(second.is_ready());
  CHECK(has_event(first.unsafe_result(), cardio::fd_event::read));
  CHECK(has_event(second.unsafe_result(), cardio::fd_event::read));
  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void canceled_fd_registration_can_reuse_same_fd() {
  cardio::dispatcher_host_android dispatcher;
  int first_fds[2]{-1, -1};
  make_pipe(first_fds);
  const auto reused_fd = first_fds[0];
  cardio::cancellation_source source;

  auto canceled = cardio::from_fd(
      first_fds[0],
      cardio::fd_event::read,
      source.get_cancellation());
  auto cancellation_observed = false;
  auto observation = observe_fd_cancellation(
      canceled, cancellation_observed);
  (void)observation;
  CHECK(source.cancel());
  dispatcher.park();
  CHECK(canceled.is_ready());
  CHECK(cancellation_observed);

  close_fd(first_fds[0]);
  close_fd(first_fds[1]);

  int second_fds[2]{-1, -1};
  make_pipe(second_fds);
  CHECK_EQ(second_fds[0], reused_fd);
  auto ready = cardio::from_fd(second_fds[0], cardio::fd_event::read);
  write_byte(second_fds[1]);
  dispatcher.park();

  CHECK(ready.is_ready());
  CHECK(has_event(ready.unsafe_result(), cardio::fd_event::read));
  close_fd(second_fds[0]);
  close_fd(second_fds[1]);
}

//-----------------------------------------------------------------------------------------------

struct external_callback_state {
  cardio::dispatcher_group* group;
  std::thread::id owner_thread;
  std::thread::id callback_thread;
  bool called;
};

static int external_looper_callback(int fd, int events, void* data) {
  auto* state = static_cast<external_callback_state*>(data);
  auto value = char{};
  CHECK((events & ALOOPER_EVENT_INPUT) != 0);
  CHECK_EQ(::read(fd, &value, 1), static_cast<ssize_t>(1));
  state->callback_thread = std::this_thread::get_id();
  state->called = true;
  state->group->shutdown();
  return 0;
}

static void external_looper_callback_coexists_with_park() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  cardio::dispatcher_host_android dispatcher(group);
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto state = external_callback_state{
      &group,
      std::this_thread::get_id(),
      {},
      false,
  };

  auto* looper = ALooper_forThread();
  CHECK(looper != nullptr);
  CHECK_EQ(
      ALooper_addFd(
          looper,
          fds[0],
          ALOOPER_POLL_CALLBACK,
          ALOOPER_EVENT_INPUT,
          &external_looper_callback,
          &state),
      1);
  write_byte(fds[1]);

  dispatcher.park();

  CHECK(state.called);
  CHECK_EQ(state.callback_thread, state.owner_thread);
  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void post_from_worker_wakes_native_looper() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  cardio::dispatcher_host_android dispatcher(group);
  const auto owner_thread = std::this_thread::get_id();
  auto ran = false;
  auto callback_thread = std::thread::id{};

  auto worker = std::thread([&] {
    cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
      ran = true;
      callback_thread = std::this_thread::get_id();
      group.shutdown();
    });
  });

  dispatcher.park();
  worker.join();

  CHECK(ran);
  CHECK_EQ(callback_thread, owner_thread);
}

static void gentle_shutdown_collects_already_ready_fd() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  cardio::dispatcher_host_android dispatcher(group);
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto ready = cardio::from_fd(fds[0], cardio::fd_event::read);

  write_byte(fds[1]);
  group.shutdown();
  dispatcher.park();

  CHECK(ready.is_ready());
  CHECK(has_event(ready.unsafe_result(), cardio::fd_event::read));
  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void immediate_shutdown_skips_remaining_work() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  cardio::dispatcher_host_android dispatcher(group);
  auto first_ran = false;
  auto second_ran = false;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    first_ran = true;
    group.shutdown();
  });
  cardio::internal::dangerous_schedule_later__(
      &dispatcher, [&] { second_ran = true; });

  dispatcher.park(cardio::shutdown_mode::unsafe_immediate);

  CHECK(first_ran);
  CHECK(!second_ran);
}

static void park_rejects_non_owner_thread() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  cardio::dispatcher_host_android dispatcher(group);
  auto ready = std::latch{1};
  auto start = std::latch{1};
  auto rejected = std::atomic<bool>{false};

  auto worker = std::thread([&] {
    ready.count_down();
    start.wait();
    try {
      dispatcher.park();
    } catch (const std::runtime_error&) {
      rejected.store(true, std::memory_order_release);
    }
  });

  ready.wait();
  start.count_down();
  group.shutdown();
  worker.join();

  CHECK(rejected.load(std::memory_order_acquire));
}

static void repeated_host_lifecycle_preserves_looper() {
  for (auto iteration = 0; iteration < 32; ++iteration) {
    cardio::dispatcher_host_android dispatcher;
    auto ran = false;
    cardio::internal::dangerous_schedule_later__(
        &dispatcher, [&] { ran = true; });
    dispatcher.park();
    CHECK(ran);
    CHECK(ALooper_forThread() != nullptr);
  }
}

//-----------------------------------------------------------------------------------------------

int main() {
  posted_work_runs_and_reports_android_feature();
  timer_resumes_on_owner_thread();
  fd_readiness_resumes_on_owner_thread();
  duplicate_fd_waits_complete_together();
  canceled_fd_registration_can_reuse_same_fd();
  external_looper_callback_coexists_with_park();
  post_from_worker_wakes_native_looper();
  gentle_shutdown_collects_already_ready_fd();
  immediate_shutdown_skips_remaining_work();
  park_rejects_non_owner_thread();
  repeated_host_lifecycle_preserves_looper();

  std::puts("cardio_android_manual_test: PASS");
  return 0;
}
