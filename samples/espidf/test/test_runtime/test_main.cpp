#include "cardio.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "unity.h"
#ifndef CARDIO_TEST_CORE_ONLY
#include "esp_netif.h"
#include "esp_vfs_eventfd.h"
#include "loopback.h"
void ip_event_starts_tcp();
void drop_wait_during_event_delivery();
void one_shot_events();
#endif
#include <array>
#include <atomic>
#include <cstdio>

static cardio::promise<bool> timer_on_owner(cardio::dispatcher_host& dispatcher) {
  co_await cardio::promises::delay(5);
  co_return dispatcher.is_current_thread_owner();
}

static void task_bound_dispatcher() {
  cardio::dispatcher_host dispatcher(cardio::dispatcher_thread_policy::current_thread);
  auto work = timer_on_owner(dispatcher);
  dispatcher.park();
  TEST_ASSERT_TRUE(work.unsafe_result());
}

static constexpr unsigned worker_count = CONFIG_FREERTOS_NUMBER_OF_CORES;
static constexpr EventBits_t all_workers = (1U << worker_count) - 1;

struct workers {
  cardio::dispatcher_group group{cardio::exit_condition::exit_by_manual};
  cardio::dispatcher_host dispatcher{group};
  EventGroupHandle_t events = xEventGroupCreate();
  TaskHandle_t caller = xTaskGetCurrentTaskHandle();
  std::array<TaskHandle_t, worker_count> handles{};
  std::array<int, worker_count> cores{};
  std::array<std::atomic<unsigned>, worker_count> executions{};
};

static cardio::promise<void> observe(workers& state, unsigned index, cardio::promise<void> signal) {
  co_await signal;
  const int core = esp_cpu_get_core_id();
  configASSERT(xTaskGetCurrentTaskHandle() == state.handles[core]);
  configASSERT(&cardio::get_current_dispatcher() == &state.dispatcher);
  state.cores[index] = core;
  ++state.executions[index];
  // Neither worker can finish the rendezvous by running both continuations.
  xEventGroupSync(state.events, 1U << index, all_workers, portMAX_DELAY);
  xEventGroupSetBits(state.events, BIT4 << index);
}

static void park_worker(void* arg) {
  auto& state = *static_cast<workers*>(arg);
  const auto core = esp_cpu_get_core_id();
  cardio::set_current_dispatcher(&state.dispatcher);
  state.dispatcher.park();
  cardio::set_current_dispatcher(nullptr);
  std::printf("worker %d stack free: %u bytes\n", core,
              static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  xTaskNotifyGive(state.caller);
  vTaskDelete(nullptr);
}

static void shared_dispatcher() {
  workers state;
  TEST_ASSERT_NOT_NULL(state.events);
  std::array<cardio::promise_source<void>, worker_count> sources;
  std::vector<cardio::promise<void>> work;
  for (unsigned i = 0; i < worker_count; ++i) {
    work.push_back(observe(state, i, sources[i].get_promise()));
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(park_worker, "cardio", 6144,
        &state, 3, &state.handles[i], i));
  }
  for (auto& source : sources) {
    source.resolve();
  }
  xEventGroupWaitBits(state.events, all_workers << 4, pdFALSE, pdTRUE, portMAX_DELAY);
  state.group.shutdown();
  for (unsigned i = 0; i < worker_count; ++i) {
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
  }
  for (unsigned i = 0; i < worker_count; ++i) {
    work[i].unsafe_result();
    TEST_ASSERT_EQUAL_UINT(1, state.executions[i].load());
    if (i > 0) {
      TEST_ASSERT_NOT_EQUAL(state.cores[0], state.cores[i]);
    }
  }
  vEventGroupDelete(state.events);
}

static cardio::promise<void> stop_after_timer(cardio::dispatcher_group& group) {
  co_await cardio::promises::delay(5);
  group.shutdown();
}

