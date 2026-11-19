#include "cardio.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "unity.h"
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

extern "C" void app_main() {
  UNITY_BEGIN();
  RUN_TEST(task_bound_dispatcher);
  RUN_TEST(shared_dispatcher);
  RUN_TEST(timer_added_to_idle_dispatcher);
  std::printf("app_main stack free: %u bytes\n",
              static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  for (unsigned core = 0; core < worker_count; ++core) {
    std::printf("idle %u stack free: %u bytes\n", core,
        static_cast<unsigned>(uxTaskGetStackHighWaterMark(xTaskGetIdleTaskHandleForCore(core))));
  }
  UNITY_END();
}
