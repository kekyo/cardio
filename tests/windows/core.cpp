// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

static cardio::promise<void> run_async(int& completed) {
  const auto owner = ::GetCurrentThreadId();
  auto task = cardio::promises::start_new(
      [owner, input = std::make_unique<int>(42)] {
    CHECK(::GetCurrentThreadId() != owner);
    return *input;
  });
  auto value = co_await task;
  CHECK_EQ(value, 42);
  co_await cardio::promises::delay(1);
  CHECK_EQ(::GetCurrentThreadId(), owner);
  ++completed;
}

int main() {
  cardio::dispatcher_host dispatcher;
  auto completed = 0;
  auto tasks = std::vector<cardio::promise<void>>{};
  for (auto index = 0; index != 16; ++index) {
    tasks.push_back(run_async(completed));
  }
  dispatcher.park();
  CHECK_EQ(completed, 16);
  std::puts("Windows core: PASS");
}
