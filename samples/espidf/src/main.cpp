#include "cardio.h"
#include <cstdio>
#if CARDIO_HAS_POSIX_FD
#include "ip_sample.h"
#endif

#if !CARDIO_HAS_POSIX_FD
static cardio::promise<void> run_sample() {
  co_await cardio::promises::delay(10);
  std::puts("cardio timer completed");
}
#endif

extern "C" void app_main() {
#if CARDIO_HAS_POSIX_FD
  ESP_ERROR_CHECK(esp_netif_init());
  const esp_vfs_eventfd_config_t config{1};
  ESP_ERROR_CHECK(esp_vfs_eventfd_register(&config));
  ESP_ERROR_CHECK(run_ip_sample() ? ESP_OK : ESP_FAIL);
  std::puts("IP event, TCP loopback and shutdown completed");
#else
  cardio::dispatcher_host dispatcher;
  auto work = run_sample();
  dispatcher.park();
  work.unsafe_result();
#endif
}
