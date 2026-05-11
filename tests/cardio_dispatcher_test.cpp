// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

//-----------------------------------------------------------------------------------------------

template <typename T>
concept has_dispatcher_park = requires(T& dispatcher) {
  dispatcher.park();
};

template <typename T>
concept has_dispatcher_unsafe_post = requires(T& dispatcher) {
  dispatcher.unsafe_post(std::function<void()>{});
};

static_assert(std::is_abstract_v<cardio::dispatcher>);
static_assert(std::is_base_of_v<cardio::dispatcher, test_dispatcher_host>);
static_assert(!has_dispatcher_park<cardio::dispatcher>);
static_assert(has_dispatcher_park<test_dispatcher_host>);
static_assert(!has_dispatcher_unsafe_post<test_dispatcher_host>);

//-----------------------------------------------------------------------------------------------

static bool wait_until_true(const std::atomic<bool>& flag) {
  for (auto retry = 0; retry < 5000; ++retry) {
    if (flag.load(std::memory_order_acquire)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return flag.load(std::memory_order_acquire);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> await_resolved_async(
    bool& continued,
    int& result) {
  auto r = co_await cardio::resolved(7);
  continued = true;
  result = r;
}

static cardio::promise<int> throw_after_await_async(const char* message) {
  auto r = co_await cardio::resolved(1);
  (void)r;
  throw std::runtime_error(message);
  co_return 0;
}

static cardio::promise<int> throw_non_standard_after_await_async() {
  auto r = co_await cardio::resolved(1);
  (void)r;
  throw 1;
  co_return 0;
}

static cardio::promise<void> await_twice_async(
    bool& continued,
    int& result) {
  auto first = co_await cardio::resolved(1);
  auto second = co_await cardio::resolved(2);
  continued = true;
  result = first + second;
}

static cardio::promise<void> wait_between_awaits_async(
    std::atomic<bool>& running,
    std::atomic<bool>& allow_continue,
    int& result) {
  auto first = co_await cardio::resolved(1);
  (void)first;

  running.store(true, std::memory_order_release);
  while (!allow_continue.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  result = co_await cardio::resolved(2);
}

static cardio::promise<void> switch_to_dispatcher_async(
    cardio::dispatcher& target,
    std::thread::id& resumed_thread) {
  co_await cardio::switch_to(target);
  resumed_thread = std::this_thread::get_id();
}

static cardio::promise<void> switch_to_roundtrip_async(
    cardio::dispatcher& worker,
    cardio::dispatcher& main,
    std::thread::id& worker_thread,
    std::thread::id& main_thread) {
  co_await cardio::switch_to(worker);
  worker_thread = std::this_thread::get_id();

  co_await cardio::switch_to(main);
  main_thread = std::this_thread::get_id();
}

static cardio::promise<void> switch_then_resolve_async(
    cardio::dispatcher& target,
    std::thread::id& after_switch,
    std::thread::id& after_resolve) {
  co_await cardio::switch_to(target);
  after_switch = std::this_thread::get_id();

  co_await cardio::resolved();
  after_resolve = std::this_thread::get_id();
}

static cardio::promise<void> switch_to_same_dispatcher_async(
    cardio::dispatcher& target,
    bool& continued) {
  co_await cardio::switch_to(target);
  continued = true;
}

static cardio::promise<void> switch_to_other_group_async(
    cardio::dispatcher& target) {
  co_await cardio::switch_to(target);
  co_return;
}

static cardio::promise<void> empty_async() {
  co_return;
}

static cardio::promise<void> delayed_switch_back_async(
    cardio::dispatcher& worker,
    cardio::dispatcher& main,
    std::atomic<bool>& worker_running,
    std::atomic<bool>& allow_switch,
    std::thread::id& main_thread) {
  co_await cardio::switch_to(worker);
  worker_running.store(true, std::memory_order_release);

  while (!allow_switch.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  co_await cardio::switch_to(main);
  main_thread = std::this_thread::get_id();
}

//-----------------------------------------------------------------------------------------------

static void dispatcher_callback_receives_unhandled_exception() {
  test_dispatcher_host dispatcher;
  auto callback_called = false;
  auto message = std::string();
  dispatcher.unhandled_exception([&](std::exception_ptr exception) {
    callback_called = true;
    try {
      std::rethrow_exception(exception);
    } catch (const std::runtime_error& caught) {
      message = caught.what();
    }
  });

  auto p = throw_after_await_async("root failure");
  (void)p;
  dispatcher.park();

  CHECK(callback_called);
  CHECK_EQ(message, std::string("root failure"));
}

//-----------------------------------------------------------------------------------------------

static void dispatcher_continues_after_callback_handles_exception() {
  test_dispatcher_host dispatcher;
  auto callback_count = 0;
  dispatcher.unhandled_exception([&](std::exception_ptr) { ++callback_count; });

  auto failing = throw_after_await_async("handled root failure");
  (void)failing;
  auto continued = false;
  auto succeeded_result = 0;
  auto succeeding = await_resolved_async(continued, succeeded_result);
  (void)succeeding;
  dispatcher.park();

  CHECK_EQ(callback_count, 1);
  CHECK(continued);
  CHECK_EQ(succeeded_result, 7);
}

//-----------------------------------------------------------------------------------------------

static void dispatcher_ignores_callback_exception() {
  test_dispatcher_host dispatcher;
  dispatcher.unhandled_exception([](std::exception_ptr) {
    throw std::logic_error("callback failure");
  });

  auto failing = throw_after_await_async("root failure");
  (void)failing;
  auto continued = false;
  auto succeeded_result = 0;
  auto succeeding = await_resolved_async(continued, succeeded_result);
  (void)succeeding;
  auto caught = false;
  try {
    dispatcher.park();
  } catch (...) {
    caught = true;
  }

  CHECK(!caught);
  CHECK(continued);
  CHECK_EQ(succeeded_result, 7);
}

//-----------------------------------------------------------------------------------------------

static void dispatcher_not_throws_unhandled_exception_without_callback() {
  test_dispatcher_host dispatcher;
  auto output = std::ostringstream();
  auto* original_clog_buffer = std::clog.rdbuf(output.rdbuf());

  auto p = throw_after_await_async("uncaught root failure");
  (void)p;
  auto caught = false;
  try {
    dispatcher.park();
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("uncaught root failure");
  }
  std::clog.rdbuf(original_clog_buffer);

  CHECK(!caught);
  CHECK(output.str().find(
            "cardio: ignored unhandled exception: uncaught root failure") !=
        std::string::npos);
}

//-----------------------------------------------------------------------------------------------

static void dispatcher_logs_non_standard_unhandled_exception_without_callback() {
  test_dispatcher_host dispatcher;
  auto output = std::ostringstream();
  auto* original_clog_buffer = std::clog.rdbuf(output.rdbuf());

  auto p = throw_non_standard_after_await_async();
  (void)p;
  dispatcher.park();
  std::clog.rdbuf(original_clog_buffer);

  CHECK(output.str().find(
            "cardio: ignored non-standard unhandled exception") !=
        std::string::npos);
}

//-----------------------------------------------------------------------------------------------

static void empty_dispatcher_returns() {
  test_dispatcher_host dispatcher;

  dispatcher.park();
}

//-----------------------------------------------------------------------------------------------

static void manual_group_keeps_park_running_after_work_becomes_empty() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  auto callback_ran = std::atomic<bool>{false};
  auto returned = std::atomic<bool>{false};

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    callback_ran.store(true, std::memory_order_release);
  });

  auto worker = std::thread([&] {
    park_current_dispatcher(dispatcher);
    returned.store(true, std::memory_order_release);
  });

  CHECK(wait_until_true(callback_ran));
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(!returned.load(std::memory_order_acquire));

  group.shutdown();
  worker.join();

  CHECK(returned.load(std::memory_order_acquire));
}

//-----------------------------------------------------------------------------------------------

static void gentle_shutdown_drains_queued_callbacks() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  auto first_ran = false;
  auto second_ran = false;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    first_ran = true;
    group.shutdown();
  });
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    second_ran = true;
  });

  dispatcher.park();

  CHECK(first_ran);
  CHECK(second_ran);
}

