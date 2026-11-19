#pragma once

#include "loopback.h"
#include "esp_cpu.h"
#include "esp_event.h"
#include "esp_netif.h"
#include <cstdio>

// An application receiving real station events uses the default loop (nullptr).
static inline cardio::promise<bool> exchange_after_ip(esp_event_loop_handle_t loop) {
  auto ip = co_await cardio::from_esp_event<ip_event_got_ip_t>(IP_EVENT, IP_EVENT_STA_GOT_IP, loop);
  if (ip.error != ESP_OK || !ip.value.ip_info.ip.addr) co_return false;
  co_return co_await loopback_exchange();
}

// Drive the same IP notification on QEMU without an access point.
static inline bool run_ip_sample() {
  esp_event_loop_args_t args{};
  args.queue_size = 4;
  args.task_name = "ip-events";
  args.task_priority = 4;
  args.task_stack_size = 4096;
  args.task_core_id = tskNO_AFFINITY;
  esp_event_loop_handle_t loop = nullptr;
  ESP_ERROR_CHECK(esp_event_loop_create(&args, &loop));
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  cardio::dispatcher_host dispatcher(group);
  struct worker_context { cardio::dispatcher_host& dispatcher; TaskHandle_t supervisor; };
  worker_context context{dispatcher, xTaskGetCurrentTaskHandle()};
  const auto park_worker = [](void* arg) {
    auto& context = *static_cast<worker_context*>(arg);
    cardio::set_current_dispatcher(&context.dispatcher);
    context.dispatcher.park();
    cardio::set_current_dispatcher(nullptr);
    std::printf("Dispatcher worker on core %d stopped\n", esp_cpu_get_core_id());
    xTaskNotifyGive(context.supervisor);
    vTaskDelete(nullptr);
  };
  for (int core = 0; core < CONFIG_FREERTOS_NUMBER_OF_CORES; ++core) {
    const auto created = xTaskCreatePinnedToCore(park_worker, "cardio", 6144, &context, 3, nullptr, core);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
  }

  auto finish = [&]() -> cardio::promise<bool> {
    const bool exchanged = co_await exchange_after_ip(loop);
    xTaskNotifyGive(context.supervisor);
    co_return exchanged;
  };
  auto finished = finish();
  ip_event_got_ip_t ip{};
  ip.ip_info.ip.addr = htonl(INADDR_LOOPBACK);
  ESP_ERROR_CHECK(esp_event_post_to(loop, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip, sizeof(ip), portMAX_DELAY));

  // app_main requests shutdown after the exchange, then joins every park worker.
  ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
  group.shutdown();
  for (int core = 0; core < CONFIG_FREERTOS_NUMBER_OF_CORES; ++core) {
    ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
  }
  ESP_ERROR_CHECK(esp_event_loop_delete(loop));
  return finished.unsafe_result();
}
