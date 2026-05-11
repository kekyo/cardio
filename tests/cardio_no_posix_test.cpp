// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>

#if CARDIO_HAS_POSIX_FD
#error CARDIO_HAS_POSIX_FD must be disabled for this test.
#endif

#if CARDIO_WITH_LINUX_IO_URING
#error CARDIO_WITH_LINUX_IO_URING must be disabled for this test.
#endif

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> make_value_async() {
  co_return 42;
}

static cardio::promise<void> await_value_async(
    cardio::promise<int>& child,
    int& result) {
  const auto value = co_await child;
  result = value + 1;
}

static cardio::promise<void> await_delay_async(int& result) {
  co_await cardio::promises::delay(10);
  result = 59;
}

int main() {
  test_dispatcher_host dispatcher;
  auto posted = false;

  auto resolved = cardio::resolved(41);
  CHECK(resolved.is_ready());
  CHECK_EQ(resolved.unsafe_result(), 41);

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] { posted = true; });

  auto child = make_value_async();
  auto parent_result = 0;
  auto delayed_result = 0;
  auto parent = await_value_async(child, parent_result);
  auto delayed = await_delay_async(delayed_result);
  (void)parent;
  (void)delayed;
  dispatcher.park();

  CHECK(posted);
  CHECK_EQ(parent_result, 43);
  CHECK_EQ(delayed_result, 59);

  auto timeout_source = cardio::cancellations::timeout(10);
  CHECK(!timeout_source.get_cancellation().is_cancellation_requested());
  dispatcher.park();
  CHECK(timeout_source.get_cancellation().is_cancellation_requested());
  CHECK(!timeout_source.cancel());

  cardio::cancellation_source first;
  cardio::cancellation_source second;
  auto any_source = cardio::cancellations::any(
      first.get_cancellation(), second.get_cancellation());
  CHECK(!any_source.get_cancellation().is_cancellation_requested());
  CHECK(second.cancel());
  CHECK(!any_source.get_cancellation().is_cancellation_requested());
  dispatcher.park();
  CHECK(any_source.get_cancellation().is_cancellation_requested());
  CHECK(!any_source.cancel());

  std::puts("cardio_no_posix_test: PASS");
  return 0;
}
