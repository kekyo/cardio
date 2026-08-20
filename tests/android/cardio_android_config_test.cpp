// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"

#if !defined(__ANDROID__)
#error This test must be compiled by the Android NDK.
#endif

#if !CARDIO_HAS_POSIX_FD
#error Android builds must enable POSIX file descriptor support.
#endif

#if CARDIO_WITH_LINUX_IO_URING
#error Android builds must not enable Linux io_uring support.
#endif

static_assert(
    (cardio::dispatcher_feature::android & cardio::dispatcher_feature::android) ==
    cardio::dispatcher_feature::android);

int main() {
  return 0;
}
