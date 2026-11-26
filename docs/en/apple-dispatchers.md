# Dispatchers and Event Loops on macOS and iOS

Choose a host based on which thread runs your continuations and who drives its event loop. All four hosts support waiting for file descriptor (fd) readiness with `from_fd()`. Keep the fd open while waiting and close it in the application when finished.

| Use case | Host | How work runs |
| --- | --- | --- |
| CLI or worker thread | `dispatcher_host` | Call `park()`. The standard POSIX implementation waits with `poll()`. |
| Keep multiple fds registered with the kernel | `dispatcher_host_kqueue` | Call `park()`. The same wait handles timers and cross-thread notifications. |
| The application drives a Core Foundation run loop | `dispatcher_host_cfrunloop` | Call `park()` on the owner thread to run both application events and cardio continuations. |
| AppKit or UIKit already drives the loop | `dispatcher_host_cfrunloop_auto` | Construct the host on the loop's owner thread. Continuations resume there automatically. |

Construct and destroy both Core Foundation hosts on the run loop's owner thread. Keep the automatic host alive until its continuations finish. To use Core Foundation, define `CARDIO_WITH_CFRUNLOOP=1` in each application translation unit and link the framework. Sources and timers participate in the loop's common modes. [Apple's Run Loops guide](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/RunLoopManagement/RunLoopManagement.html)

```sh
xcrun --sdk macosx clang++ -std=c++20 -Iinclude -DCARDIO_WITH_CFRUNLOOP=1 \
  app.cpp -framework CoreFoundation -o app
```

`cardio.h` defines a macro for each available host family. Applications can select hosts across platforms at compile time.

```cpp
#include "cardio.h"

#ifdef CARDIO_DISPATCHER_HOST_KQUEUE
using worker_host = cardio::dispatcher_host_kqueue;
#else
using worker_host = cardio::dispatcher_host;
#endif

#ifdef CARDIO_DISPATCHER_HOST_CFRUNLOOP
using native_loop_host = cardio::dispatcher_host_cfrunloop;
using ui_host = cardio::dispatcher_host_cfrunloop_auto;
#elif defined(__ANDROID__)
using native_loop_host = cardio::dispatcher_host_android;
using ui_host = cardio::dispatcher_host_android_auto;
#elif CARDIO_HAS_WIN32_HANDLE
using native_loop_host = cardio::dispatcher_host_win32;
using ui_host = cardio::dispatcher_host_win32_auto;
#endif
```

## Test environments

macOS support is experimental. The minimum deployment targets below are build settings. The tests run on GitHub Actions macOS runners with Xcode 16.4. [CI configuration](../../.github/workflows/ci.yml)

| CPU | Minimum deployment target | Tested macOS |
| --- | --- | --- |
| Intel x86_64 | 10.13 | 15.7.9 |
| Apple Silicon arm64 | 11.0 | 15.8.2 |

iOS support is also experimental. All hosts compile and link against both iOS 18.5 device and Simulator SDKs with a minimum deployment target of 15.0. On an iPhone 16 Simulator running iOS 18.5, cross-thread notifications, timers and fd continuations resumed on the UIKit main thread. [iOS test results](https://github.com/kekyo/cardio/actions/runs/45802233648/job/139848387233)

---

[All guides](../../README.md#documentation) · [日本語](../ja/apple-dispatchers.md)
