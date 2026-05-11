// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>

#if CARDIO_WITH_SUPPLEMENTAL
#error CARDIO_WITH_SUPPLEMENTAL must be disabled for this test.
#endif

//-----------------------------------------------------------------------------------------------

int main() {
  test_dispatcher_host dispatcher;
  auto promise = cardio::resolved(42);

  CHECK(promise.is_ready());
  CHECK_EQ(promise.unsafe_result(), 42);

  std::puts("cardio_supplemental_disabled_test: PASS");
  return 0;
}
