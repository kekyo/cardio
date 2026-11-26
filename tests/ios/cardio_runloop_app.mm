// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#import <UIKit/UIKit.h>
#include "cardio.h"
#include "../test_helpers.h"

static cardio::promise<void> exercise_ui_loop() {
  int fds[2];
  CHECK_EQ(::pipe(fds), 0);
  cardio::promise_source<void> source;
  auto posted = source.get_promise();
  auto read = cardio::from_fd(fds[0], cardio::fd_event::read);
  auto producer = std::thread([&] {
    source.resolve();
    CHECK_EQ(::write(fds[1], "i", 1), 1);
  });
  co_await posted;
  CHECK([NSThread isMainThread]);
  co_await cardio::promises::delay(2);
  CHECK([NSThread isMainThread]);
  const auto events = co_await read;
  CHECK((events & cardio::fd_event::read) != cardio::fd_event::none);
  CHECK([NSThread isMainThread]);
  producer.join();
  char byte = 0;
  CHECK_EQ(::read(fds[0], &byte, 1), 1);
  CHECK_EQ(byte, 'i');
  CHECK_EQ(::close(fds[0]), 0);
  CHECK_EQ(::close(fds[1]), 0);
  std::puts("cardio_ios_cfrunloop_test: PASS");
  std::fflush(stdout);
  std::_Exit(0);
}

@interface CardioAppDelegate : UIResponder <UIApplicationDelegate>
@end
@implementation CardioAppDelegate
@end

@interface CardioSceneDelegate : UIResponder <UIWindowSceneDelegate>
@end
@implementation CardioSceneDelegate
- (void)scene:(UIScene*)scene willConnectToSession:(UISceneSession*)session
      options:(UISceneConnectionOptions*)options {
  (void)scene;
  (void)session;
  (void)options;
  static cardio::dispatcher_host_cfrunloop_auto host;
  static auto root = exercise_ui_loop();
  (void)root;
}
@end

int main(int argc, char** argv) {
  @autoreleasepool {
    ::alarm(60);
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([CardioAppDelegate class]));
  }
}
