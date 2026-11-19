#include "cardio.h"
#include <cstdio>

static cardio::promise<void> run_sample() {
  co_await cardio::promises::delay(10);
  std::puts("cardio timer completed");
}

extern "C" void app_main() {
  cardio::dispatcher_host dispatcher;
  auto work = run_sample();
  dispatcher.park();
  work.unsafe_result();
}
