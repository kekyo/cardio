// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>

//-----------------------------------------------------------------------------------------------

static void primitives_can_be_disabled_without_breaking_core_api() {
  test_dispatcher_host dispatcher;
  auto promise = cardio::resolved(123);

  CHECK(promise.is_ready());
  CHECK_EQ(promise.unsafe_result(), 123);
}

//-----------------------------------------------------------------------------------------------

int main() {
  primitives_can_be_disabled_without_breaking_core_api();

  std::puts("cardio_primitives_disabled_test: PASS");
  return 0;
}
