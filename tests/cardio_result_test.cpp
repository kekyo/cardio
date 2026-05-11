// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>
#include <string>

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> await_resolved_async(bool& continued) {
  auto r = co_await cardio::resolved(7);
  continued = true;
  co_return r;
}

//-----------------------------------------------------------------------------------------------

static void resolved_promise_exposes_unsafe_result() {
  test_dispatcher_host dispatcher;

  auto p = cardio::resolved<std::string>("ready");

  CHECK_EQ(p.unsafe_result(), std::string("ready"));
}

//-----------------------------------------------------------------------------------------------

static void resolved_promise_exposes_ready_result() {
  test_dispatcher_host dispatcher;

  auto p = cardio::resolved<std::string>("ready");
  const auto& cp = p;

  CHECK(p.is_ready());
  CHECK(cp.is_ready());

  auto* result = p.try_result();
  const auto* const_result = cp.try_result();

  CHECK(result != nullptr);
  CHECK(const_result != nullptr);
  CHECK_EQ(*result, std::string("ready"));
  CHECK_EQ(*const_result, std::string("ready"));
}

//-----------------------------------------------------------------------------------------------

static void pending_promise_reports_not_ready() {
  test_dispatcher_host dispatcher;
  auto continued = false;

  auto p = await_resolved_async(continued);
  const auto& cp = p;

  CHECK(!continued);
  CHECK(!p.is_ready());
  CHECK(!cp.is_ready());
}

//-----------------------------------------------------------------------------------------------

static void pending_promise_does_not_expose_try_result() {
  test_dispatcher_host dispatcher;
  auto continued = false;

  auto p = await_resolved_async(continued);
  const auto& cp = p;

  CHECK(!continued);
  CHECK(p.try_result() == nullptr);
  CHECK(cp.try_result() == nullptr);
}

//-----------------------------------------------------------------------------------------------

static void pending_promise_exposes_result_after_parked() {
  test_dispatcher_host dispatcher;
  auto continued = false;

  auto p = await_resolved_async(continued);
  const auto& cp = p;

  dispatcher.park();

  CHECK(continued);
  CHECK(p.is_ready());
  CHECK(cp.is_ready());

  auto* result = p.try_result();
  const auto* const_result = cp.try_result();

  CHECK(result != nullptr);
  CHECK(const_result != nullptr);
  CHECK_EQ(*result, 7);
  CHECK_EQ(*const_result, 7);
}

//-----------------------------------------------------------------------------------------------

int main() {
  resolved_promise_exposes_unsafe_result();
  resolved_promise_exposes_ready_result();
  pending_promise_reports_not_ready();
  pending_promise_does_not_expose_try_result();
  pending_promise_exposes_result_after_parked();

  std::puts("cardio_result_test: PASS");
  return 0;
}
