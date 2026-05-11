// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#pragma once

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                   \
  do {                                                                     \
    if (!(condition)) {                                                    \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,        \
                   __LINE__, #condition);                                 \
      std::exit(1);                                                        \
    }                                                                      \
  } while (false)

#define CHECK_EQ(actual, expected) CHECK((actual) == (expected))

#if CARDIO_HAS_WIN32_HANDLE
using test_dispatcher_host = cardio::dispatcher_host_win32;
#else
using test_dispatcher_host = cardio::dispatcher_host;
#endif

inline void park_current_dispatcher(test_dispatcher_host& dispatcher) {
  cardio::set_current_dispatcher(&dispatcher);
  dispatcher.park();
}