//-----------------------------------------------------------------------------------------------

static void immediate_shutdown_exits_without_draining_queued_callbacks() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  auto first_ran = false;
  auto second_ran = false;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    first_ran = true;
    group.shutdown();
  });
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    second_ran = true;
  });

  dispatcher.park(cardio::shutdown_mode::unsafe_immediate);

  CHECK(first_ran);
  CHECK(!second_ran);
}

//-----------------------------------------------------------------------------------------------

static void posted_callback_runs_when_parked() {
  test_dispatcher_host dispatcher;
  auto ran = false;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] { ran = true; });
  dispatcher.park();

  CHECK(ran);
}

//-----------------------------------------------------------------------------------------------

static void empty_posted_callback_is_ignored() {
  test_dispatcher_host dispatcher;

  cardio::internal::dangerous_schedule_later__(&dispatcher, {});
  dispatcher.park();
}

//-----------------------------------------------------------------------------------------------

static void posted_callback_wakes_waiting_worker() {
  test_dispatcher_host dispatcher;
  auto first_running = std::atomic<bool>{false};
  auto allow_first = std::atomic<bool>{false};
  auto second_ran = std::atomic<bool>{false};
  auto first_thread_id = std::thread::id{};
  auto second_thread_id = std::thread::id{};

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    first_thread_id = std::this_thread::get_id();
    first_running.store(true, std::memory_order_release);
    while (!allow_first.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  auto first = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  auto second = std::thread(
      [&] { park_current_dispatcher(dispatcher); });

  CHECK(wait_until_true(first_running));

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    second_thread_id = std::this_thread::get_id();
    second_ran.store(true, std::memory_order_release);
  });

  CHECK(wait_until_true(second_ran));
  CHECK(first_thread_id != std::thread::id{});
  CHECK(second_thread_id != std::thread::id{});
  CHECK(first_thread_id != second_thread_id);

  allow_first.store(true, std::memory_order_release);
  first.join();
  second.join();
}