static void timer_added_to_idle_dispatcher() {
  workers state;
  // This higher-priority worker runs until park() blocks on the caller's core.
  TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(park_worker, "cardio-idle", 6144,
      &state, uxTaskPriorityGet(nullptr) + 1, nullptr, esp_cpu_get_core_id()));
  auto work = stop_after_timer(state.group);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  TEST_ASSERT_TRUE(work.is_ready());
  vEventGroupDelete(state.events);
}

#ifndef CARDIO_TEST_CORE_ONLY
static void earlier_timer_interrupts_select() {
  workers state;
  auto later = cardio::promises::delay(60000);
  TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(park_worker, "cardio-select", 6144,
      &state, uxTaskPriorityGet(nullptr) + 1, nullptr, esp_cpu_get_core_id()));
  auto earlier = stop_after_timer(state.group);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  TEST_ASSERT_TRUE(earlier.is_ready());
  TEST_ASSERT_FALSE(later.is_ready());
  vEventGroupDelete(state.events);
}

static void tcp_and_eventfd() {
  workers state;
  const int fd = eventfd(0, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, fd);
  auto ready = cardio::from_fd(fd, cardio::fd_event::read);
  auto exchange = loopback_exchange();
  auto wait_for = [](auto& io) -> cardio::promise<void> { (void)co_await io; };
  std::vector<cardio::promise<void>> observed;
  observed.push_back(observe(state, 0, wait_for(exchange)));
  if constexpr (worker_count > 1) {
    observed.push_back(observe(state, 1, wait_for(ready)));
  }
  for (unsigned i = 0; i < worker_count; ++i) {
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(park_worker, "cardio-io", 6144,
        &state, 3, &state.handles[i], i));
  }
  const uint64_t signal = 7;
  TEST_ASSERT_EQUAL(sizeof(signal), write(fd, &signal, sizeof(signal)));
  // On two cores the socket and eventfd continuations must meet on distinct workers.
  auto finish = [&]() -> cardio::promise<void> {
    if constexpr (worker_count == 1) co_await ready;
    for (auto& work : observed) co_await work;
    state.group.shutdown();
  };
  auto done = finish();
  for (unsigned i = 0; i < worker_count; ++i) {
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
  }
  TEST_ASSERT_TRUE(done.is_ready());
  if constexpr (worker_count > 1) TEST_ASSERT_NOT_EQUAL(state.cores[0], state.cores[1]);
  TEST_ASSERT_TRUE(exchange.unsafe_result());
  TEST_ASSERT_TRUE((ready.unsafe_result() & cardio::fd_event::read) == cardio::fd_event::read);
  uint64_t value = 0;
  TEST_ASSERT_EQUAL(sizeof(value), read(fd, &value, sizeof(value)));
  TEST_ASSERT_EQUAL_UINT(7, value);
  close(fd);
  vEventGroupDelete(state.events);
}

#if CARDIO_HAS_EXCEPTIONS
static void cancel_and_reuse_fd() {
  cardio::dispatcher_host dispatcher;
  int fd = eventfd(0, 0);
  TEST_ASSERT_GREATER_OR_EQUAL(0, fd);
  cardio::cancellation_source cancel;
  auto old = cardio::from_fd(fd, cardio::fd_event::read, cancel.get_cancellation());
  cancel.cancel();
  dispatcher.park();
  try {
    (void)old.unsafe_result();
    TEST_FAIL_MESSAGE("Expected cancellation");
  } catch (const cardio::canceled_exception&) {}
  TEST_ASSERT_EQUAL(0, close(fd));
  const int reused = eventfd(0, 0);
  TEST_ASSERT_EQUAL(fd, reused);
  auto fresh = cardio::from_fd(reused, cardio::fd_event::read);
  const uint64_t one = 1;
  TEST_ASSERT_EQUAL(sizeof(one), write(reused, &one, sizeof(one)));
  dispatcher.park();
  TEST_ASSERT_TRUE(fresh.unsafe_result() == cardio::fd_event::read);
  TEST_ASSERT_EQUAL(0, close(reused));
  auto out_of_range = cardio::from_fd(FD_SETSIZE, cardio::fd_event::read);
  try {
    (void)out_of_range.unsafe_result();
    TEST_FAIL_MESSAGE("Expected FD_SETSIZE to be rejected");
  } catch (const std::invalid_argument&) {}
}

