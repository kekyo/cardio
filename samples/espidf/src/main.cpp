#include "cardio.h"
#include <cstdio>
#if CARDIO_HAS_POSIX_FD
#include "esp_netif.h"
#include "loopback.h"
#endif

static cardio::promise<void> run_sample() {
  co_await cardio::promises::delay(10);
  std::puts("cardio timer completed");
#if CARDIO_HAS_POSIX_FD
  ESP_ERROR_CHECK((co_await loopback_exchange()) ? ESP_OK : ESP_FAIL);
  std::puts("TCP loopback completed");
#endif
}

extern "C" void app_main() {
#if CARDIO_HAS_POSIX_FD
  ESP_ERROR_CHECK(esp_netif_init());
  const esp_vfs_eventfd_config_t config{1};
  ESP_ERROR_CHECK(esp_vfs_eventfd_register(&config));
#endif
  cardio::dispatcher_host dispatcher;
  auto work = run_sample();
  dispatcher.park();
  work.unsafe_result();
}
