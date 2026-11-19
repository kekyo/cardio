#ifndef CARDIO_TEST_CORE_ONLY
#include "ip_sample.h"
#include "unity.h"

ESP_EVENT_DEFINE_BASE(CARDIO_TEST_EVENT);

void ip_event_starts_tcp() {
  TEST_ASSERT_TRUE(run_ip_sample());
}

void drop_wait_during_event_delivery() {
  cardio::dispatcher_host dispatcher;
  esp_event_loop_args_t args{};
  args.queue_size = 2;
  args.task_name = "event-delivery";
  args.task_priority = 3;
  args.task_stack_size = 4096;
  args.task_core_id = tskNO_AFFINITY;
  esp_event_loop_handle_t loop{};
  ESP_ERROR_CHECK(esp_event_loop_create(&args, &loop));
  const auto gate = xEventGroupCreate();
  esp_event_handler_instance_t blocker{};
  ESP_ERROR_CHECK(esp_event_handler_instance_register_with(loop, CARDIO_TEST_EVENT, 1,
      [](void* arg, esp_event_base_t, int32_t, void*) {
        auto gate = static_cast<EventGroupHandle_t>(arg);
        xEventGroupSetBits(gate, BIT0);
        xEventGroupWaitBits(gate, BIT1, pdFALSE, pdTRUE, portMAX_DELAY);
      }, gate, &blocker));
  auto wait = std::optional{cardio::from_esp_event<void>(CARDIO_TEST_EVENT, 1, loop)};
  ESP_ERROR_CHECK(esp_event_post_to(loop, CARDIO_TEST_EVENT, 1, nullptr, 0, 0));
  xEventGroupWaitBits(gate, BIT0, pdFALSE, pdTRUE, portMAX_DELAY);
  struct drop_request { decltype(wait)& pending; TaskHandle_t caller; };
  drop_request request{wait, xTaskGetCurrentTaskHandle()};
  // Run the dropper on this core before releasing the event-loop callback.
  TEST_ASSERT_EQUAL(pdPASS, xTaskCreatePinnedToCore([](void* arg) {
    auto& request = *static_cast<drop_request*>(arg);
    request.pending.reset();
    xTaskNotifyGive(request.caller);
    vTaskDelete(nullptr);
  }, "drop-wait", 4096, &request, uxTaskPriorityGet(nullptr) + 1, nullptr, esp_cpu_get_core_id()));
  xEventGroupSetBits(gate, BIT1);
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  dispatcher.park();
  TEST_ASSERT_FALSE(wait.has_value());
  ESP_ERROR_CHECK(esp_event_handler_instance_unregister_with(loop, CARDIO_TEST_EVENT, 1, blocker));
  ESP_ERROR_CHECK(esp_event_loop_delete(loop));
  vEventGroupDelete(gate);
}

void one_shot_events() {
  esp_event_loop_args_t args{};
  args.queue_size = 4;
  esp_event_loop_handle_t loop{};
  ESP_ERROR_CHECK(esp_event_loop_create(&args, &loop));
  cardio::dispatcher_host dispatcher;
  auto first = cardio::from_esp_event<int>(CARDIO_TEST_EVENT, 1, loop);
  int value = 42;
  ESP_ERROR_CHECK(esp_event_post_to(loop, CARDIO_TEST_EVENT, 1, &value, sizeof(value), 0));
  value = 99;
  ESP_ERROR_CHECK(esp_event_post_to(loop, CARDIO_TEST_EVENT, 1, &value, sizeof(value), 0));
  ESP_ERROR_CHECK(esp_event_loop_run(loop, 0));
  ESP_ERROR_CHECK(esp_event_loop_run(loop, 0));
  dispatcher.park();
  TEST_ASSERT_EQUAL(ESP_OK, first.unsafe_result().error);
  TEST_ASSERT_EQUAL(42, first.unsafe_result().value);
  {
    auto abandoned = cardio::from_esp_event<void>(CARDIO_TEST_EVENT, 2, loop);
    ESP_ERROR_CHECK(esp_event_post_to(loop, CARDIO_TEST_EVENT, 2, nullptr, 0, 0));
    ESP_ERROR_CHECK(esp_event_loop_run(loop, 0));
  }
  { auto pending = cardio::from_esp_event<void>(CARDIO_TEST_EVENT, 2, loop); }
  dispatcher.park();
  ESP_ERROR_CHECK(esp_event_loop_delete(loop));

  // The application has not created the default event loop.
  auto failed = cardio::from_esp_event<int>(CARDIO_TEST_EVENT, 1);
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, failed.unsafe_result().error);
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  cardio::cancellation_source cancel;
  auto canceled = cardio::from_esp_event<void>(CARDIO_TEST_EVENT, 1, nullptr, cancel.get_cancellation());
  cancel.cancel();
  dispatcher.park();
#if CARDIO_HAS_EXCEPTIONS
  try { (void)canceled.unsafe_result(); TEST_FAIL_MESSAGE("Expected cancellation"); }
  catch (const cardio::canceled_exception&) {}
#else
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, canceled.unsafe_result().error);
#endif
  auto signal = cardio::from_esp_event<void>(CARDIO_TEST_EVENT, 2);
  ESP_ERROR_CHECK(esp_event_post(CARDIO_TEST_EVENT, 2, nullptr, 0, portMAX_DELAY));
  dispatcher.park();
  TEST_ASSERT_EQUAL(ESP_OK, signal.unsafe_result().error);
  ESP_ERROR_CHECK(esp_event_loop_delete_default());
}
#endif
