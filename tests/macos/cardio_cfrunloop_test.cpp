// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "../test_helpers.h"

#ifndef CARDIO_DISPATCHER_HOST_CFRUNLOOP
#error The Core Foundation dispatcher hosts must be available in this build.
#else
#include <CoreFoundation/CFRunLoop.h>
#include <future>
#include <thread>
#include <unistd.h>

struct loop_test {
  cardio::dispatcher& host;
  int read_fd;
  std::thread::id owner = std::this_thread::get_id();
  cardio::promise_source<void> posted;
  std::promise<void> app_event;
  std::promise<void> fd_idle;
  bool waiting_for_fd = false;
  bool released_writer = false;

  loop_test(cardio::dispatcher& host, int fd) : host(host), read_fd(fd) {}
};

static void check_owner(const loop_test& test) {
  CHECK_EQ(std::this_thread::get_id(), test.owner);
  CHECK_EQ(&cardio::get_current_dispatcher(), &test.host);
}

static cardio::promise<void> observe_work(loop_test& test) {
  auto posted = test.posted.get_promise();
  co_await posted;
  check_owner(test);
  co_await cardio::promises::delay(2);
  check_owner(test);
  test.waiting_for_fd = true;
  const auto ready = co_await cardio::from_fd(test.read_fd, cardio::fd_event::read);
  CHECK((ready & cardio::fd_event::read) != cardio::fd_event::none);
  check_owner(test);
}

template <typename Host>
static void run_loop_test() {
  Host host;
  int fds[2];
  CHECK_EQ(::pipe(fds), 0);
  loop_test test(host, fds[0]);
  auto work = observe_work(test);
  auto loop = CFRunLoopGetCurrent();
  CFRunLoopSourceContext context{};
  context.info = &test;
  context.perform = [](void* info) { static_cast<loop_test*>(info)->app_event.set_value(); };
  auto source = CFRunLoopSourceCreate(nullptr, 0, &context);
  CFRunLoopAddSource(loop, source, kCFRunLoopCommonModes);

  // The pipe becomes readable only after the owner is about to sleep on it.
  CFRunLoopObserverContext observer_context{};
  observer_context.info = &test;
  auto observer = CFRunLoopObserverCreate(nullptr, kCFRunLoopBeforeWaiting, true, 0,
      [](CFRunLoopObserverRef, CFRunLoopActivity, void* info) {
        auto& state = *static_cast<loop_test*>(info);
        if (state.waiting_for_fd && !state.released_writer) {
          state.released_writer = true;
          state.fd_idle.set_value();
        }
      }, &observer_context);
  CFRunLoopAddObserver(loop, observer, kCFRunLoopCommonModes);
  auto producer = std::thread([&, app = test.app_event.get_future(), idle = test.fd_idle.get_future()] {
    app.wait();
    test.posted.resolve();
    idle.wait();
    CHECK_EQ(::write(fds[1], "x", 1), 1);
  });
  CFRunLoopSourceSignal(source);
  if constexpr (std::is_same_v<Host, cardio::dispatcher_host_cfrunloop>) {
    host.park();
  } else {
    while (!work.is_ready()) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 30, true);
  }
  producer.join();
  CHECK(work.is_ready());
  work.unsafe_result();
  CFRunLoopObserverInvalidate(observer);
  CFRelease(observer);
  CFRunLoopSourceInvalidate(source);
  CFRelease(source);
  CHECK_EQ(::close(fds[0]), 0);
  CHECK_EQ(::close(fds[1]), 0);
}

static void cancellation_and_teardown_leave_the_fd_open() {
  int fds[2];
  CHECK_EQ(::pipe(fds), 0);
  {
    cardio::dispatcher_host_cfrunloop_auto host;
    cardio::cancellation_source source;
    auto wait = cardio::from_fd(fds[0], cardio::fd_event::read, source.get_cancellation());
    CHECK(source.cancel());
    while (!wait.is_ready()) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 30, true);
    bool canceled = false;
    try { (void)wait.unsafe_result(); } catch (const cardio::canceled_exception&) { canceled = true; }
    CHECK(canceled);
    auto abandoned = cardio::from_fd(fds[0], cardio::fd_event::read);
    CHECK_EQ(::write(fds[1], "a", 1), 1);
  }
  (void)CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, false);
  char byte = 0;
  CHECK_EQ(::read(fds[0], &byte, 1), 1);
  CHECK_EQ(byte, 'a');
  CHECK_EQ(::close(fds[0]), 0);
  CHECK_EQ(::close(fds[1]), 0);
}

int main() {
  // Synchronization uses run loop events. Bound a lost wakeup on CI.
  ::alarm(60);
  run_loop_test<cardio::dispatcher_host_cfrunloop>();
  std::puts("cardio_cfrunloop_manual_test: PASS");
  run_loop_test<cardio::dispatcher_host_cfrunloop_auto>();
  cancellation_and_teardown_leave_the_fd_open();
  std::puts("cardio_cfrunloop_auto_test: PASS");
  ::alarm(0);
  return 0;
}
#endif
