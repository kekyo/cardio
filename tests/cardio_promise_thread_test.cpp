// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

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

static void park_on_two_threads(test_dispatcher_host& dispatcher) {
  auto first = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  auto second = std::thread(
      [&] { park_current_dispatcher(dispatcher); });

  first.join();
  second.join();
}

static void wait_until_allowed(
    std::atomic<bool>& running,
    std::atomic<bool>& allow_continue) {
  running.store(true, std::memory_order_release);
  while (!allow_continue.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> delayed_value_async(
    std::atomic<bool>& running,
    std::atomic<bool>& allow_continue) {
  co_await cardio::resolved();
  wait_until_allowed(running, allow_continue);
  co_return 41;
}

static cardio::promise<void> delayed_void_async(
    std::atomic<bool>& running,
    std::atomic<bool>& allow_continue) {
  co_await cardio::resolved();
  wait_until_allowed(running, allow_continue);
}

static cardio::promise<int> delayed_value_failure_async(
    std::atomic<bool>& running,
    std::atomic<bool>& allow_continue) {
  co_await cardio::resolved();
  wait_until_allowed(running, allow_continue);
  throw std::runtime_error("thread value failure");
  co_return 0;
}

static cardio::promise<void> delayed_void_failure_async(
    std::atomic<bool>& running,
    std::atomic<bool>& allow_continue) {
  co_await cardio::resolved();
  wait_until_allowed(running, allow_continue);
  throw std::runtime_error("thread void failure");
}

static cardio::promise<void> await_value_child_async(
    cardio::promise<int>& child,
    std::atomic<int>& continuations,
    int& result) {
  auto value = co_await child;
  continuations.fetch_add(1, std::memory_order_acq_rel);
  result = value + 1;
}

static cardio::promise<void> await_completed_value_child_async(
    std::atomic<int>& continuations,
    int& result) {
  auto child = cardio::resolved(23);
  auto value = co_await child;
  continuations.fetch_add(1, std::memory_order_acq_rel);
  result = value + 1;
}

//-----------------------------------------------------------------------------------------------

static void child_completion_on_worker_resumes_parent_once() {
  test_dispatcher_host dispatcher;
  auto child_running = std::atomic<bool>{false};
  auto allow_child = std::atomic<bool>{false};
  auto continuations = std::atomic<int>{0};
  auto parent_result = 0;

  auto child = delayed_value_async(child_running, allow_child);
  auto parent = await_value_child_async(child, continuations, parent_result);
  (void)parent;

  auto first = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  auto second = std::thread(
      [&] { park_current_dispatcher(dispatcher); });

  wait_until_true(child_running);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK_EQ(continuations.load(std::memory_order_acquire), 0);

  allow_child.store(true, std::memory_order_release);
  first.join();
  second.join();

  CHECK_EQ(continuations.load(std::memory_order_acquire), 1);
  CHECK_EQ(parent_result, 42);
}

static void completed_child_before_await_resumes_parent_once() {
  test_dispatcher_host dispatcher;
  auto continuations = std::atomic<int>{0};
  auto parent_result = 0;

  auto parent = await_completed_value_child_async(continuations, parent_result);
  (void)parent;
  park_on_two_threads(dispatcher);

  CHECK_EQ(continuations.load(std::memory_order_acquire), 1);
  CHECK_EQ(parent_result, 24);
}

static void value_promise_result_is_visible_after_worker_completion() {
  test_dispatcher_host dispatcher;
  auto running = std::atomic<bool>{false};
  auto allow_continue = std::atomic<bool>{false};

  auto promise = delayed_value_async(running, allow_continue);
  auto worker = std::thread(
      [&] { park_current_dispatcher(dispatcher); });

  wait_until_true(running);
  allow_continue.store(true, std::memory_order_release);
  worker.join();

  CHECK(promise.is_ready());
  auto* result = promise.try_result();
  CHECK(result != nullptr);
  CHECK_EQ(*result, 41);
  CHECK_EQ(promise.unsafe_result(), 41);
}

static void void_promise_result_is_visible_after_worker_completion() {
  test_dispatcher_host dispatcher;
  auto running = std::atomic<bool>{false};
  auto allow_continue = std::atomic<bool>{false};

  auto promise = delayed_void_async(running, allow_continue);
  auto worker = std::thread(
      [&] { park_current_dispatcher(dispatcher); });

  wait_until_true(running);
  allow_continue.store(true, std::memory_order_release);
  worker.join();

  CHECK(promise.is_ready());
  CHECK(promise.try_result());
  promise.unsafe_result();
}

static void value_promise_exception_is_visible_after_worker_completion() {
  test_dispatcher_host dispatcher;
  dispatcher.unhandled_exception([](std::exception_ptr) {});
  auto running = std::atomic<bool>{false};
  auto allow_continue = std::atomic<bool>{false};

  auto promise = delayed_value_failure_async(running, allow_continue);
  auto worker = std::thread(
      [&] { park_current_dispatcher(dispatcher); });

  wait_until_true(running);
  allow_continue.store(true, std::memory_order_release);
  worker.join();

  CHECK(promise.is_ready());

  auto try_caught = false;
  try {
    (void)promise.try_result();
  } catch (const std::runtime_error& exception) {
    try_caught = exception.what() == std::string("thread value failure");
  }

  auto unsafe_caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const std::runtime_error& exception) {
    unsafe_caught = exception.what() == std::string("thread value failure");
  }

  CHECK(try_caught);
  CHECK(unsafe_caught);
}

static void void_promise_exception_is_visible_after_worker_completion() {
  test_dispatcher_host dispatcher;
  dispatcher.unhandled_exception([](std::exception_ptr) {});
  auto running = std::atomic<bool>{false};
  auto allow_continue = std::atomic<bool>{false};

  auto promise = delayed_void_failure_async(running, allow_continue);
  auto worker = std::thread(
      [&] { park_current_dispatcher(dispatcher); });

  wait_until_true(running);
  allow_continue.store(true, std::memory_order_release);
  worker.join();

  CHECK(promise.is_ready());

  auto try_caught = false;
  try {
    (void)promise.try_result();
  } catch (const std::runtime_error& exception) {
    try_caught = exception.what() == std::string("thread void failure");
  }

  auto unsafe_caught = false;
  try {
    promise.unsafe_result();
  } catch (const std::runtime_error& exception) {
    unsafe_caught = exception.what() == std::string("thread void failure");
  }

  CHECK(try_caught);
  CHECK(unsafe_caught);
}

//-----------------------------------------------------------------------------------------------

int main() {
  child_completion_on_worker_resumes_parent_once();
  completed_child_before_await_resumes_parent_once();
  value_promise_result_is_visible_after_worker_completion();
  void_promise_result_is_visible_after_worker_completion();
  value_promise_exception_is_visible_after_worker_completion();
  void_promise_exception_is_visible_after_worker_completion();

  std::puts("cardio_promise_thread_test: PASS");
  return 0;
}
