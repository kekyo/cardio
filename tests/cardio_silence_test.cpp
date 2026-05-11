// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#define CARDIO_SILENCE_UNHANDLED_EXCEPTION

#include "cardio.h"
#include "test_helpers.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

//-----------------------------------------------------------------------------------------------

static cardio::promise<int> throw_after_await_async(const char* message) {
  auto r = co_await cardio::resolved(1);
  (void)r;
  throw std::runtime_error(message);
  co_return 0;
}

//-----------------------------------------------------------------------------------------------

static void silence_define_suppresses_ignored_unhandled_exception_log() {
  test_dispatcher_host dispatcher;
  auto output = std::ostringstream();
  auto* original_clog_buffer = std::clog.rdbuf(output.rdbuf());

  auto p = throw_after_await_async("silent root failure");
  auto caught = false;
  try {
    dispatcher.park();
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("silent root failure");
  }
  std::clog.rdbuf(original_clog_buffer);

  CHECK(!caught);
  CHECK(p.is_ready());
  CHECK(output.str().empty());
}

//-----------------------------------------------------------------------------------------------

int main() {
  silence_define_suppresses_ignored_unhandled_exception_log();

  std::puts("cardio_silence_test: PASS");
  return 0;
}