//-----------------------------------------------------------------------------------------------

static void group_waits_for_active_posted_callback() {
  cardio::dispatcher_group group;
  test_dispatcher_host main_dispatcher(group);
  test_dispatcher_host worker_dispatcher(group);
  auto callback_running = std::atomic<bool>{false};
  auto allow_callback = std::atomic<bool>{false};
  auto main_returned = std::atomic<bool>{false};

  cardio::internal::dangerous_schedule_later__(&worker_dispatcher, [&] {
    callback_running.store(true, std::memory_order_release);
    while (!allow_callback.load(std::memory_order_acquire)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  auto worker = std::thread(
      [&] { park_current_dispatcher(worker_dispatcher); });
  CHECK(wait_until_true(callback_running));

  auto main = std::thread([&] {
    park_current_dispatcher(main_dispatcher);
    main_returned.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(!main_returned.load(std::memory_order_acquire));

  allow_callback.store(true, std::memory_order_release);
  worker.join();
  main.join();

  CHECK(main_returned.load(std::memory_order_acquire));
}

//-----------------------------------------------------------------------------------------------

static void posted_callback_exception_reaches_unhandled_exception() {
  test_dispatcher_host dispatcher;
  auto callback_called = false;
  auto message = std::string();
  dispatcher.unhandled_exception([&](std::exception_ptr exception) {
    callback_called = true;
    try {
      std::rethrow_exception(exception);
    } catch (const std::runtime_error& caught) {
      message = caught.what();
    }
  });

  cardio::internal::dangerous_schedule_later__(&dispatcher, [] { throw std::runtime_error("posted failure"); });
  dispatcher.park();

  CHECK(callback_called);
  CHECK_EQ(message, std::string("posted failure"));
}

//-----------------------------------------------------------------------------------------------

static void first_group_dispatcher_becomes_current_dispatcher() {
  cardio::dispatcher_group group;
  test_dispatcher_host dispatcher(group);

  auto promise = cardio::resolved();

  CHECK(promise.is_ready());
}

//-----------------------------------------------------------------------------------------------

static void park_does_not_set_current_dispatcher() {
  test_dispatcher_host dispatcher;
  auto caught = std::atomic<bool>{false};

  auto worker = std::thread([&] {
    dispatcher.park();

    try {
      auto promise = cardio::resolved();
      (void)promise;
    } catch (const std::runtime_error& exception) {
      caught.store(
          exception.what() == std::string("cardio: no active dispatcher"),
          std::memory_order_release);
    }
  });
  worker.join();

  CHECK(caught.load(std::memory_order_acquire));
}

//-----------------------------------------------------------------------------------------------

static void worker_thread_can_await_again_while_parked() {
  test_dispatcher_host dispatcher;
  auto continued = false;
  auto result = 0;

  auto p = await_twice_async(continued, result);
  (void)p;
  auto worker = std::thread(
      [&] { park_current_dispatcher(dispatcher); });
  worker.join();

  CHECK(continued);
  CHECK_EQ(result, 3);
}

//-----------------------------------------------------------------------------------------------

static void workers_wait_for_active_continuation_before_empty_exit() {
  test_dispatcher_host dispatcher;
  auto running = std::atomic<bool>{false};
  auto allow_continue = std::atomic<bool>{false};
  auto returned = std::atomic<int>{0};
  auto result = 0;

  auto p = wait_between_awaits_async(running, allow_continue, result);
  (void)p;
  auto first = std::thread([&] {
    park_current_dispatcher(dispatcher);
    returned.fetch_add(1, std::memory_order_acq_rel);
  });
  auto second = std::thread([&] {
    park_current_dispatcher(dispatcher);
    returned.fetch_add(1, std::memory_order_acq_rel);
  });

  while (!running.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  CHECK_EQ(returned.load(std::memory_order_acquire), 0);

  allow_continue.store(true, std::memory_order_release);
  first.join();
  second.join();

  CHECK_EQ(returned.load(std::memory_order_acquire), 2);
  CHECK_EQ(result, 2);
}

//-----------------------------------------------------------------------------------------------

static void switch_to_resumes_on_target_dispatcher_thread() {
  cardio::dispatcher_group group;
  test_dispatcher_host main_dispatcher(group);
  test_dispatcher_host worker_dispatcher(group);
  auto worker_thread_id = std::thread::id{};
  auto resumed_thread_id = std::thread::id{};

  auto promise =
      switch_to_dispatcher_async(worker_dispatcher, resumed_thread_id);
  (void)promise;
  CHECK_EQ(resumed_thread_id, std::thread::id{});

  auto worker = std::thread([&] {
    worker_thread_id = std::this_thread::get_id();
    park_current_dispatcher(worker_dispatcher);
  });
  worker.join();

  CHECK_EQ(resumed_thread_id, worker_thread_id);
}

//-----------------------------------------------------------------------------------------------

static void switch_to_can_return_to_original_dispatcher() {
  cardio::dispatcher_group group;
  test_dispatcher_host main_dispatcher(group);
  test_dispatcher_host worker_dispatcher(group);
  const auto main_thread_id = std::this_thread::get_id();
  auto worker_thread_id = std::thread::id{};
  auto worker_resumed_thread_id = std::thread::id{};
  auto main_resumed_thread_id = std::thread::id{};

  auto promise = switch_to_roundtrip_async(
      worker_dispatcher,
      main_dispatcher,
      worker_resumed_thread_id,
      main_resumed_thread_id);
  (void)promise;
  CHECK_EQ(worker_resumed_thread_id, std::thread::id{});
  CHECK_EQ(main_resumed_thread_id, std::thread::id{});

  auto worker = std::thread([&] {
    worker_thread_id = std::this_thread::get_id();
    park_current_dispatcher(worker_dispatcher);
  });

  main_dispatcher.park();
  worker.join();

  CHECK_EQ(worker_resumed_thread_id, worker_thread_id);
  CHECK_EQ(main_resumed_thread_id, main_thread_id);
}

//-----------------------------------------------------------------------------------------------

static void switched_dispatcher_remains_current_for_following_await() {
  cardio::dispatcher_group group;
  test_dispatcher_host main_dispatcher(group);
  test_dispatcher_host worker_dispatcher(group);
  auto worker_thread_id = std::thread::id{};
  auto after_switch_thread_id = std::thread::id{};
  auto after_resolve_thread_id = std::thread::id{};

  auto promise = switch_then_resolve_async(
      worker_dispatcher, after_switch_thread_id, after_resolve_thread_id);
  (void)promise;
  CHECK_EQ(after_switch_thread_id, std::thread::id{});
  CHECK_EQ(after_resolve_thread_id, std::thread::id{});

  auto worker = std::thread([&] {
    worker_thread_id = std::this_thread::get_id();
    park_current_dispatcher(worker_dispatcher);
  });
  worker.join();

  CHECK_EQ(after_switch_thread_id, worker_thread_id);
  CHECK_EQ(after_resolve_thread_id, worker_thread_id);
}

//-----------------------------------------------------------------------------------------------

static void switch_to_same_dispatcher_does_not_suspend() {
  test_dispatcher_host dispatcher;
  auto continued = false;

  auto promise = switch_to_same_dispatcher_async(dispatcher, continued);
  (void)promise;

  CHECK(continued);
}

//-----------------------------------------------------------------------------------------------

static void promise_creation_requires_current_dispatcher() {
  auto caught = false;
  try {
    auto promise = empty_async();
    (void)promise;
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("cardio: no active dispatcher");
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void resolve_requires_current_dispatcher() {
  auto caught = false;
  try {
    auto promise = cardio::resolved();
    (void)promise;
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("cardio: no active dispatcher");
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void reject_requires_current_dispatcher() {
  auto caught = false;
  try {
    auto promise = cardio::rejected(std::runtime_error("failure"));
    (void)promise;
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("cardio: no active dispatcher");
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void switch_to_requires_current_dispatcher() {
  test_dispatcher_host target;
  auto caught = std::atomic<bool>{false};

  auto worker = std::thread([&] {
    try {
      auto awaiter = cardio::switch_to(target);
      (void)awaiter.await_suspend(std::noop_coroutine());
    } catch (const std::runtime_error& exception) {
      caught.store(
          exception.what() == std::string("cardio: no active dispatcher"),
          std::memory_order_release);
    }
  });
  worker.join();

  CHECK(caught.load(std::memory_order_acquire));
}

//-----------------------------------------------------------------------------------------------

static void switch_to_ready_check_requires_current_dispatcher() {
  test_dispatcher_host target;
  auto caught = std::atomic<bool>{false};

  auto worker = std::thread([&] {
    try {
      auto awaiter = cardio::switch_to(target);
      (void)awaiter.await_ready();
    } catch (const std::runtime_error& exception) {
      caught.store(
          exception.what() == std::string("cardio: no active dispatcher"),
          std::memory_order_release);
    }
  });
  worker.join();

  CHECK(caught.load(std::memory_order_acquire));
}

//-----------------------------------------------------------------------------------------------

static void switch_to_other_group_fails() {
  cardio::dispatcher_group first_group;
  cardio::dispatcher_group second_group;
  test_dispatcher_host other_dispatcher(second_group);
  test_dispatcher_host current_dispatcher(first_group);

  auto caught = false;
  try {
    auto promise = switch_to_other_group_async(other_dispatcher);
    (void)promise;
  } catch (const std::runtime_error& exception) {
    caught =
        exception.what() ==
        std::string("cardio: cannot switch to a dispatcher in another group");
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void main_dispatcher_waits_for_group_work_before_switch_back() {
  cardio::dispatcher_group group;
  test_dispatcher_host main_dispatcher(group);
  test_dispatcher_host worker_dispatcher(group);
  auto worker_running = std::atomic<bool>{false};
  auto allow_switch = std::atomic<bool>{false};
  auto worker_returns = std::atomic<int>{0};
  auto main_returned = std::atomic<bool>{false};
  auto main_park_thread_id = std::thread::id{};
  auto main_resumed_thread_id = std::thread::id{};

  auto promise = delayed_switch_back_async(
      worker_dispatcher,
      main_dispatcher,
      worker_running,
      allow_switch,
      main_resumed_thread_id);
  (void)promise;

  auto worker1 = std::thread([&] {
    park_current_dispatcher(worker_dispatcher);
    worker_returns.fetch_add(1, std::memory_order_acq_rel);
  });
  auto worker2 = std::thread([&] {
    park_current_dispatcher(worker_dispatcher);
    worker_returns.fetch_add(1, std::memory_order_acq_rel);
  });
  auto main_worker = std::thread([&] {
    main_park_thread_id = std::this_thread::get_id();
    park_current_dispatcher(main_dispatcher);
    main_returned.store(true, std::memory_order_release);
  });

  while (!worker_running.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  CHECK_EQ(worker_returns.load(std::memory_order_acquire), 0);
  CHECK_EQ(main_resumed_thread_id, std::thread::id{});
  CHECK(!main_returned.load(std::memory_order_acquire));

  allow_switch.store(true, std::memory_order_release);
  worker1.join();
  worker2.join();
  main_worker.join();

  CHECK_EQ(worker_returns.load(std::memory_order_acquire), 2);
  CHECK(main_returned.load(std::memory_order_acquire));
  CHECK_EQ(main_resumed_thread_id, main_park_thread_id);
}

//-----------------------------------------------------------------------------------------------

int main() {
  dispatcher_callback_receives_unhandled_exception();
  dispatcher_continues_after_callback_handles_exception();
  dispatcher_ignores_callback_exception();
  dispatcher_not_throws_unhandled_exception_without_callback();
  dispatcher_logs_non_standard_unhandled_exception_without_callback();
  empty_dispatcher_returns();
  manual_group_keeps_park_running_after_work_becomes_empty();
  gentle_shutdown_drains_queued_callbacks();
  immediate_shutdown_exits_without_draining_queued_callbacks();
  posted_callback_runs_when_parked();
  empty_posted_callback_is_ignored();
  posted_callback_wakes_waiting_worker();
  group_waits_for_active_posted_callback();
  posted_callback_exception_reaches_unhandled_exception();
  first_group_dispatcher_becomes_current_dispatcher();
  park_does_not_set_current_dispatcher();
  worker_thread_can_await_again_while_parked();
  workers_wait_for_active_continuation_before_empty_exit();
  switch_to_resumes_on_target_dispatcher_thread();
  switch_to_can_return_to_original_dispatcher();
  switched_dispatcher_remains_current_for_following_await();
  switch_to_same_dispatcher_does_not_suspend();
  promise_creation_requires_current_dispatcher();
  resolve_requires_current_dispatcher();
  reject_requires_current_dispatcher();
  switch_to_requires_current_dispatcher();
  switch_to_ready_check_requires_current_dispatcher();
  switch_to_other_group_fails();
  main_dispatcher_waits_for_group_work_before_switch_back();

  std::puts("cardio_dispatcher_test: PASS");
  return 0;
}
