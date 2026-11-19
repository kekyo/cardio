#pragma once

#include "loopback.h"
#include "esp_event.h"
#include "esp_netif.h"

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
  auto exchange = exchange_after_ip(loop);
  ip_event_got_ip_t ip{};
  ip.ip_info.ip.addr = htonl(INADDR_LOOPBACK);
  ESP_ERROR_CHECK(esp_event_post_to(loop, IP_EVENT, IP_EVENT_STA_GOT_IP, &ip, sizeof(ip), portMAX_DELAY));

  struct stop_request { cardio::dispatcher_group& group; TaskHandle_t caller; };
  stop_request request{group, xTaskGetCurrentTaskHandle()};
  TaskHandle_t stopper{};
  const auto created = xTaskCreate([](void* arg) {
    auto& request = *static_cast<stop_request*>(arg);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    request.group.shutdown();
    xTaskNotifyGive(request.caller);
    vTaskDelete(nullptr);
  }, "stop-cardio", 4096, &request, 3, &stopper);
  ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
  auto finish = [&]() -> cardio::promise<void> {
    co_await exchange;
    xTaskNotifyGive(stopper);
  };
  auto finished = finish();
  dispatcher.park();
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  ESP_ERROR_CHECK(esp_event_loop_delete(loop));
  finished.unsafe_result();
  return exchange.unsafe_result();
}
