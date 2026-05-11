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
#include <utility>

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> delayed_void_async(bool& completed) {
  co_await cardio::promises::delay(10);
  completed = true;
}

static cardio::promise<int> delayed_value_async(bool& completed) {
  co_await cardio::promises::delay(10);
  completed = true;
  co_return 29;
}

static cardio::promise<void> await_source_async(
    cardio::promise<int> child,
    int& result) {
  result = co_await child;
}

static cardio::promise<void> failing_after_await_async() {
  co_await cardio::resolved();
  throw std::runtime_error("fire_and_forget failure");
}

//-----------------------------------------------------------------------------------------------

static void fire_and_forget_keeps_void_root_promise_alive() {
  test_dispatcher_host dispatcher;
  auto completed = false;

  cardio::fire_and_forget(delayed_void_async(completed));
  dispatcher.park();

  CHECK(completed);
}

static void fire_and_forget_keeps_value_root_promise_alive() {
  test_dispatcher_host dispatcher;
  auto completed = false;

  cardio::fire_and_forget(delayed_value_async(completed));
  dispatcher.park();

  CHECK(completed);
}

static void fire_and_forget_keeps_source_observer_alive() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();
  auto result = 0;
  auto resolved = std::atomic<bool>{false};

  cardio::fire_and_forget(await_source_async(std::move(child), result));

  auto resolver = std::thread([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    resolved.store(true, std::memory_order_release);
    source.resolve(37);
  });

  dispatcher.park();

  const auto resolved_before_join = resolved.load(std::memory_order_acquire);
  resolver.join();

  CHECK(resolved_before_join);
  CHECK_EQ(result, 37);
}

static void fire_and_forget_reports_unhandled_exception() {
  test_dispatcher_host dispatcher;
  auto callback_called = false;
  auto message = std::string{};
  dispatcher.unhandled_exception([&](std::exception_ptr exception) {
    callback_called = true;
    try {
      std::rethrow_exception(exception);
    } catch (const std::runtime_error& caught) {
      message = caught.what();
    }
  });

  cardio::fire_and_forget(failing_after_await_async());
  dispatcher.park();

  CHECK(callback_called);
  CHECK_EQ(message, std::string("fire_and_forget failure"));
}

//-----------------------------------------------------------------------------------------------

int main() {
  fire_and_forget_keeps_void_root_promise_alive();
  fire_and_forget_keeps_value_root_promise_alive();
  fire_and_forget_keeps_source_observer_alive();
  fire_and_forget_reports_unhandled_exception();

  std::puts("cardio_fire_and_forget_test: PASS");
  return 0;
}
