// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "../test_helpers.h"

#ifndef __APPLE__
#error This test requires an Apple target.
#endif
static_assert(CARDIO_HAS_POSIX_FD, "Apple builds must enable POSIX fd waits by default");

int main() {
  cardio::dispatcher_host dispatcher;
  CHECK((dispatcher.get_feature() & cardio::dispatcher_feature::posix) ==
        cardio::dispatcher_feature::posix);
  std::puts("cardio_apple_config_test: PASS");
  return 0;
}
