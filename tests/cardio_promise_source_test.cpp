// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> await_value_source_async(
    cardio::promise<int>& child,
    bool& continued) {
  auto value = co_await child;
  continued = true;
  co_return value + 1;
}

static cardio::promise<int> await_void_source_async(
    cardio::promise<void>& child,
    bool& continued) {
  co_await child;
  continued = true;
  co_return 23;
}

static cardio::promise<int> await_rejected_source_async(
    cardio::promise<int>& child,
    const char* message) {
  try {
    (void)co_await child;
  } catch (const std::runtime_error& exception) {
    if (exception.what() == std::string(message)) {
      co_return 31;
    }
  }

  co_return 0;
}

static cardio::promise<int> await_broken_source_async(
    cardio::promise<int>& child) {
  try {
    (void)co_await child;
  } catch (const std::runtime_error& exception) {
    if (exception.what() == std::string("cardio: broken promise_source")) {
      co_return 37;
    }
  }

  co_return 0;
}

static cardio::promise<void> await_owned_source_async(
    cardio::promise<int> child,
    bool& continued) {
  (void)co_await child;
  continued = true;
}

//-----------------------------------------------------------------------------------------------

static void value_source_resolve_resumes_awaiter() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();
  auto continued = false;

  auto parent = await_value_source_async(child, continued);

  CHECK(!continued);
  CHECK(!parent.is_ready());

  source.resolve(41);

  CHECK(!continued);

  dispatcher.park();

  CHECK(continued);
  CHECK(parent.is_ready());
  CHECK_EQ(parent.unsafe_result(), 42);
  CHECK(child.is_ready());
  CHECK_EQ(child.unsafe_result(), 41);
}

static void void_source_resolve_resumes_awaiter() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<void> source;
  auto child = source.get_promise();
  auto continued = false;

  auto parent = await_void_source_async(child, continued);

  CHECK(!continued);

  source.resolve();
  dispatcher.park();

  CHECK(continued);
  CHECK(parent.is_ready());
  CHECK_EQ(parent.unsafe_result(), 23);
  CHECK(child.is_ready());
  CHECK(child.try_result());
}

static void resolved_source_can_be_awaited_later() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();
  auto continued = false;

  source.resolve(10);

  CHECK(child.is_ready());

  auto parent = await_value_source_async(child, continued);

  CHECK(!continued);

  dispatcher.park();

  CHECK(continued);
  CHECK_EQ(parent.unsafe_result(), 11);
}

static void rejected_source_rethrows_from_results_and_await() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();

  source.reject(std::make_exception_ptr(
      std::runtime_error("source rejected failure")));

  CHECK(child.is_ready());

  auto unsafe_caught = false;
  try {
    (void)child.unsafe_result();
  } catch (const std::runtime_error& exception) {
    unsafe_caught =
        exception.what() == std::string("source rejected failure");
  }

  auto try_caught = false;
  try {
    (void)child.try_result();
  } catch (const std::runtime_error& exception) {
    try_caught = exception.what() == std::string("source rejected failure");
  }

  auto parent = await_rejected_source_async(child, "source rejected failure");
  dispatcher.park();

  CHECK(unsafe_caught);
  CHECK(try_caught);
  CHECK_EQ(parent.unsafe_result(), 31);
}

static void abandoned_source_rejects_waiting_promise() {
  test_dispatcher_host dispatcher;
  cardio::promise<int> child;
  auto parent = std::optional<cardio::promise<int>>{};

  {
    cardio::promise_source<int> source;
    child = source.get_promise();
    parent.emplace(await_broken_source_async(child));
  }

  dispatcher.park();

  CHECK(parent.has_value());
  CHECK_EQ(parent->unsafe_result(), 37);
  CHECK(child.is_ready());
}

static void get_promise_twice_fails() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;

  auto child = source.get_promise();

  auto caught = false;
  try {
    (void)source.get_promise();
  } catch (const std::logic_error&) {
    caught = true;
  }

  source.resolve(1);

  CHECK(caught);
  CHECK_EQ(child.unsafe_result(), 1);
}

static void double_completion_fails() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;

  source.resolve(1);

  auto resolve_caught = false;
  try {
    source.resolve(2);
  } catch (const std::logic_error&) {
    resolve_caught = true;
  }

  auto reject_caught = false;
  try {
    source.reject(std::runtime_error("late rejection"));
  } catch (const std::logic_error&) {
    reject_caught = true;
  }

  auto child = source.get_promise();

  CHECK(resolve_caught);
  CHECK(reject_caught);
  CHECK_EQ(child.unsafe_result(), 1);
}

static void resolve_after_reject_fails() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;

  source.reject(std::runtime_error("first rejection"));

  auto caught = false;
  try {
    source.resolve(2);
  } catch (const std::logic_error&) {
    caught = true;
  }

  CHECK(caught);
}

static void moved_from_source_use_fails() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto moved = std::move(source);

  auto caught = false;
  try {
    (void)source.get_promise();
  } catch (const std::logic_error&) {
    caught = true;
  }

  moved.resolve(5);
  auto child = moved.get_promise();

  CHECK(caught);
  CHECK_EQ(child.unsafe_result(), 5);
}

static void observing_promise_destruction_does_not_finish_source() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto resolved = std::atomic<bool>{false};

  {
    auto child = source.get_promise();
    (void)child;
  }

  auto resolver = std::thread([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    resolved.store(true, std::memory_order_release);
    source.resolve(9);
  });

  dispatcher.park();

  const auto resolved_before_join = resolved.load(std::memory_order_acquire);
  resolver.join();

  CHECK(resolved_before_join);
}

static void destroying_awaiter_unregisters_source_continuation() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();
  auto continued = false;

  {
    auto parent = await_owned_source_async(std::move(child), continued);
    CHECK(!parent.is_ready());
  }

  source.resolve(10);
  dispatcher.park();

  CHECK(!continued);
}

//-----------------------------------------------------------------------------------------------

int main() {
  value_source_resolve_resumes_awaiter();
  void_source_resolve_resumes_awaiter();
  resolved_source_can_be_awaited_later();
  rejected_source_rethrows_from_results_and_await();
  abandoned_source_rejects_waiting_promise();
  get_promise_twice_fails();
  double_completion_fails();
  resolve_after_reject_fails();
  moved_from_source_use_fails();
  observing_promise_destruction_does_not_finish_source();
  destroying_awaiter_unregisters_source_continuation();

  std::puts("cardio_promise_source_test: PASS");
  return 0;
}
