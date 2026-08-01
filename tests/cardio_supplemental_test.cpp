// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> await_delay_async(
    bool& continued,
    int& result) {
  co_await cardio::promises::delay(10);
  continued = true;
  result = 17;
}

static cardio::promise<void> await_canceled_delay_async(
    cardio::promise<void>& child,
    int& result) {
  try {
    co_await child;
  } catch (const cardio::canceled_exception& exception) {
    if (exception.what() == std::string("cardio: operation canceled")) {
      result = 1;
      co_return;
    }
  }

  result = 0;
  co_return;
}

template <typename T>
static bool wait_until_ready(const cardio::promise<T>& promise) {
  for (auto retry = 0; retry < 5000; ++retry) {
    if (promise.is_ready()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return promise.is_ready();
}

//-----------------------------------------------------------------------------------------------

static void delay_resolves_after_wait() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::promises::delay(20);

  CHECK(!promise.is_ready());

  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK(promise.try_result());
}

static void zero_timeout_source_is_already_canceled() {
  auto source = cardio::cancellations::timeout(0);
  auto cancellation = source.get_cancellation();

  CHECK(cancellation.is_cancellation_requested());
  CHECK(!source.cancel());
}

static void empty_any_is_not_canceled() {
  auto source = cardio::cancellations::any();
  auto cancellation = source.get_cancellation();

  CHECK(!cancellation.is_cancellation_requested());
  CHECK(source.cancel());
  CHECK(cancellation.is_cancellation_requested());
}

static void any_is_canceled_when_input_already_canceled() {
  cardio::cancellation_source first;
  cardio::cancellation_source second;

  CHECK(first.cancel());

  auto source = cardio::cancellations::any(
      first.get_cancellation(), second.get_cancellation());

  CHECK(source.get_cancellation().is_cancellation_requested());
  CHECK(!source.cancel());
}

static void any_cancels_when_any_input_cancels() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source first;
  cardio::cancellation_source second;

  auto source = cardio::cancellations::any(
      std::vector<cardio::cancellation>{
          first.get_cancellation(),
          second.get_cancellation()});
  auto cancellation = source.get_cancellation();
  auto callbacks = 0;

  auto registration = cancellation.on_cancellation_requested([&] {
    callbacks += 1;
  });

  CHECK(!cancellation.is_cancellation_requested());
  CHECK(second.cancel());
  CHECK(!cancellation.is_cancellation_requested());
  CHECK_EQ(callbacks, 0);

  dispatcher.park();

  CHECK(cancellation.is_cancellation_requested());
  CHECK_EQ(callbacks, 1);
  CHECK(!source.cancel());

  CHECK(first.cancel());
  dispatcher.park();
  CHECK_EQ(callbacks, 1);

  registration.reset();
}

static void timeout_source_cancels_after_wait() {
  test_dispatcher_host dispatcher;
  auto source = cardio::cancellations::timeout(50);
  auto cancellation = source.get_cancellation();
  auto callbacks = 0;

  auto registration = cancellation.on_cancellation_requested([&] {
    callbacks += 1;
  });

  CHECK(!cancellation.is_cancellation_requested());
  CHECK_EQ(callbacks, 0);

  dispatcher.park();

  CHECK(cancellation.is_cancellation_requested());
  CHECK_EQ(callbacks, 1);
  CHECK(!source.cancel());

  registration.reset();
}

static void timeout_source_can_outlive_dispatcher() {
  auto source_to_cancel = std::optional<cardio::cancellation_source>{};
  auto source_to_destroy = std::optional<cardio::cancellation_source>{};
  {
    test_dispatcher_host dispatcher;
    source_to_cancel.emplace(cardio::cancellations::timeout(60000));
    source_to_destroy.emplace(cardio::cancellations::timeout(60000));
  }

  CHECK(source_to_cancel->cancel());
  CHECK(source_to_cancel->get_cancellation().is_cancellation_requested());
  source_to_cancel.reset();
  source_to_destroy.reset();

  CHECK(!source_to_cancel.has_value());
  CHECK(!source_to_destroy.has_value());

  cardio::dispatcher_group group;
  auto source_with_surviving_group =
      std::optional<cardio::cancellation_source>{};
  {
    test_dispatcher_host dispatcher(group);
    source_with_surviving_group.emplace(
        cardio::cancellations::timeout(60000));
  }

  CHECK(source_with_surviving_group->cancel());
  source_with_surviving_group.reset();
  CHECK(!source_with_surviving_group.has_value());
}

