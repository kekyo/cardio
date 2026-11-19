# cardio

A header-only library for asynchronous processing in C++20.

![cardio](./images/cardio-120.png)

[![Project Status: Active – The project has reached a stable, usable state and is being actively developed.](https://www.repostatus.org/badges/latest/active.svg)](https://www.repostatus.org/#active)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

---

[(For Japanese language/日本語はこちら)](./README_ja.md)

> Please note that this English version of the document was machine-translated and then partially edited, so it may contain inaccuracies.
> We welcome pull requests to correct any errors in the text.

## What Is This?

Have you ever wanted to easily use the new extension syntax added in C++20—such as `co_await`—for asynchronous processing?
While compilers support this syntax, it is currently rarely provided in the standard library,
so you need to implement it from the ground up.

cardio is a library designed to assist with this, making it easy to write generic asynchronous code.
Since it is a header-only library, you can easily integrate it into your environment.

It is primarily designed for asynchronous API designers and the developers who use them.

C++20 asynchronous programming supports syntax that can be written simply, much like TypeScript/JavaScript.
With this library, asynchronous processing can be written as simply as follows:

```cpp
#include <stdio.h>
#include "cardio.h"

// Async function (returns a promise)
static cardio::promise<int> add_async(int a, int b) {
  co_return a + b;  // Can 'co_return'
}

// Async function (returns a promise)
static cardio::promise<void> main_async() {
  auto r = co_await add_async(1, 2);  // Can 'co_await'
  printf("1 + 2 = %d\n", r);
}

int main() {
  // Initialize the cardio dispatcher.
  // The dispatcher uses this thread (the main thread) to execute promise continuations.
  cardio::dispatcher_host d;

  // Keep the root promise alive until park() finishes.
  auto p = main_async();
  (void)p;

  // Park this thread and execute asynchronous continuations.
  d.park();

  return 0;
}
```

### Features

- It is a header-only library. It can be easily integrated.
- The basic behavior follows asynchronous processing in TypeScript/JavaScript.
  Therefore, advanced asynchronous processing can be written as concisely as in TypeScript.
- Supports cancellation notifications.
- Can await for POSIX file descriptors and implement readiness-based asynchronous processing.
- Can await for Win32 handles and externally submitted OVERLAPPED I/O.
- Can use Linux io_uring to implement true asynchronous I/O.
- Supports integration with Android native and Java UI Loopers.
- Supports integration with the GLib 2 (GTK) scheduler.
- Supports continuation execution across multiple threads.
- Supports asynchronous primitives corresponding to synchronous primitives (mutex, semaphore, conditional variables and reader-writer-lock).

### Environment

- C++20 compiler (GNU g++, and probably clang)
- POSIX environment (Core functionality will work in non-POSIX environments as long as the major C++20 libraries are available)
- Windows (optional, Win32 handle and OVERLAPPED wait support)
- Linux (optional, io_uring support)
- Android API 24 or newer (optional, Android Looper integration)
- ESP-IDF 6.0 (primary target ESP32-S3)
- GLib 2/GTK (optional)

---

## Installation

Copy [`cardio.h`](./include/cardio.h) into your project and include it. That is all.

---

## Documentation

### User Guide

- [Basic Usage](./docs/en/getting-started.md)
- [Working With Promises and Their Lifetimes](./docs/en/promises.md)
- [Exceptions](./docs/en/exceptions.md)
- [Cancellation](./docs/en/cancellation.md)
- [Supplemental Functions and Async Primitives](./docs/en/helpers-and-primitives.md)

### Platform Guides

- [Waiting for POSIX File Descriptors](./docs/en/posix-fd.md)
- [Implementing Asynchronous I/O With Linux io_uring](./docs/en/linux-io-uring.md)
- [Win32 Handle Waits and I/O Completion Ports](./docs/en/win32.md)
- [Integration With Android Looper](./docs/en/android-looper.md)
- [GLib/GTK Integration and GIO File I/O](./docs/en/glib-gio.md)
- [Using FreeRTOS tasks on ESP-IDF](./docs/en/esp-idf.md)

### Advanced Topics

- [Using Dispatchers Across Threads](./docs/en/dispatchers.md)
- [Reading Promise Results Without co_await](./docs/en/promise-results.md)
- [Shared Runtime and Exception-Disabled Builds](./docs/en/build-options.md)

---

## Note

This is the successor project to [libbounce](https://github.com/kekyo/libbounce/).
(Going even further back, there is also [future-promise](https://github.com/kekyo/future-promise/))

As libbounce had become increasingly complex,
we reversed the roles of C and C++ and rebuilt it from the ground up using C++ standards.

In C++, inline expansion—including that of `std::function` takes place,
which allows us to avoid the issue where function pointer optimization was suppressed in libbounce.

## License

Under MIT.
