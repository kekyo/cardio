// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>
#include <utility>

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> await_resolved_void_async(bool& continued) {
  co_await cardio::resolved();
  continued = true;
}

static void void_resolved_await_runs_through_dispatcher() {
  test_dispatcher_host dispatcher;
  auto continued = false;

  auto p = await_resolved_void_async(continued);
  CHECK(!continued);

  dispatcher.park();

  CHECK(continued);
  CHECK(p.is_ready());
  CHECK(p.try_result());
  p.unsafe_result();
}

//-----------------------------------------------------------------------------------------------

static void resolved_void_promise_exposes_ready_result() {
  test_dispatcher_host dispatcher;

  auto p = cardio::resolved();
  const auto& cp = p;

  CHECK(p.is_ready());
  CHECK(cp.is_ready());
  CHECK(p.try_result());
  CHECK(cp.try_result());

  p.unsafe_result();
  cp.unsafe_result();
}

//-----------------------------------------------------------------------------------------------

static void pending_void_promise_reports_not_ready() {
  test_dispatcher_host dispatcher;
  auto continued = false;

  auto p = await_resolved_void_async(continued);
  const auto& cp = p;

  CHECK(!continued);
  CHECK(!p.is_ready());
  CHECK(!cp.is_ready());
  CHECK(!p.try_result());
  CHECK(!cp.try_result());
}

//-----------------------------------------------------------------------------------------------

static void pending_void_promise_exposes_result_after_parked() {
  test_dispatcher_host dispatcher;
  auto continued = false;

  auto p = await_resolved_void_async(continued);
  const auto& cp = p;

  dispatcher.park();

  CHECK(continued);
  CHECK(p.is_ready());
  CHECK(cp.is_ready());
  CHECK(p.try_result());
  CHECK(cp.try_result());
}

//-----------------------------------------------------------------------------------------------

static void moved_void_promise_keeps_coroutine_ownership() {
  test_dispatcher_host dispatcher;
  auto continued = false;

  auto original = await_resolved_void_async(continued);
  auto moved = std::move(original);
  dispatcher.park();

  CHECK(continued);
  CHECK(moved.is_ready());
  moved.unsafe_result();
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> await_void_then_return_async(bool& continued) {
  co_await cardio::resolved();
  continued = true;
  co_return 23;
}

static cardio::promise<void> await_value_then_return_void_async(
    bool& continued) {
  auto r = co_await cardio::resolved(19);
  continued = r == 19;
}

static void value_and_void_promises_can_await_each_other() {
  test_dispatcher_host dispatcher;
  auto value_continued = false;
  auto void_continued = false;

  auto value_promise = await_void_then_return_async(value_continued);
  auto void_promise = await_value_then_return_void_async(void_continued);
  dispatcher.park();

  CHECK(value_continued);
  CHECK(void_continued);
  CHECK_EQ(value_promise.unsafe_result(), 23);
  CHECK(void_promise.try_result());
}

//-----------------------------------------------------------------------------------------------

int main() {
  void_resolved_await_runs_through_dispatcher();
  resolved_void_promise_exposes_ready_result();
  pending_void_promise_reports_not_ready();
  pending_void_promise_exposes_result_after_parked();
  moved_void_promise_keeps_coroutine_ownership();
  value_and_void_promises_can_await_each_other();

  std::puts("cardio_void_test: PASS");
  return 0;
}
