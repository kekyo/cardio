// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#if CARDIO_HAS_POSIX_FD
#include <unistd.h>
#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_POSIX_FD
static bool has_event(cardio::fd_event events, cardio::fd_event expected) {
  return (events & expected) != cardio::fd_event::none;
}

static void close_fd(int& fd) {
  if (fd >= 0) {
    CHECK_EQ(::close(fd), 0);
    fd = -1;
  }
}

static void make_pipe(int (&fds)[2]) {
  CHECK_EQ(::pipe(fds), 0);
}

static void write_byte(int fd) {
  const auto value = char{'x'};
  CHECK_EQ(::write(fd, &value, 1), static_cast<ssize_t>(1));
}
#endif

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> await_canceled_value_async(
    cardio::promise<int>& child,
    int& result) {
  try {
    (void)co_await child;
  } catch (const cardio::canceled_exception& exception) {
    if (exception.what() == std::string("cardio: operation canceled")) {
      result = 1;
      co_return;
    }
  }

  result = 0;
  co_return;
}

#if CARDIO_HAS_POSIX_FD
static cardio::promise<void> await_canceled_fd_async(
    cardio::promise<cardio::fd_event>& child,
    int& result) {
  try {
    (void)co_await child;
  } catch (const cardio::canceled_exception&) {
    result = 1;
    co_return;
  }

  result = 0;
  co_return;
}
#endif

//-----------------------------------------------------------------------------------------------

static void cancellation_source_notifies_once() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;
  auto cancellation = source.get_cancellation();
  auto callbacks = 0;

  CHECK(!cancellation.is_cancellation_requested());

  auto first = cancellation.on_cancellation_requested([&] {
    callbacks += 1;
  });
  auto second = cancellation.on_cancellation_requested([&] {
    callbacks += 10;
  });

  CHECK(source.cancel());
  CHECK(!source.cancel());
  CHECK(cancellation.is_cancellation_requested());
  CHECK_EQ(callbacks, 0);

  dispatcher.park();

  CHECK_EQ(callbacks, 11);

  auto third = cancellation.on_cancellation_requested([&] {
    callbacks += 100;
  });
  (void)third;

  CHECK_EQ(callbacks, 11);

  dispatcher.park();

  CHECK_EQ(callbacks, 111);
}

static void cancellation_registration_unregisters_callback() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;
  auto cancellation = source.get_cancellation();
  auto callbacks = 0;

  {
    auto registration = cancellation.on_cancellation_requested([&] {
      callbacks += 1;
    });
    registration.reset();
  }

  CHECK(source.cancel());
  dispatcher.park();

  CHECK_EQ(callbacks, 0);
}

static void cancellation_registration_requires_dispatcher() {
  cardio::set_current_dispatcher(nullptr);
  cardio::cancellation_source source;
  auto cancellation = source.get_cancellation();

  auto caught = false;
  try {
    auto registration = cancellation.on_cancellation_requested([] {});
    (void)registration;
  } catch (const std::runtime_error& exception) {
    caught = std::string(exception.what()).find("no active dispatcher") !=
             std::string::npos;
  }

  CHECK(caught);
}

static void cancellation_callback_runs_on_registered_dispatcher() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;
  auto cancellation = source.get_cancellation();
  const auto dispatcher_thread_id = std::this_thread::get_id();
  auto callback_thread_id = std::thread::id{};

  auto registration = cancellation.on_cancellation_requested([&] {
    callback_thread_id = std::this_thread::get_id();
  });

  auto worker = std::thread([&] {
    CHECK(source.cancel());
  });
  worker.join();

  CHECK(cancellation.is_cancellation_requested());
  CHECK(callback_thread_id == std::thread::id{});

  dispatcher.park();

  CHECK(callback_thread_id == dispatcher_thread_id);

  registration.reset();
}

static void cancellation_throw_if_requested_throws_canceled_exception() {
  cardio::cancellation_source source;
  auto cancellation = source.get_cancellation();

  cancellation.throw_if_cancellation_requested();
  CHECK(source.cancel());

  auto caught = false;
  try {
    cancellation.throw_if_cancellation_requested();
  } catch (const cardio::canceled_exception& exception) {
    caught = exception.what() == std::string("cardio: operation canceled");
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void promise_source_try_resolve_wins_once() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();

  CHECK(source.try_resolve(41));
  CHECK(!source.try_resolve(42));
  CHECK(!source.try_reject(std::runtime_error("late failure")));
  CHECK(!source.try_cancel());

  dispatcher.park();

  CHECK(child.is_ready());
  CHECK_EQ(child.unsafe_result(), 41);
}

static void promise_source_try_cancel_rethrows_canceled_exception() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();
  auto parent_result = 0;
  auto parent = await_canceled_value_async(child, parent_result);
  (void)parent;

  CHECK(source.try_cancel());
  CHECK(!source.try_resolve(1));

  dispatcher.park();

  CHECK_EQ(parent_result, 1);

  auto try_caught = false;
  try {
    (void)child.try_result();
  } catch (const cardio::canceled_exception&) {
    try_caught = true;
  }

  auto unsafe_caught = false;
  try {
    (void)child.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    unsafe_caught = true;
  }

  CHECK(try_caught);
  CHECK(unsafe_caught);
}

static void promise_source_cancel_twice_fails() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();

  source.cancel();

  auto caught = false;
  try {
    source.cancel();
  } catch (const std::logic_error&) {
    caught = true;
  }

  CHECK(caught);
  CHECK(child.is_ready());
}

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_POSIX_FD
static void fd_wait_cancellation_resumes_with_canceled_exception() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto child =
      cardio::from_fd(fds[0], cardio::fd_event::read, source.get_cancellation());
  auto parent_result = 0;
  auto parent = await_canceled_fd_async(child, parent_result);
  (void)parent;

  CHECK(source.cancel());
  dispatcher.park();

  CHECK_EQ(parent_result, 1);

  auto caught = false;
  try {
    (void)child.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }

  CHECK(caught);

  close_fd(fds[0]);
  close_fd(fds[1]);
}
#endif

#if CARDIO_HAS_POSIX_FD
static void fd_wait_readiness_wins_over_late_cancellation() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto promise =
      cardio::from_fd(fds[0], cardio::fd_event::read, source.get_cancellation());
  write_byte(fds[1]);
  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK(has_event(promise.unsafe_result(), cardio::fd_event::read));
  CHECK(source.cancel());
  CHECK(has_event(promise.unsafe_result(), cardio::fd_event::read));

  close_fd(fds[0]);
  close_fd(fds[1]);
}
#endif

//-----------------------------------------------------------------------------------------------

int main() {
  cancellation_source_notifies_once();
  cancellation_registration_unregisters_callback();
  cancellation_registration_requires_dispatcher();
  cancellation_callback_runs_on_registered_dispatcher();
  cancellation_throw_if_requested_throws_canceled_exception();
  promise_source_try_resolve_wins_once();
  promise_source_try_cancel_rethrows_canceled_exception();
  promise_source_cancel_twice_fails();
#if CARDIO_HAS_POSIX_FD
  fd_wait_cancellation_resumes_with_canceled_exception();
  fd_wait_readiness_wins_over_late_cancellation();
#endif

  std::puts("cardio_cancellation_test: PASS");
  return 0;
}
