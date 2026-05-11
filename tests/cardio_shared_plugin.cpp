// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"

#include <optional>

#if defined(_WIN32)
#define CARDIO_SHARED_TEST_API extern "C" __declspec(dllexport)
#else
#define CARDIO_SHARED_TEST_API extern "C"
#endif

static std::optional<cardio::promise<void>> pending_promise;

static cardio::promise<void> shared_plugin_async(
    cardio::dispatcher* expected_dispatcher,
    bool* dispatcher_matched,
    int* result) {
  *dispatcher_matched =
      cardio::unsafe_get_current_dispatcher() == expected_dispatcher;
  auto value = co_await cardio::resolved(42);
  *result = value;
}

CARDIO_SHARED_TEST_API bool cardio_shared_plugin_run(
    cardio::dispatcher* expected_dispatcher,
    bool* dispatcher_matched,
    int* result) {
  try {
    pending_promise.emplace(
        shared_plugin_async(expected_dispatcher, dispatcher_matched, result));
    return true;
  } catch (...) {
    return false;
  }
}
