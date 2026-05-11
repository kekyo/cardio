// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>
#include <utility>
#include <vector>

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> add_async(int a, int b) {
  return cardio::resolved(a + b);
}

static cardio::promise<void> main_async(int& result) {
  auto r = co_await add_async(1, 2);
  result = r * 3;
}

static void sample_code_equivalent_runs() {
  test_dispatcher_host dispatcher;
  auto result = 0;

  auto p = main_async(result);
  (void)p;
  dispatcher.park();

  CHECK_EQ(result, 9);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> await_resolved_async(
    bool& continued,
    int& result) {
  auto r = co_await cardio::resolved(7);
  continued = true;
  result = r;
}

static void resolved_await_runs_through_dispatcher() {
  test_dispatcher_host dispatcher;
  auto continued = false;
  auto result = 0;

  auto p = await_resolved_async(continued, result);
  (void)p;
  CHECK(!continued);

  dispatcher.park();

  CHECK(continued);
  CHECK_EQ(result, 7);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> immediate_coroutine_async() {
  co_return 40;
}

static cardio::promise<void> await_completed_child_async(
    bool& continued,
    int& result) {
  auto r = co_await immediate_coroutine_async();
  continued = true;
  result = r + 2;
}

static void completed_child_coroutine_can_be_awaited() {
  test_dispatcher_host dispatcher;
  auto continued = false;
  auto result = 0;

  auto p = await_completed_child_async(continued, result);
  (void)p;
  CHECK(!continued);

  dispatcher.park();

  CHECK(continued);
  CHECK_EQ(result, 42);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> chain_value_async(int depth) {
  if (depth == 0) {
    co_return 1;
  }

  auto previous = co_await chain_value_async(depth - 1);
  co_return previous + 1;
}

static cardio::promise<void> chain_async(int depth, int& result) {
  result = co_await chain_value_async(depth);
}

static void nested_coroutines_complete_in_order() {
  test_dispatcher_host dispatcher;
  auto result = 0;

  auto p = chain_async(5, result);
  (void)p;
  dispatcher.park();

  CHECK_EQ(result, 6);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> record_after_await_async(
    int value, std::vector<int>& events) {
  events.push_back(value * 10);
  auto r = co_await cardio::resolved(value);
  events.push_back(r);
  co_return r * 2;
}

static cardio::promise<void> record_result_after_await_async(
    int value,
    std::vector<int>& events,
    int& result) {
  result = co_await record_after_await_async(value, events);
}

static void multiple_promises_are_drained_by_one_dispatcher() {
  test_dispatcher_host dispatcher;
  auto events = std::vector<int>{};
  auto results = std::vector<int>{0, 0};

  auto first = record_result_after_await_async(1, events, results[0]);
  auto second = record_result_after_await_async(2, events, results[1]);
  (void)first;
  (void)second;

  CHECK_EQ(events.size(), 2U);
  CHECK_EQ(events[0], 10);
  CHECK_EQ(events[1], 20);

  dispatcher.park();

  CHECK_EQ(events.size(), 4U);
  CHECK_EQ(events[2], 1);
  CHECK_EQ(events[3], 2);
  CHECK_EQ(results[0], 2);
  CHECK_EQ(results[1], 4);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> move_target_async() {
  auto r = co_await cardio::resolved(11);
  co_return r + 1;
}

static void moved_promise_keeps_coroutine_ownership() {
  test_dispatcher_host dispatcher;

  auto original = move_target_async();
  auto moved = std::move(original);
  dispatcher.park();

  CHECK_EQ(moved.unsafe_result(), 12);
}

//-----------------------------------------------------------------------------------------------

int main() {
  sample_code_equivalent_runs();
  resolved_await_runs_through_dispatcher();
  completed_child_coroutine_can_be_awaited();
  nested_coroutines_complete_in_order();
  multiple_promises_are_drained_by_one_dispatcher();
  moved_promise_keeps_coroutine_ownership();

  std::puts("cardio_await_test: PASS");
  return 0;
}
