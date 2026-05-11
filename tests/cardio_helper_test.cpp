// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> await_all_async(bool& continued, int& result) {
  auto values = co_await cardio::promises::all(
      cardio::promises::delay(1), cardio::resolved(20), cardio::resolved(22));
  continued = true;
  result = std::get<0>(values) + std::get<1>(values);
}

#if CARDIO_HAS_EXCEPTIONS
static cardio::promise<void> await_failed_all_async(int& result) {
  try {
    auto values = co_await cardio::promises::all(
        cardio::promises::delay(1000),
        cardio::rejected<int>(std::runtime_error("all failed")));
    (void)values;
  } catch (const std::runtime_error& exception) {
    if (std::string(exception.what()).find("all failed") !=
        std::string::npos) {
      result = 1;
      co_return;
    }
  }

  result = 0;
  co_return;
}

static cardio::promise<void> await_canceled_all_async(
    cardio::promise<void> child,
    int& result) {
  try {
    co_await cardio::promises::all(std::move(child));
  } catch (const cardio::canceled_exception&) {
    result = 1;
    co_return;
  }

  result = 0;
  co_return;
}
#endif

//-----------------------------------------------------------------------------------------------

static void variadic_value_promises_are_collected_in_input_order() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::promises::all(
      cardio::resolved(3),
      cardio::resolved(std::string("four")),
      cardio::resolved(5));

  CHECK(!promise.is_ready());

  dispatcher.park();

  CHECK(promise.is_ready());
  auto& values = promise.unsafe_result();
  CHECK_EQ(std::get<0>(values), 3);
  CHECK_EQ(std::get<1>(values), std::string("four"));
  CHECK_EQ(std::get<2>(values), 5);
}

static void void_promises_resolve_to_void() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::promises::all(
      cardio::promises::delay(1), cardio::resolved());

  CHECK(!promise.is_ready());

  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK(promise.try_result());
}

static void mixed_promises_omit_void_results() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::promises::all(
      cardio::resolved(7),
      cardio::promises::delay(1),
      cardio::resolved(std::string("done")));

  static_assert(std::is_same_v<
      decltype(promise),
      cardio::promise<std::tuple<int, std::string>>>);

  dispatcher.park();

  CHECK(promise.is_ready());
  auto& values = promise.unsafe_result();
  CHECK_EQ(std::get<0>(values), 7);
  CHECK_EQ(std::get<1>(values), std::string("done"));
}

static void vector_promises_are_collected_in_input_order() {
  test_dispatcher_host dispatcher;
  auto promises = std::vector<cardio::promise<int>>{};
  promises.push_back(cardio::resolved(1));
  promises.push_back(cardio::resolved(2));
  promises.push_back(cardio::resolved(3));

  auto promise = cardio::promises::all(std::move(promises));

  CHECK(!promise.is_ready());

  dispatcher.park();

  CHECK(promise.is_ready());
  auto& values = promise.unsafe_result();
  CHECK_EQ(values.size(), static_cast<std::size_t>(3));
  CHECK_EQ(values[0], 1);
  CHECK_EQ(values[1], 2);
  CHECK_EQ(values[2], 3);
}

static void move_only_values_are_collected() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::promises::all(
      cardio::resolved(std::make_unique<int>(8)),
      cardio::resolved(std::make_unique<int>(9)));

  dispatcher.park();

  CHECK(promise.is_ready());
  auto& values = promise.unsafe_result();
  CHECK_EQ(*std::get<0>(values), 8);
  CHECK_EQ(*std::get<1>(values), 9);
}

static void empty_inputs_resolve_immediately() {
  test_dispatcher_host dispatcher;

  auto variadic_promise = cardio::promises::all();
  CHECK(variadic_promise.is_ready());
  CHECK(variadic_promise.try_result());

  auto vector_promise = cardio::promises::all(
      std::vector<cardio::promise<int>>{});
  CHECK(vector_promise.is_ready());
  CHECK_EQ(vector_promise.unsafe_result().size(), static_cast<std::size_t>(0));

  auto void_vector_promise = cardio::promises::all(
      std::vector<cardio::promise<void>>{});
  CHECK(void_vector_promise.is_ready());
  CHECK(void_vector_promise.try_result());
}

static void all_can_be_awaited() {
  test_dispatcher_host dispatcher;
  auto continued = false;
  auto result = 0;

  auto promise = await_all_async(continued, result);
  (void)promise;

  CHECK(!continued);

  dispatcher.park();

  CHECK(continued);
  CHECK_EQ(result, 42);
}

#if CARDIO_HAS_EXCEPTIONS
static void failed_input_fails_fast() {
  test_dispatcher_host dispatcher;
  const auto started = std::chrono::steady_clock::now();
  auto result = 0;

  auto promise = await_failed_all_async(result);
  (void)promise;

  dispatcher.park();

  const auto elapsed = std::chrono::steady_clock::now() - started;
#if CARDIO_HAS_WIN32_HANDLE
  CHECK(elapsed < std::chrono::seconds(5));
#else
  CHECK(elapsed < std::chrono::milliseconds(500));
#endif
  CHECK_EQ(result, 1);
}

static void canceled_input_fails() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;

  auto child = cardio::promises::delay(100, source.get_cancellation());
  auto result = 0;
  auto promise = await_canceled_all_async(std::move(child), result);
  (void)promise;

  CHECK(source.cancel());

  dispatcher.park();

  CHECK_EQ(result, 1);
}
#endif

//-----------------------------------------------------------------------------------------------

int main() {
  variadic_value_promises_are_collected_in_input_order();
  void_promises_resolve_to_void();
  mixed_promises_omit_void_results();
  vector_promises_are_collected_in_input_order();
  move_only_values_are_collected();
  empty_inputs_resolve_immediately();
  all_can_be_awaited();
#if CARDIO_HAS_EXCEPTIONS
  failed_input_fails_fast();
  canceled_input_fails();
#endif

  std::puts("cardio_helper_test: PASS");
  return 0;
}
