// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>
#include <stdexcept>
#include <string>

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> throw_before_first_await_async() {
  throw std::runtime_error("before first await");
  co_return 0;
}

static void exception_before_first_await_is_thrown_immediately() {
  test_dispatcher_host dispatcher;
  auto callback_called = false;
  dispatcher.unhandled_exception(
      [&](std::exception_ptr) { callback_called = true; });

  auto caught = false;
  try {
    auto p = throw_before_first_await_async();
    (void)p;
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("before first await");
  }

  CHECK(caught);
  CHECK(!callback_called);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> throw_after_await_async(const char* message) {
  auto r = co_await cardio::resolved(1);
  (void)r;
  throw std::runtime_error(message);
  co_return 0;
}

static cardio::promise<void> catch_child_exception_async(int& result) {
  try {
    auto r = co_await throw_after_await_async("child failure");
    (void)r;
  } catch (const std::runtime_error& exception) {
    if (exception.what() == std::string("child failure")) {
      result = 13;
      co_return;
    }
  }

  result = 0;
  co_return;
}

static cardio::promise<void> throw_void_after_await_async(
    const char* message) {
  co_await cardio::resolved();
  throw std::runtime_error(message);
}

static cardio::promise<void> catch_void_child_exception_async(int& result) {
  try {
    co_await throw_void_after_await_async("void child failure");
  } catch (const std::runtime_error& exception) {
    if (exception.what() == std::string("void child failure")) {
      result = 17;
      co_return;
    }
  }

  result = 0;
  co_return;
}

static void child_exception_is_rethrown_from_await_resume() {
  test_dispatcher_host dispatcher;
  auto result = 0;

  auto p = catch_child_exception_async(result);
  (void)p;
  dispatcher.park();

  CHECK_EQ(result, 13);
}

static void void_child_exception_is_rethrown_from_await_resume() {
  test_dispatcher_host dispatcher;
  auto result = 0;

  auto p = catch_void_child_exception_async(result);
  (void)p;
  dispatcher.park();

  CHECK_EQ(result, 17);
}

//-----------------------------------------------------------------------------------------------

static void failed_promise_results_rethrow_stored_exception() {
  test_dispatcher_host dispatcher;
  dispatcher.unhandled_exception([](std::exception_ptr) {});

  auto p = throw_after_await_async("stored failure");
  dispatcher.park();

  auto unsafe_caught = false;
  try {
    (void)p.unsafe_result();
  } catch (const std::runtime_error& exception) {
    unsafe_caught = exception.what() == std::string("stored failure");
  }

  auto try_caught = false;
  try {
    (void)p.try_result();
  } catch (const std::runtime_error& exception) {
    try_caught = exception.what() == std::string("stored failure");
  }

  CHECK(unsafe_caught);
  CHECK(try_caught);
}

static void failed_void_promise_results_rethrow_stored_exception() {
  test_dispatcher_host dispatcher;
  dispatcher.unhandled_exception([](std::exception_ptr) {});

  auto p = throw_void_after_await_async("stored void failure");
  dispatcher.park();

  auto unsafe_caught = false;
  try {
    p.unsafe_result();
  } catch (const std::runtime_error& exception) {
    unsafe_caught = exception.what() == std::string("stored void failure");
  }

  auto try_caught = false;
  try {
    (void)p.try_result();
  } catch (const std::runtime_error& exception) {
    try_caught = exception.what() == std::string("stored void failure");
  }

  CHECK(unsafe_caught);
  CHECK(try_caught);
}

//-----------------------------------------------------------------------------------------------

static void rejected_promise_results_rethrow_stored_exception() {
  test_dispatcher_host dispatcher;

  auto p = cardio::rejected<int>(
      std::make_exception_ptr(std::runtime_error("rejected failure")));

  CHECK(p.is_ready());

  auto unsafe_caught = false;
  try {
    (void)p.unsafe_result();
  } catch (const std::runtime_error& exception) {
    unsafe_caught = exception.what() == std::string("rejected failure");
  }

  auto try_caught = false;
  try {
    (void)p.try_result();
  } catch (const std::runtime_error& exception) {
    try_caught = exception.what() == std::string("rejected failure");
  }

  CHECK(unsafe_caught);
  CHECK(try_caught);
}

static void rejected_void_promise_results_rethrow_stored_exception() {
  test_dispatcher_host dispatcher;

  auto p = cardio::rejected(
      std::make_exception_ptr(std::runtime_error("rejected void failure")));

  CHECK(p.is_ready());

  auto unsafe_caught = false;
  try {
    p.unsafe_result();
  } catch (const std::runtime_error& exception) {
    unsafe_caught = exception.what() == std::string("rejected void failure");
  }

  auto try_caught = false;
  try {
    (void)p.try_result();
  } catch (const std::runtime_error& exception) {
    try_caught = exception.what() == std::string("rejected void failure");
  }

  CHECK(unsafe_caught);
  CHECK(try_caught);
}

static void rejected_promise_accepts_exception_object() {
  test_dispatcher_host dispatcher;

  auto p = cardio::rejected<int>(
      std::runtime_error("rejected object failure"));

  CHECK(p.is_ready());

  auto caught = false;
  try {
    (void)p.unsafe_result();
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("rejected object failure");
  }

  CHECK(caught);
}

static void rejected_void_promise_accepts_exception_object() {
  test_dispatcher_host dispatcher;

  auto p = cardio::rejected(
      std::runtime_error("rejected void object failure"));

  CHECK(p.is_ready());

  auto caught = false;
  try {
    p.unsafe_result();
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("rejected void object failure");
  }

  CHECK(caught);
}

static cardio::promise<void> await_rejected_async(
    const char* message,
    int& result) {
  try {
    auto r = co_await cardio::rejected<int>(
        std::make_exception_ptr(std::runtime_error(message)));
    (void)r;
  } catch (const std::runtime_error& exception) {
    if (exception.what() == std::string(message)) {
      result = 23;
      co_return;
    }
  }

  result = 0;
  co_return;
}

static cardio::promise<void> await_rejected_void_async(
    const char* message,
    int& result) {
  try {
    co_await cardio::rejected(
        std::make_exception_ptr(std::runtime_error(message)));
  } catch (const std::runtime_error& exception) {
    if (exception.what() == std::string(message)) {
      result = 29;
      co_return;
    }
  }

  result = 0;
  co_return;
}

static void rejected_promise_is_rethrown_from_await_resume() {
  test_dispatcher_host dispatcher;
  auto value_result = 0;
  auto empty_result = 0;

  auto value = await_rejected_async("await rejected failure", value_result);
  auto empty = await_rejected_void_async(
      "await rejected void failure", empty_result);
  (void)value;
  (void)empty;
  dispatcher.park();

  CHECK_EQ(value_result, 23);
  CHECK_EQ(empty_result, 29);
}

static cardio::promise<int> reject_current_exception() {
  try {
    throw std::runtime_error("captured rejection");
  } catch (...) {
    return cardio::rejected<int>();
  }
}

static void reject_without_argument_captures_current_exception() {
  test_dispatcher_host dispatcher;

  auto p = reject_current_exception();

  CHECK(p.is_ready());

  auto caught = false;
  try {
    (void)p.unsafe_result();
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("captured rejection");
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

int main() {
  exception_before_first_await_is_thrown_immediately();
  child_exception_is_rethrown_from_await_resume();
  void_child_exception_is_rethrown_from_await_resume();
  failed_promise_results_rethrow_stored_exception();
  failed_void_promise_results_rethrow_stored_exception();
  rejected_promise_results_rethrow_stored_exception();
  rejected_void_promise_results_rethrow_stored_exception();
  rejected_promise_accepts_exception_object();
  rejected_void_promise_accepts_exception_object();
  rejected_promise_is_rethrown_from_await_resume();
  reject_without_argument_captures_current_exception();

  std::puts("cardio_exception_test: PASS");
  return 0;
}
