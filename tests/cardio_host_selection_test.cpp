// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include <cstdio>
#include <type_traits>

#ifdef CARDIO_DISPATCHER_HOST_KQUEUE
using worker_host = cardio::dispatcher_host_kqueue;
#else
using worker_host = cardio::dispatcher_host;
#endif
static_assert(requires(worker_host& host) { host.park(); });

#ifdef CARDIO_DISPATCHER_HOST_CFRUNLOOP
using native_loop_host = cardio::dispatcher_host_cfrunloop;
using ui_host = cardio::dispatcher_host_cfrunloop_auto;
#elif defined(__ANDROID__)
using native_loop_host = cardio::dispatcher_host_android;
using ui_host = cardio::dispatcher_host_android_auto;
#elif CARDIO_HAS_WIN32_HANDLE
using native_loop_host = cardio::dispatcher_host_win32;
using ui_host = cardio::dispatcher_host_win32_auto;
#endif
#if defined(CARDIO_DISPATCHER_HOST_CFRUNLOOP) || defined(__ANDROID__) || CARDIO_HAS_WIN32_HANDLE
static_assert(requires(native_loop_host& host) { host.park(); });
static_assert(std::is_base_of_v<cardio::dispatcher, ui_host>);
#endif
#if defined(CARDIO_DISPATCHER_HOST_KQUEUE) != (defined(__APPLE__) && CARDIO_HAS_POSIX_FD)
#error kqueue availability does not match the target.
#endif
#if defined(CARDIO_DISPATCHER_HOST_CFRUNLOOP) != (defined(__APPLE__) && CARDIO_WITH_CFRUNLOOP)
#error Core Foundation availability does not match the build configuration.
#endif

int main() {
  std::puts("cardio_host_selection_test: PASS");
}