static void timeout_cancellation_cancels_delay() {
  test_dispatcher_host dispatcher;
  auto source = cardio::cancellations::timeout(10);

  auto child = cardio::promises::delay(100, source.get_cancellation());
  auto parent_result = 0;
  auto parent = await_canceled_delay_async(child, parent_result);
  (void)parent;

  dispatcher.park();

  CHECK(source.get_cancellation().is_cancellation_requested());
  CHECK_EQ(parent_result, 1);

  auto caught = false;
  try {
    child.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }

  CHECK(caught);
}

static void delay_can_be_awaited() {
  test_dispatcher_host dispatcher;
  auto continued = false;
  auto result = 0;

  auto promise = await_delay_async(continued, result);
  (void)promise;

  CHECK(!continued);

  dispatcher.park();

  CHECK(continued);
  CHECK_EQ(result, 17);
}

static void already_canceled_delay_fails() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;

  CHECK(source.cancel());

  auto promise = cardio::promises::delay(20, source.get_cancellation());

  CHECK(promise.is_ready());

  auto caught = false;
  try {
    promise.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }

  CHECK(caught);
}

static void pending_delay_cancellation_fails() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;

  auto child = cardio::promises::delay(100, source.get_cancellation());
  auto parent_result = 0;
  auto parent = await_canceled_delay_async(child, parent_result);
  (void)parent;

  CHECK(!child.is_ready());
  CHECK(source.cancel());

  dispatcher.park();

  CHECK_EQ(parent_result, 1);

  auto caught = false;
  try {
    child.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }

  CHECK(caught);
}

static void completed_delay_wins_over_late_cancellation() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;

  auto promise = cardio::promises::delay(10, source.get_cancellation());

  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK(promise.try_result());
  CHECK(source.cancel());
  CHECK(promise.try_result());
}

static void earlier_timer_registered_while_parked_reschedules_wait() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);

  auto long_delay = cardio::promises::delay(1000);
  auto worker = std::thread([&] {
    cardio::set_current_dispatcher(&dispatcher);
    dispatcher.park(cardio::shutdown_mode::unsafe_immediate);
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  auto short_delay = cardio::promises::delay(10);
  CHECK(wait_until_ready(short_delay));
  CHECK(short_delay.try_result());
  CHECK(!long_delay.is_ready());

  group.shutdown();
  worker.join();
}

static void canceled_delay_does_not_complete_again_after_deadline() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;

  auto child = cardio::promises::delay(20, source.get_cancellation());
  auto parent_result = 0;
  auto parent = await_canceled_delay_async(child, parent_result);
  (void)parent;

  CHECK(source.cancel());
  dispatcher.park();
  CHECK_EQ(parent_result, 1);

  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  dispatcher.park();
  CHECK_EQ(parent_result, 1);

  auto caught = false;
  try {
    child.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }
  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

int main() {
  delay_resolves_after_wait();
  zero_timeout_source_is_already_canceled();
  empty_any_is_not_canceled();
  any_is_canceled_when_input_already_canceled();
  any_cancels_when_any_input_cancels();
  timeout_source_cancels_after_wait();
  timeout_source_can_outlive_dispatcher();
  timeout_cancellation_cancels_delay();
  delay_can_be_awaited();
  already_canceled_delay_fails();
  pending_delay_cancellation_fails();
  completed_delay_wins_over_late_cancellation();
  earlier_timer_registered_while_parked_reschedules_wait();
  canceled_delay_does_not_complete_again_after_deadline();

  std::puts("cardio_supplemental_test: PASS");
  return 0;
}