static void cancellation_races_readiness() {
  workers state;
  sample_socket event(eventfd(0, 0));
  cardio::cancellation_source cancel;
  unsigned completions = 0;
  auto wait = [&]() -> cardio::promise<void> {
    try {
      const auto ready = co_await cardio::from_fd(event.fd, cardio::fd_event::read,
                                                   cancel.get_cancellation());
      configASSERT(ready == cardio::fd_event::read);
    } catch (const cardio::canceled_exception&) {}
    ++completions;
    state.group.shutdown();
  };
  auto result = wait();
  for (unsigned i = 0; i < worker_count; ++i) {
    TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore(park_worker, "cardio-race", 6144,
        &state, 3, nullptr, i));
  }
  const uint64_t one = 1;
  TEST_ASSERT_EQUAL(sizeof(one), write(event.fd, &one, sizeof(one)));
  cancel.cancel();
  for (unsigned i = 0; i < worker_count; ++i) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
  TEST_ASSERT_TRUE(result.is_ready());
  TEST_ASSERT_EQUAL_UINT(1, completions);
  vEventGroupDelete(state.events);
}

static void cancellation_races_timer_expiry() {
  for (unsigned round = 0; round < 12; ++round) {
    workers state;
    cardio::cancellation_source cancel;
    unsigned completions = 0;
    auto observe_timer = [&]() -> cardio::promise<void> {
      try { co_await cardio::promises::delay(1, cancel.get_cancellation()); }
      catch (const cardio::canceled_exception&) {}
      ++completions;
      state.group.shutdown();
    };
    auto work = observe_timer();
    for (unsigned core = 0; core < worker_count; ++core) {
      TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore([](void* arg) {
        auto& state = *static_cast<workers*>(arg);
        xEventGroupWaitBits(state.events, BIT12, pdFALSE, pdTRUE, portMAX_DELAY);
        park_worker(arg);
      }, "timer-race", 6144, &state, 3, nullptr, core));
    }
    // Let the deadline expire before two park workers can claim the timer and cancellation.
    cancel.cancel();
    vTaskDelay(pdMS_TO_TICKS(1) + 2);
    xEventGroupSetBits(state.events, BIT12);
    for (unsigned i = 0; i < worker_count; ++i) ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
    TEST_ASSERT_TRUE(work.is_ready());
    TEST_ASSERT_EQUAL_UINT(1, completions);
    vEventGroupDelete(state.events);
  }
}
#endif
#endif

extern "C" void app_main() {
#ifndef CARDIO_TEST_CORE_ONLY
  ESP_ERROR_CHECK(esp_netif_init());
  const esp_vfs_eventfd_config_t config{4};
  ESP_ERROR_CHECK(esp_vfs_eventfd_register(&config));
#endif
  UNITY_BEGIN();
  RUN_TEST(task_bound_dispatcher);
  RUN_TEST(shared_dispatcher);
  RUN_TEST(timer_added_to_idle_dispatcher);
#ifndef CARDIO_TEST_CORE_ONLY
  RUN_TEST(tcp_and_eventfd);
  RUN_TEST(earlier_timer_interrupts_select);
  RUN_TEST(ip_event_starts_tcp);
  RUN_TEST(drop_wait_during_event_delivery);
  RUN_TEST(one_shot_events);
#if CARDIO_HAS_EXCEPTIONS
  RUN_TEST(cancel_and_reuse_fd);
  RUN_TEST(cancellation_races_readiness);
  RUN_TEST(cancellation_races_timer_expiry);
#endif
#endif
  std::printf("app_main stack free: %u bytes\n",
              static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  for (unsigned core = 0; core < worker_count; ++core) {
    std::printf("idle %u stack free: %u bytes\n", core,
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandleForCore(core))));
  }
  UNITY_END();
}
