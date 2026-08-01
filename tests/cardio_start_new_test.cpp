// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

//-----------------------------------------------------------------------------------------------

static void wait_until_true(const std::atomic<bool>& flag) {
  for (auto retry = 0; retry < 5000; ++retry) {
    if (flag.load(std::memory_order_acquire)) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(flag.load(std::memory_order_acquire));
}

static cardio::promise<int> delayed_value_async(
    std::atomic<bool>& running,
    std::thread::id& resumed_thread_id) {
  running.store(true, std::memory_order_release);
  co_await cardio::promises::delay(10);
  resumed_thread_id = std::this_thread::get_id();
  co_return 41;
}

static cardio::promise<void> delayed_void_async(
    std::atomic<bool>& completed) {
  co_await cardio::promises::delay(10);
  completed.store(true, std::memory_order_release);
}

#if CARDIO_HAS_EXCEPTIONS
static cardio::promise<int> failing_async() {
  co_await cardio::promises::delay(1);
  throw std::runtime_error("start_new async failure");
  co_return 0;
}

static cardio::promise<int> cancelable_worker_async(
    cardio::cancellation cancellation,
    std::atomic<bool>& delay_registered) {
  auto delay = cardio::promises::delay(1000, std::move(cancellation));
  delay_registered.store(true, std::memory_order_release);
  co_await delay;
  co_return 0;
}
#endif

//-----------------------------------------------------------------------------------------------

static void synchronous_value_runs_on_worker_thread() {
  test_dispatcher_host dispatcher;
  const auto caller_thread_id = std::this_thread::get_id();
  auto worker_thread_id = std::thread::id{};

  auto promise = cardio::promises::start_new([&] {
    worker_thread_id = std::this_thread::get_id();
    return 42;
  });

  static_assert(std::is_same_v<decltype(promise), cardio::promise<int>>);
  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK_EQ(promise.unsafe_result(), 42);
  CHECK(worker_thread_id != std::thread::id{});
  CHECK(worker_thread_id != caller_thread_id);
}

static void synchronous_void_completes() {
  test_dispatcher_host dispatcher;
  auto completed = std::atomic<bool>{false};

  auto promise = cardio::promises::start_new([&] {
    completed.store(true, std::memory_order_release);
  });

  static_assert(std::is_same_v<decltype(promise), cardio::promise<void>>);
  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK(promise.try_result());
  CHECK(completed.load(std::memory_order_acquire));
}

static void asynchronous_value_runs_with_worker_dispatcher() {
  test_dispatcher_host dispatcher;
  const auto caller_thread_id = std::this_thread::get_id();
  auto running = std::atomic<bool>{false};
  auto resumed_thread_id = std::thread::id{};

  auto promise = cardio::promises::start_new([&] {
    return delayed_value_async(running, resumed_thread_id);
  });

  static_assert(std::is_same_v<decltype(promise), cardio::promise<int>>);
  wait_until_true(running);
  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK_EQ(promise.unsafe_result(), 41);
  CHECK(resumed_thread_id != std::thread::id{});
  CHECK(resumed_thread_id != caller_thread_id);
}

static void asynchronous_void_completes_with_worker_dispatcher() {
  test_dispatcher_host dispatcher;
  auto completed = std::atomic<bool>{false};

  auto promise = cardio::promises::start_new([&] {
    return delayed_void_async(completed);
  });

  static_assert(std::is_same_v<decltype(promise), cardio::promise<void>>);
  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK(promise.try_result());
  CHECK(completed.load(std::memory_order_acquire));
}

static void move_only_value_is_returned() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::promises::start_new([] {
    return std::make_unique<int>(77);
  });

  dispatcher.park();

  CHECK(promise.is_ready());
  auto value = std::move(promise).unsafe_result();
  CHECK(value != nullptr);
  CHECK_EQ(*value, 77);
}

#if CARDIO_HAS_EXCEPTIONS
static void synchronous_exception_is_rejected() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::promises::start_new([]() -> int {
    throw std::runtime_error("start_new sync failure");
  });

  dispatcher.park();

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("start_new sync failure");
  }

  CHECK(caught);
}

static void asynchronous_exception_is_rejected() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::promises::start_new([] {
    return failing_async();
  });

  dispatcher.park();

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("start_new async failure");
  }

  CHECK(caught);
}

static void parent_cancellation_cancels_worker_async_operation() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;
  auto delay_registered = std::atomic<bool>{false};

  auto promise = cardio::promises::start_new([&] {
    return cancelable_worker_async(
        source.get_cancellation(), delay_registered);
  });

  wait_until_true(delay_registered);
  CHECK(source.cancel());
  dispatcher.park();

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }

  CHECK(caught);
}
#endif

static void fire_and_forget_keeps_start_new_promise_alive() {
  test_dispatcher_host dispatcher;
  auto completed = std::atomic<bool>{false};

  cardio::fire_and_forget(cardio::promises::start_new([&] {
    return delayed_void_async(completed);
  }));

  dispatcher.park();

  CHECK(completed.load(std::memory_order_acquire));
}

static void worker_can_finish_after_caller_dispatcher_is_destroyed() {
  auto completed = std::atomic<bool>{false};

  {
    test_dispatcher_host dispatcher;
    auto promise = cardio::promises::start_new([&] {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      completed.store(true, std::memory_order_release);
    });
    (void)promise;
  }

  wait_until_true(completed);
}

//-----------------------------------------------------------------------------------------------

int main() {
  synchronous_value_runs_on_worker_thread();
  synchronous_void_completes();
  asynchronous_value_runs_with_worker_dispatcher();
  asynchronous_void_completes_with_worker_dispatcher();
  move_only_value_is_returned();
#if CARDIO_HAS_EXCEPTIONS
  synchronous_exception_is_rejected();
  asynchronous_exception_is_rejected();
  parent_cancellation_cancels_worker_async_operation();
#endif
  fire_and_forget_keeps_start_new_promise_alive();
  worker_can_finish_after_caller_dispatcher_is_destroyed();

  std::puts("cardio_start_new_test: PASS");
  return 0;
}
