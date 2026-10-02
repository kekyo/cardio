// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#ifndef CARDIO_SHARED_LIB
#define CARDIO_SHARED_LIB 1
#endif

#ifndef CARDIO_BUILD_SHARED_LIB
#define CARDIO_BUILD_SHARED_LIB 1
#endif

#if !CARDIO_SHARED_LIB || !CARDIO_BUILD_SHARED_LIB
#error "libcardio.cpp requires CARDIO_SHARED_LIB=1 and CARDIO_BUILD_SHARED_LIB=1"
#endif

#include "cardio.h"

namespace cardio::internal {
  CARDIO_API runtime_thread_state& runtime_state() noexcept {
    thread_local runtime_thread_state state;
    return state;
  }
#if CARDIO_HAS_WIN32_HANDLE
  CARDIO_API win32_legacy_io_registry& legacy_io_registry() {
    static win32_legacy_io_registry registry;
    return registry;
  }
#endif
}
