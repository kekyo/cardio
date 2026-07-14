# cardio

A header-only library for asynchronous processing in C++20.

![cardio](./images/cardio-120.png)

[![Project Status: WIP – Initial development is in progress, but there has not yet been a stable, usable release suitable for the public.](https://www.repostatus.org/badges/latest/wip.svg)](https://www.repostatus.org/#wip)
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
- Supports integration with the GLib 2 (GTK) scheduler.
- Supports continuation execution across multiple threads.
- Supports asynchronous primitives corresponding to synchronous primitives (mutex, semaphore, conditional variables and reader-writer-lock).

### Environment

- C++20 compiler (GNU g++, and probably clang)
- POSIX environment (Core functionality will work in non-POSIX environments as long as the major C++20 libraries are available)
- Windows (optional, Win32 handle and OVERLAPPED wait support)
- Linux (optional, io_uring support)
- GLib 2/GTK (optional)

---

## Installation

Copy [`cardio.h`](./include/cardio.h) into your project and include it. That is all.

---

## Basic Usage

All public API definitions are placed in the `cardio` namespace.

The most important feature set in cardio is `dispatcher` and `promise`.
You can think of `promise` as roughly equivalent to TypeScript/JavaScript's
`Promise`.

`dispatcher` is the abstract base class that manages asynchronous
"continuations", and corresponds to the runtime. Use `dispatcher_host` as the
standard concrete dispatcher host.
When using cardio, you need to initialize a `dispatcher_host`.

First, place and initialize a `dispatcher_host`, then let it wait so that it can
execute asynchronous continuations:

```cpp
// Header-only library
#include "cardio.h"

int main() {
  // Initialize the dispatcher.
  cardio::dispatcher_host d;

  // Park the main thread and execute asynchronous continuations.
  d.park();

  return 0;
}
```

- The `park()` function continues executing asynchronous continuations as long as
  they exist.

In the code above, no asynchronous processing exists, so the function returns
immediately and the program exits.
Next, insert the first asynchronous process to run:

```cpp
// Async function
static cardio::promise<int> add_async(int a, int b) {
  co_return a + b;  // Return the result of an async function with `co_return`.
}

// Async function
static cardio::promise<void> main_async() {
  auto r = co_await add_async(1, 2);  // Can use `co_await`.
  printf("1 + 2 = %d\n", r);
}

int main() {
  cardio::dispatcher_host d;

  // Call the async function and start asynchronous processing.
  auto p = main_async();
  (void)p;

  // park() returns when there are no more asynchronous continuations.
  d.park();

  return 0;
}
```

The asynchronous function `add_async()` is written to return `promise<int>`.
In the example above, it only performs simple addition, so there is no practical
reason for it to be asynchronous, but returning `promise<int>` makes it clear
that this is an asynchronous function.

Also declare the overall function `main_async()`, which calls and performs work
with asynchronous functions such as `add_async()`, as an asynchronous function.
Because it does not return a result, it uses `promise<void>`.

Calling the asynchronous function `main_async()` before `park()` starts
asynchronous processing.
Keep the returned root promise alive until `park()` finishes, even when you do
not need to retrieve its result.

Even if `add_async()` returns a result instantly as shown above, the caller
cannot know that.
In other words, you do not know when the asynchronous function will complete.
`promise<T>` abstracts this behavior, and `co_await` retrieves its result.

Then `park()` runs the asynchronous processing and continues until it completes.
Once all asynchronous processing is complete and there is nothing else to wait
for, the `park()` function exits.
In this example, execution continues until the calculation completes and the
value is printed with `printf()`.

At this point, you may wonder whether such short code could be written directly
in `main()` without preparing `main_async()`:

```cpp
static cardio::promise<int> add_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  // Could add_async() be called directly here?
  auto r = co_await add_async(1, 2);
  printf("1 + 2 = %d\n", r);

  d.park();

  return 0;
}
```

However, this cannot be done. `main()` does not return a promise, so `co_await`
cannot be used inside `main()`.
Every asynchronous function must return `promise<T>`.
Therefore, preparing and calling a "root asynchronous function" such as
`main_async()` is a simple way to solve this problem. There are other ways, but
they are omitted here.

---

## Exceptions

Exceptions can also be handled naturally, but care is required when a
`promise<T>` is not awaited with `co_await`.
The following example throws an exception inside an asynchronous function before
performing an asynchronous wait (`co_await`):

```cpp
// Async function (exception before asynchronous wait)
static cardio::promise<void> exception_async() {
  // Throw an exception before performing an asynchronous wait.
  throw std::exception();
  co_return;
}

// Async function
static cardio::promise<void> main_async() {
  try {
    // Start asynchronous processing, but an exception occurs immediately.
    auto p = exception_async();  // (Not awaited with co_await.)
    (void)p;
  } catch (const std::exception& ex) {
    // It can be received by the caller.
    printf("%s\n", ex.what());
  }
}

int main() {
  cardio::dispatcher_host d;

  // Call the async function and start asynchronous processing.
  auto p = main_async();
  (void)p;

  // Park this thread and execute asynchronous continuations.
  d.park();

  return 0;
}
```

The example above can detect the exception without issue.

However, if an exception is thrown after an asynchronous wait, it cannot be
received by the `try` / `catch` around the asynchronous function call.
The stored exception is rethrown when the caller retrieves the result of the
`promise` with `co_await`:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  co_return a + b;
}

// Async function (exception after asynchronous wait)
static cardio::promise<void> exception_async() {
  // Call and await an asynchronous function.
  co_await calc_async(1, 2);
  // Then throw an exception.
  throw std::exception();
}

// Async function (does not throw exceptions)
static cardio::promise<void> main_async() noexcept {
  try {
    // Start asynchronous processing and wait with co_await.
    co_await exception_async();
  } catch (const std::exception& ex) {
    // It can be received by the caller if awaited with co_await.
    printf("%s\n", ex.what());
  }
}

int main() {
  cardio::dispatcher_host d;

  // Call the async function and start asynchronous processing.
  auto p = main_async();
  (void)p;

  // Park this thread and execute asynchronous continuations.
  d.park();

  return 0;
}
```

This way, if you always write code so that asynchronous operations are awaited
with `co_await`, the `catch` block can catch exceptions whether the function
throws before or after an asynchronous wait.

On the other hand, if `main_async()` itself throws an exception, it is treated as
an "unhandled exception".
This can be detected by hooking `dispatcher`'s `unhandled_exception()`:

```cpp
// Async function
static cardio::promise<void> exception_async() {
  // Throw an exception after an asynchronous wait.
  co_await cardio::resolved();
  throw std::exception();
}

// Async function (exception leaks out)
static cardio::promise<void> main_async() {
  // Start asynchronous processing.
  co_await exception_async();
}

int main() {
  cardio::dispatcher_host d;

  // Unhandled exception hook.
  d.unhandled_exception([](std::exception_ptr exception) {
    try {
      std::rethrow_exception(exception);
    } catch (const std::exception& ex) {
      printf("%s\n", ex.what());
    }
  });

  // Run the asynchronous function.
  // (The root promise is kept alive, but no one retrieves its result.)
  auto p = main_async();
  (void)p;

  // Park this thread and execute asynchronous continuations.
  d.park();

  return 0;
}
```

- If the `unhandled_exception()` hook is not set, the exception is recorded in
  `std::clog`.
- If the `unhandled_exception()` hook itself throws an exception, that exception
  is ignored.

The `unhandled_exception()` hook is fairly low-level, and it is detached from
`main_async()`, so it cannot perform complex work.
Therefore, asynchronous entry-point functions such as `main_async()` should
catch and handle all exceptions inside the asynchronous function as much as
possible, rather than relying on the `unhandled_exception()` hook.

Note: Code examples in this document omit exception handling unless it is
necessary.

---

## Working With Promises

`promise<T>` manages continuations of asynchronous functions. It also holds
results that return values and exceptions.
Functions that return `promise<T>` are treated as asynchronous functions. You can
use `co_await` and `co_return` inside the function to implement asynchronous
processing:

```cpp
static cardio::promise<void> main_async() {
  // Call and await an asynchronous function.
  auto r = co_await calc_async(1, 2);

  // Print the result.
  printf("%d\n", r);
}
```

However, when returning a value or throwing an exception immediately, you can use
`resolved()` or `rejected()` without using `co_await`:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  // Return the result directly without an asynchronous wait.
  return cardio::resolved(a + b);
}

static cardio::promise<int> calc_will_fail_async(int a, int b) {
  // Return a failure directly without an asynchronous wait.
  return cardio::rejected<int>(std::runtime_error("calc failed"));
}
```

Use `promise_source<T>` when you want to complete a `promise<T>` later from
outside an asynchronous function, such as from an external callback or another
thread.
`promise_source<T>` separates the side that performs completion operations from
the `promise<T>` side that waits for or retrieves the result.

This can be used to turn APIs that do not natively support C++20 asynchronous
processing into promises:

```cpp
static cardio::promise<int> calc_async(int param) {
  // Create a promise_source and prepare to return a future result.
  auto source = std::make_shared<cardio::promise_source<int>>();

  // Get the associated promise.
  auto p = source->get_promise();

  // Start asynchronous processing.
  // For example, use an external API that delivers completion through a callback.
  foobar::request(param, [source]() {
    // Notify the promise because the operation has completed.
    source->resolve(42);
  });

  return p;
}
```

- `promise_source<T>::get_promise()` can be called only once.
- `resolve()` or `reject()` completes the corresponding `promise<T>` and resumes
  waiting continuations.
- `reject()` is available only in exception-enabled builds.
- `try_resolve()` and, in exception-enabled builds, `try_reject()` return `false`
  instead of throwing an exception if the promise is already completed.
- In exception-enabled builds, `cancel()` or `try_cancel()` can complete the
  promise as a failure with `canceled_exception`.
- If an incomplete `promise_source<T>` is destroyed, then in exception-enabled
  builds the `promise<T>` completes as a failure with `std::runtime_error`.

Here is a more concrete example.
The following wraps an imaginary C-language remote API call in an asynchronous
function:

```cpp
// The remote API is a C entry point, so define a structure for passing state.
struct foobar_request_state {
  std::shared_ptr<cardio::promise_source<std::string>> source;
};

// Call the remote API and get the result.
static cardio::promise<std::string> request_foobar_async() {

  // Prepare a state management structure to pass to the remote API.
  auto* state = new foobar_request_state{};
  state->source = std::make_shared<cardio::promise_source<std::string>>();
  auto promise = state->source->get_promise();

  // Call the remote API. The result is returned by function-pointer callbacks.
  foobar_request_text(
    // Success callback.
    [](const char* text, void* state_ptr) {
      auto* state = static_cast<foobar_request_state*>(state_ptr);
      auto source = state->source;
      delete state;
      // Notify the promise.
      source->resolve(text);
    },
    // Failure callback.
    [](int error, void* state_ptr) {
      auto* state = static_cast<foobar_request_state*>(state_ptr);
      auto source = state->source;
      delete state;
      // Notify the promise.
      source->reject(std::system_error(
          error, std::generic_category(), "foobar_request_text failed"));
    },
    state);

  return promise;
}
```

---

## C++-Specific Pitfalls in Asynchronous Processing

An asynchronous function starts running when it is called, but the returned
`promise` owns the asynchronous operation (coroutine).
Do not immediately discard a root promise:

```cpp
// Bad: the returned promise is destroyed right after this full-expression.
(void)main_async();

d.park();
```

Keep the root promise alive until `park()` returns, even when you do not need to
retrieve its result:

```cpp
auto p = main_async();
(void)p;

d.park();
```

If you intentionally do not need the root promise result, `fire_and_forget()`
can keep it alive until completion:

```cpp
cardio::fire_and_forget(main_async());

d.park();
```

When exception support is enabled, failures escaping a promise passed to
`fire_and_forget()` are reported to `dispatcher::unhandled_exception()`, the same
as other unhandled root promise failures. This only keeps the promise alive; it
does not extend the lifetime of objects referenced by the asynchronous operation.

The same rule applies to values referenced by an asynchronous function.
Every object referenced by asynchronous processing must outlive the asynchronous
processing itself.
Be especially careful with capturing asynchronous lambdas.
For example, the following lambda object is destroyed immediately after the
call, but the asynchronous operation may resume later and read its captures:

```cpp
auto p = [&]() -> cardio::promise<void> {
  result = co_await work_async();
}();
```

Prefer a named asynchronous function:

```cpp
static cardio::promise<void> run_async(int& result) {
  result = co_await work_async();
}

auto p = run_async(result);
(void)p;

d.park();
```

Alternatively, keep the lambda object alive for the duration of the asynchronous
operation:

```cpp
auto run = [&]() -> cardio::promise<void> {
  result = co_await work_async();
};

auto p = run();
(void)p;

d.park();
```

In all cases, `p`, `run`, `result`, and any other referenced objects must remain
alive until the asynchronous processing has completed.

---

## Cancellation

You can use `cancellation_source` and `cancellation` to notify asynchronous
processing of cancellation requests.
This is similar to the role of TypeScript/JavaScript's `AbortController` /
`AbortSignal`.

`cancellation_source` is the side that requests cancellation, and `cancellation`
is the handle for the side that observes cancellation.
The following example notifies an API that supports `cancellation` of a
cancellation request:

```cpp
// Read the specified file.
static cardio::promise<std::string> read_file_async(
  const std::string& path, cardio::cancellation cancellation) {
  // Actively check the cancellation state.
  cancellation.throw_if_cancellation_requested();

  // Open the file.
  auto fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);

  try {
    auto text = std::string{};
    auto buffer = std::array<char, 4096>{};

    while (true) {
      // Wait until the file becomes readable.
      // Pass the cancellation notification to the wait target.
      auto events = co_await cardio::from_fd(
          fd, cardio::fd_event::read, cancellation);

      // Read the file.
      const auto size = read(fd, buffer.data(), buffer.size());
      if (size == 0) {
        break;
      }
      text.append(buffer.data(), static_cast<std::size_t>(size));
    }

    close(fd);
    co_return text;
  } catch (...) {
    close(fd);
    throw;
  }
}

static cardio::promise<void> main_async() noexcept {
  try {
    // Prepare a cancellation_source.
    cardio::cancellation_source source;

    // (Cancel the asynchronous processing after the timeout has elapsed.)
    my_timeout(10000, [&]() {
      source.cancel();
    });

    // Pass cancellation and start asynchronous processing.
    auto r = co_await read_file_async(
      "foobar.txt", source.get_cancellation());

    // Result.
    std::cout << "result: " << r << '\n';
  } catch (const std::exception& ex) {
    printf("%s\n", ex.what());
  }
}

int main() {
  cardio::dispatcher_host d;

  auto p = main_async();
  (void)p;

  // If cancellation occurs, it is handled by main_async().
  d.park();

  return 0;
}
```

- `cancellation_source::get_cancellation()` gets the reader-side `cancellation`.
- `cancellation_source::cancel()` returns `true` for the first
  cancellation request and `false` if cancellation has already been requested.
- `cancellation::is_cancellation_requested()` returns whether cancellation is
  currently requested.
  This is an active check, and cancellation is detected only at locations where
  the caller explicitly checks it.
  Alternatively, you can use `cancellation::throw_if_cancellation_requested()`.
  This throws `canceled_exception` if cancellation has been requested.

When using an API that does not support `cancellation`, you need to handle the
cancellation request yourself.
Using `cancellation::on_cancellation_requested()` gives you an opportunity to run
a callback when cancellation is requested.

The following example adds complete cancellation handling to the asynchronous
wrapper function shown in the previous chapter:

```cpp
// The remote API is a C entry point, so define a structure for passing state.
struct foobar_request_state {
  std::shared_ptr<cardio::promise_source<std::string>> source;
  cardio::cancellation_registration registration;
  foobar_request_handle request = nullptr;
};

// Call the remote API and get the result.
static cardio::promise<std::string> request_foobar_async(
    cardio::cancellation cancellation) {

  // Prepare a state management structure to pass to the remote API.
  auto* state = new foobar_request_state{};
  state->source = std::make_shared<cardio::promise_source<std::string>>();
  auto promise = state->source->get_promise();

  // Call the remote API. The result is returned by function-pointer callbacks.
  state->request = foobar_request_text(
    // Success callback.
    [](const char* text, void* state_ptr) {
      auto* state = static_cast<foobar_request_state*>(state_ptr);
      auto source = state->source;
      // Unregister the cancellation callback.
      state->registration.reset();
      delete state;
      // Notify the promise.
      (void)source->try_resolve(text);
    },
    // Failure callback.
    [](int error, void* state_ptr) {
      auto* state = static_cast<foobar_request_state*>(state_ptr);
      auto source = state->source;
      // Unregister the cancellation callback.
      state->registration.reset();
      delete state;
      // Notify the promise.
      (void)source->try_reject(std::system_error(
          error, std::generic_category(), "foobar_request_text failed"));
    },
    state);

  // Handling when a cancellation request occurs from outside the function.
  state->registration = cancellation.on_cancellation_requested(
    [state] {
      auto source = state->source;
      // Cancel the API call:
      // In this example, the failure callback is called after foobar_cancel(),
      // and state is released there.
      foobar_cancel(state->request);
      // Notify the promise.
      (void)source->try_cancel();
    });

  return promise;
}
```

- `on_cancellation_requested()` registers a callback that is called when
  cancellation is requested.
  The registration is removed when the returned `cancellation_registration` is
  destroyed or `reset()`.
- Cancellation is best effort.
  It does not forcibly destroy running asynchronous processing from outside; it
  is detected by cancellation-aware APIs or active checks by the caller.

### Handling Completion Races

Callbacks from external APIs and I/O requests can race between successful
completion, failure, and cancellation.
In these cases, use the `try_*` APIs of `promise_source<T>`:

```cpp
static cardio::promise<int> request_async(
    cardio::cancellation cancellation) {
  auto source = std::make_shared<cardio::promise_source<int>>();
  auto promise = source->get_promise();
  auto registration =
      std::make_shared<cardio::cancellation_registration>();

  *registration = cancellation.on_cancellation_requested([source] {
    source->try_cancel();
  });

  foobar_request_number(
    [source, registration](int value) {
      registration->reset();
      (void)source->try_resolve(value);
    },
    [source, registration](std::exception_ptr ep) {
      registration->reset();
      (void)source->try_reject(std::move(ep));
    });

  return promise;
}
```

- `resolve()`, `reject()`, and `cancel()` throw `std::logic_error` if the promise
  is already completed.
- `try_resolve()`, `try_reject()`, and `try_cancel()` return `false` if the
  promise is already completed.
- Use `try_*` in places where races occur as part of normal control flow.

---

## Waiting for POSIX File Descriptors

In POSIX environments, file descriptor readiness can be awaited as a `promise`.
This feature is exposed only in builds where `_POSIX_C_SOURCE` or
`CARDIO_HAS_POSIX_FD=1` is enabled.
In non-POSIX builds, `fd_event` and `from_fd()` are not exposed.
`cardio::from_fd()` waits until the specified file descriptor becomes readable or
writable, and returns the actual state that occurred as `fd_event`:

```cpp
#include <unistd.h>

// Asynchronous processing using a POSIX fd.
static cardio::promise<void> main_async() noexcept {
  // Create a pipe.
  int fds[2];
  pipe(fds);

  // Send data to the pipe.
  write(fds[1], "x", 1);

  // Wait until the pipe endpoint becomes readable.
  auto events = co_await cardio::from_fd(
      fds[0], cardio::fd_event::read);

  if ((events & cardio::fd_event::read) != cardio::fd_event::none) {
    // The fd is readable.
    printf("Ready for read.\n");
  }

  if ((events & cardio::fd_event::hangup) != cardio::fd_event::none) {
    // The fd has hung up.
    printf("Hanged up.\n");
  }

  if ((events & cardio::fd_event::error) != cardio::fd_event::none) {
    // An error occurred on the fd.
    printf("Error occurred.\n");
  }

  close(fds[0]);
  close(fds[1]);
}

int main() {
  cardio::dispatcher_host d;

  // Start asynchronous pipe processing.
  auto p = main_async();
  (void)p;

  // Wait for registered fd readiness in addition to promise continuations.
  d.park();

  return 0;
}
```

The wait target is specified with `fd_event::read` and `fd_event::write`.
When waiting for multiple conditions at the same time, combine them with bitwise
operators:

```cpp
// Wait for both fd reading and writing.
auto events = co_await cardio::from_fd(
    fd, cardio::fd_event::read | cardio::fd_event::write);

if ((events & cardio::fd_event::write) != cardio::fd_event::none) {
  // The fd is writable.
}
```

- `from_fd()` does not take ownership of the file descriptor.
  Therefore, even if the promise is destroyed while waiting, the fd is not
  closed, and the caller must close it.
- In exception-enabled builds, `from_fd(fd, interests, cancellation)` can be used
  to cancel the wait.
  If cancellation is requested before readiness, the returned promise completes
  as a failure with `canceled_exception`.
- If `fd < 0`, or if neither reading nor writing is specified, such as
  `fd_event::none`, `from_fd()` returns a failed promise.
  This failure is rethrown as an exception when retrieving the result with
  `co_await`, `try_result()`, or `unsafe_result()`, just like an ordinary promise.
- `fd_event::error` and `fd_event::hangup` are states returned from POSIX
  `poll()` results.
  They are not specified as wait conditions; they are checked as results after
  readiness completion.

---

## Implementing Asynchronous I/O With Linux io_uring

On Linux, you can implement fully asynchronous I/O using io_uring.
This feature is disabled by default.
To use it, define `CARDIO_WITH_LINUX_IO_URING=1`, install the liburing development headers and library, and link `liburing`.
For example, on Debian/Ubuntu, you can install them with APT packages as follows:

```bash
sudo apt update
sudo apt install -y liburing-dev pkg-config
```

At compile time, define `CARDIO_WITH_LINUX_IO_URING=1` and pass the liburing
compile and link options with `pkg-config`:

```bash
g++ -std=c++20 -Iinclude -DCARDIO_WITH_LINUX_IO_URING=1 app.cpp -o app $(pkg-config --cflags --libs liburing)
```

The following example copies a file asynchronously using io_uring.
To use io_uring, initialize the `cardio::io_uring` structure and use
supplemental functions in the `cardio::io_urings` namespace:

```cpp
#include <array>
#include <cstddef>
#include <fcntl.h>
#include <span>
#include <stdexcept>
#include <unistd.h>

// Asynchronous file copy using io_uring.
static cardio::promise<void> main_async() noexcept {
  // Prepare a buffer.
  auto buffer = std::array<std::byte, 4096>{};
  auto bytes = std::as_writable_bytes(std::span(buffer));

  // Prepare io_uring.
  cardio::io_uring io;

  // Open the source and destination fds.
  auto source_fd = open("input.bin", O_RDONLY);
  auto destination_fd = open("output.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);

  while (true) {
    // Read asynchronously from source_fd.
    auto read_size = co_await cardio::io_urings::read(io, source_fd, bytes);
    if (read_size == 0) {
      break;
    }

    // Write only the read range asynchronously to destination_fd.
    auto remaining = std::span<const std::byte>(bytes.data(), read_size);
    while (!remaining.empty()) {
      auto write_size =
          co_await cardio::io_urings::write(io, destination_fd, remaining);
      if (write_size == 0) {
        throw std::runtime_error("write made no progress");
      }
      remaining = remaining.subspan(write_size);
    }
  }

  close(source_fd);
  close(destination_fd);
}

int main() {
  cardio::dispatcher_host d;

  // Start asynchronous file copy.
  auto p = main_async();
  (void)p;

  // Wait for io_uring completion events in addition to promise continuations.
  d.park();

  return 0;
}
```

- `cardio::io_uring` owns the queue and coordinates with the dispatcher.
- `io.submit<T>()` and `cardio::io_urings::submit()` can be used to submit a
  single operation directly with liburing's SQE preparation functions.
- `cardio::io_urings::read()` and `cardio::io_urings::write()` submit I/O requests to
  io_uring and return `promise<std::size_t>`, which returns the actual read or
  write size on completion.
  Other supplemental functions cover fsync, open/close/statx, and path mutation
  operations. See the supplemental functions section.
- Path-based helpers copy path strings and keep them alive until completion.
- In exception-enabled builds, `cancellation` can be passed to all io_uring
  helpers.
  On cancellation, the returned promise is completed as a failure with
  `canceled_exception`, and a cancel request is submitted to io_uring if possible.
  However, cancellation is best effort because native I/O side effects may
  already have occurred.
- When an I/O error occurs, an exception is thrown from the `promise`.
  This also applies when the running kernel does not support a submitted
  operation and io_uring reports a negative CQE result.
  Note that exception handling is omitted in the code example above.

### Implementing Additional io_uring Operations

If cardio does not provide a dedicated helper for an io_uring operation you
want to use, you can still build a small helper with the public
`cardio::io_uring::submit<T>()` API.
For example, the following user-defined helper submits `IORING_OP_MADVISE`:

```cpp
#include <cstddef>
#include <system_error>
#include <sys/mman.h>

static cardio::promise<void> madvise_async(
    cardio::io_uring& io,
    void* address,
    std::size_t length,
    int advice) {
  return io.submit<void>(
    [address, length, advice](::io_uring_sqe* sqe) {
      io_uring_prep_madvise(
          sqe,
          address,
          static_cast<off_t>(length),
          advice);
    },
    [](cardio::io_uring_completion completion) {
      if (completion.result < 0) {
        throw std::system_error(
            -completion.result,
            std::generic_category(),
            "io_uring madvise failed");
      }
    });
}
```

This pattern works well for single-shot operations that complete with one CQE
and do not require extra `io_uring_register_*()` setup. The caller must keep
any memory, path strings, or other pointers used by the SQE valid until the
returned promise completes.

Note: Operations that require multiple CQEs, multishot streams, linked SQEs, or
registered buffers/files are not naturally represented by the current
single-shot promise API.

---

## Integration With GLib 2 (GTK)

In environments where GLib 2 or GTK is available, you can integrate GLib's
scheduler by specifying `CARDIO_WITH_GLIB=1`.
In other words, you can integrate asynchronous processing using cardio within
applications such as GTK.

To use this feature, install the GLib development headers and libraries, and
link GLib.
For example, on Debian/Ubuntu, you can install them with APT packages as follows:

```bash
sudo apt update
sudo apt install -y libglib2.0-dev pkg-config
```

At compile time, define `CARDIO_WITH_GLIB=1` and pass GLib compile and link
options with `pkg-config`:

```bash
g++ -std=c++20 -Iinclude -DCARDIO_WITH_GLIB=1 app.cpp -o app $(pkg-config --cflags --libs glib-2.0)
```

GLib integration uses `dispatcher_group_glib`, `dispatcher_host_glib`, and
`dispatcher_host_glib_auto`.
`dispatcher_group_glib` manages the application's shutdown timing
(`dispatcher_group` is described later):

```cpp
int main() {
  // Initialize the dispatcher with GLib co-waiting enabled.
  cardio::dispatcher_group_glib group;
  cardio::dispatcher_host_glib d(group);

  // (Code that uses GLib, GTK, or another GLib-based message pump.
  //  Call shutdown() at the time of application exit.)
  // {
  //   //  :
  //   //  :
  //   group.shutdown();
  // }

  // Park the main thread while waiting for both GLib sources and cardio work.
  d.park();

  return 0;
}
```

- `dispatcher_host_glib` waits for GLib sources and cardio work together from
  `park()`. `park_policy` controls whether ready GLib sources or cardio
  continuations run first.
- `dispatcher_host_glib_auto` attaches a cardio `GSource` to the group's
  `GMainContext`. Any application-owned GLib message pump for that context can
  execute cardio continuations, timer waits, POSIX fd waits, and io_uring
  completions. Its `park()` is a compatibility wrapper around the same
  `GMainContext`, and GLib source priority determines ordering.
- Calling `dispatcher_group_glib::shutdown()` causes `park()` to exit.
  Unlike when using `dispatcher` alone, `park()` does not automatically exit when
  asynchronous continuations no longer exist.

---

## GIO File I/O Helpers

When GLib integration is enabled, GIO file I/O helpers can be enabled with
`CARDIO_WITH_GIO=1`. This also requires `CARDIO_WITH_GLIB=1` and
`gio-2.0 >= 2.44`:

```bash
g++ -std=c++20 -Iinclude -DCARDIO_WITH_GLIB=1 -DCARDIO_WITH_GIO=1 app.cpp -o app $(pkg-config --cflags --libs gio-2.0)
```

The helpers are available in `cardio::gio`. They wrap stable, single-shot
`*_async()` / `*_finish()` GIO operations for streams, files, file enumerators,
and file stream `query_info()` calls. Operations outside that scope can still be
adapted with `cardio::gio::submit<T>(start, finish)`.

For example, can copy full file contents through GIO helpers:

```cpp
static cardio::promise<void> copy_file_contents_async(
    GFile* source,
    GFile* destination) {
  auto contents = co_await cardio::gio::load_contents(source);
  co_await cardio::gio::replace_contents(
      destination,
      std::span<const std::byte>(contents.bytes.data(), contents.bytes.size()),
      nullptr,
      false,
      G_FILE_CREATE_NONE);
}
```

Before starting an operation, the helper checks
`get_current_dispatcher().get_feature()` for `dispatcher_feature::gio`.
`dispatcher_host_glib` and `dispatcher_host_glib_auto` report both
`dispatcher_feature::glib` and `dispatcher_feature::gio` when GIO support is
compiled in. A regular `dispatcher_host` does not report `gio`, so GIO helpers
return a rejected promise with `std::invalid_argument`.

Ownership and lifetime follow GIO conventions:

- Returned `GObject*`, `GBytes*`, and `GList*` values are raw caller-owned values.
  Release them with the matching GIO API such as `g_object_unref()`,
  `g_bytes_unref()`, or `g_list_free_full(list, g_object_unref)`.
- Buffers passed as `std::span` to read, write, and replace helpers must remain
  valid until the returned promise completes.
- Helpers that accept `GBytes*` rely on GIO keeping the bytes alive for the
  operation; the caller may release its own reference after the async call has
  been started.
- `GError` failures are converted to `cardio::gio::gio_error`, which stores the
  original domain, code, and message. `G_IO_ERROR_CANCELLED` is converted to
  `cardio::canceled_exception`.

The cancellable `submit<T>()` overload creates a helper-owned `GCancellable`.
When the supplied `cardio::cancellation` is requested, the helper calls
`g_cancellable_cancel()` and the promise completes when GIO invokes the callback.

---

## Waiting for Win32 Handles

On Windows, Win32 handles can be awaited as a `promise` when `CARDIO_HAS_WIN32_HANDLE=1` is enabled.
This is the default for `_WIN32` builds.

`dispatcher_host_win32::park()` waits with `MsgWaitForMultipleObjectsEx()`,
so the parked thread can wait for registered handles while also pumping Win32
messages. This is also suitable for COM apartment threads and marshaling
operations. By default, queued Win32 messages are pumped before queued or inline
cardio continuations. Pass `cardio::park_policy::continuation_first` as the
second `park()` argument to run cardio continuations first.

`dispatcher_host_win32_auto` creates a message-only window and routes cardio
continuations, timers, and Win32 handle waits through private Win32 messages.
Use it when an existing or modal Win32 message pump may run without calling
`park()`. Construct and park this dispatcher on the same thread. Win32 message
queue ordering determines when cardio work runs.

```cpp
#include <windows.h>

static cardio::promise<void> wait_async(HANDLE handle) {
  auto result = co_await cardio::from_win32_handle(handle);
  if (result == cardio::win32_handle_event::signaled) {
    printf("The event was signaled.\n");
  }
}
```

For externally submitted overlapped I/O, wait for the `OVERLAPPED` operation and
retrieve the transferred byte count:

```cpp
OVERLAPPED overlapped{};
overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

ReadFile(file, buffer, size, nullptr, &overlapped);

auto result = co_await cardio::from_win32_overlapped(file, overlapped);
printf("Transferred: %lu\n", result.bytes_transferred);
```

When cardio should own the `OVERLAPPED` structure for a single-shot I/O
operation, use the helpers in `cardio::win32`:

```cpp
char buffer[4096]{};
auto read_size = co_await cardio::win32::read(
    file,
    std::as_writable_bytes(std::span<char>(buffer)));

auto write_size = co_await cardio::win32::write(
    file,
    std::as_bytes(std::span<const char>(buffer, read_size)),
    0);
```

- `from_win32_handle()` and `from_win32_overlapped()` do not take ownership of
  the handle, the `OVERLAPPED` structure, or `OVERLAPPED::hEvent`.
- `win32::submit()`, `win32::read()`, and
  `win32::write()` own the `OVERLAPPED` structure and its manual-reset
  event until the native operation completes. They do not own the handle or I/O
  buffers.
- If `OVERLAPPED::hEvent` is `nullptr`, the file handle itself is waited on. Use a
  distinct manual-reset event for each concurrently pending operation on the
  same handle so completions can be distinguished.
- Cancellation for `from_win32_overlapped()` only cancels the promise wait. It
  does not call `CancelIoEx()` or close the handle. Cancellation for
  `win32` operations calls `CancelIoEx()` for the helper-owned
  `OVERLAPPED` operation on a best-effort basis, then completes the returned
  promise after the native operation has completed.
- The Win32 wait backend is subject to the `MAXIMUM_WAIT_OBJECTS` limit. Typically, this value is 64.
  Since the dispatcher's wakeup events and message queue consume wait slots,
  the maximum number of user handles that can wait simultaneously on a single dispatcher is `MAXIMUM_WAIT_OBJECTS - 2`.

### Win32 I/O Completion Port Helpers

For Windows file and pipe I/O that should use an I/O completion port, create a
`cardio::io_completion_port` and submit operations through `cardio::iocps`:

```cpp
cardio::io_completion_port port;
char buffer[4096]{};

auto read_size = co_await cardio::iocps::read(
    port,
    file,
    std::as_writable_bytes(std::span<char>(buffer)));

auto write_size = co_await cardio::iocps::write(
    port,
    file,
    std::as_bytes(std::span<const char>(buffer, read_size)),
    0);
```

- `io_completion_port` owns a completion port and one pump thread. The pump
  thread waits for IOCP packets and posts completed promises back to the
  dispatcher that started the operation.
- `iocps::submit()` can adapt another single-shot `OVERLAPPED` operation. The
  completion callback receives `win32_iocp_completion`.
- The handle and buffers are not owned and must remain valid until completion.
- Do not use the same handle with `from_win32_overlapped()` or another
  completion port. IOCP helpers associate the handle with their completion port.

---

## Supplemental Functions

cardio provides supplemental functions. These are enabled by default.
You can disable them with `CARDIO_WITH_SUPPLEMENTAL=0`.
io_uring helpers are available only when `CARDIO_WITH_LINUX_IO_URING=1` is
defined.
Win32 helpers are available only when `CARDIO_HAS_WIN32_HANDLE=1` is defined.

| Function | Description |
|:----|:----|
| `promises::delay()` | Returns a promise that waits for the specified time in milliseconds. |
| `promises::all()` | Returns a promise that resolves when all input promises resolve, similar to JavaScript `Promise.all()`. |
| `promises::start_new()` | Runs a callable on a new worker thread and returns a promise for its result. |
| `cancellations::timeout()` | Returns a `cancellation_source` that requests cancellation after the specified time in milliseconds, similar to JavaScript `AbortSignal.timeout()`. |
| `cancellations::any()` | Returns a `cancellation_source` that is canceled when any input `cancellation` is canceled, similar to JavaScript `AbortSignal.any()`. |
| `io_urings::submit()` | Submits a raw single-shot io_uring operation and returns its completion fields. |
| `io_urings::read()` | Submits an asynchronous io_uring read operation and returns the number of bytes read. |
| `io_urings::write()` | Submits an asynchronous io_uring write operation and returns the number of bytes written. |
| `io_urings::fsync()` | Submits an asynchronous io_uring fsync operation. |
| `io_urings::open()`, `io_urings::close()`, `io_urings::statx()` | Submit fd and metadata operations. |
| `io_urings::rename()`, `io_urings::unlink()`, `io_urings::mkdir()` | Submit path mutation operations. |
| `win32::submit()` | Submits a helper-owned single-shot Win32 `OVERLAPPED` operation and returns its completion fields. |
| `win32::read()` | Submits an asynchronous Win32 `ReadFile()` operation and returns the number of bytes read. |
| `win32::write()` | Submits an asynchronous Win32 `WriteFile()` operation and returns the number of bytes written. |
| `iocps::submit()` | Submits a helper-owned single-shot Win32 IOCP operation and returns its completion fields. |
| `iocps::read()` | Submits an asynchronous Win32 IOCP `ReadFile()` operation and returns the number of bytes read. |
| `iocps::write()` | Submits an asynchronous Win32 IOCP `WriteFile()` operation and returns the number of bytes written. |

## Async Primitives

cardio also provides promise-based primitives in `cardio::primitives`. They are
enabled by default and can be disabled with `CARDIO_WITH_PRIMITIVES=0`.

| Type | Description |
|:----|:----|
| `primitives::mutex` | Asynchronous mutual exclusion. |
| `primitives::semaphore` | Limits the number of concurrent holders. |
| `primitives::reader_writer_lock` | Allows concurrent readers and one exclusive writer. |
| `primitives::conditional` | Releases one waiter for each trigger. |
| `primitives::manually_conditional` | Manually raised/dropped conditional state. |

`lock_handle` releases automatically when it leaves scope, and `release()` can
also be called explicitly:

```cpp
static cardio::promise<void> update_async(
    cardio::primitives::mutex& locker) {
  auto handle = co_await locker.lock();
  // Critical section.
}
```

When exceptions are enabled, lock and wait operations also accept
`cardio::cancellation` and fail with `canceled_exception` if cancellation is
requested while the operation is pending.

### About delay and timeout

`promises::delay()` and `cancellations::timeout()` are measured by the current
dispatcher's internal timer queue. They do not create a per-timeout timer fd or
worker thread.

Elapsed time is observed only while the dispatcher is parked. The
`cancellation_source` returned by `cancellations::timeout()` enters the
canceled state when the dispatcher processes the expired timer, and registered
callbacks are then queued to the dispatcher that registered them.

### Starting work on a worker thread

`promises::start_new()` creates a new worker thread for one callable and returns
a promise that belongs to the caller's current dispatcher.
This is useful for running blocking or CPU-bound work without making it part of cardio's core
dispatcher implementation.

```cpp
static cardio::promise<int> worker_async() {
  co_await cardio::promises::delay(10);
  co_return 42;
}

int main() {
  cardio::dispatcher_host d;

  auto result = cardio::promises::start_new([] {
    return worker_async();
  });

  d.park();

  printf("result = %d\n", result.unsafe_result());
  return 0;
}
```

The callable may return a value, `void`, `promise<T>`, or `promise<void>`.
When it returns a promise, `start_new()` installs an independent dispatcher on the
worker thread and parks it until that promise completes.
`start_new()` creates thread per call and does not use a thread pool.

Cancellation is cooperative. Capture a `cardio::cancellation` in the callable and
pass it to cancellation-aware operations, or actively check it in your own work.
If you intentionally do not need the result, the returned promise can also be
kept alive with `fire_and_forget()`:

```cpp
cardio::fire_and_forget(cardio::promises::start_new([] {
  return worker_async();
}));
```

---

## Multithreaded Continuation Workers (Advanced Topic)

In the examples so far, `park()` has always run on the main thread.
`dispatcher_host` can call `park()` from multiple threads:

```cpp
#include <thread>

int main() {
  // Initialize the dispatcher.
  // This dispatcher is automatically associated with the current thread (the main thread).
  cardio::dispatcher_host d;

  // (Start some asynchronous I/O.)

  // Let an execution thread wait on the dispatcher.
  auto runner = [&]() {
    // In other threads, the dispatcher is not automatically associated,
    // so it must be associated explicitly.
    cardio::set_current_dispatcher(&d);

    // Park this thread and execute asynchronous continuations.
    d.park();
  };

  // Create two worker threads.
  std::thread t1(runner);
  std::thread t2(runner);

  // Also park the main thread, so three threads can execute continuations concurrently.
  d.park();

  // Wait for the worker threads to finish.
  t1.join();
  t2.join();

  return 0;
}
```

- Because continuations can be executed concurrently by multiple threads,
  asynchronous continuations can be processed efficiently.
- Additional threads can wait for continuation execution by calling `park()` after
  setting the current `dispatcher` with `set_current_dispatcher()`.
- As with processing on only the main thread, the `park()` function exits once all
  asynchronous processing is complete and there is nothing else to wait for.

Note that continuation processing may run on different threads:

```cpp
#include <thread>

static cardio::promise<void> main_async(int a, int b) noexcept {
  // Thread ID before asynchronous wait.
  auto before_id = std::this_thread::get_id();

  // Await an asynchronous function.
  co_await calc_async(a, b);

  // Thread ID after asynchronous wait.
  auto after_id = std::this_thread::get_id();

  // Note: this is satisfied when the dispatcher runs only on the main thread,
  // but it may not be satisfied when running on multiple threads.
  ASSERT(before_id == after_id);
}
```

- Naturally, continuation processing must take race conditions in multithreaded
  execution into account.

---

## Switching Dispatchers (Advanced Topic)

Using `switch_to()`, you can explicitly switch the continuation of the currently
running asynchronous function to another `dispatcher`.
For example, by separating a `dispatcher` dedicated to the main thread from a
`dispatcher` for worker threads, you can run only part of the processing on the
worker side and then return to the main thread side.
When coordinating multiple `dispatcher` instances, make them belong to the same
`dispatcher_group`.

`dispatcher_group` manages the number of asynchronous continuations in execution
and determines whether the `dispatcher` should fully exit.

Normally, `park()` exits when all continuation processing and callbacks are gone.
If you do not want it to exit when empty and instead want it to exit only on an
explicit instruction, create the `dispatcher_group` with
`exit_condition::exit_by_manual` and call `shutdown()` at exit.

Under this condition, `park()` does not exit even when no continuations or
callbacks remain.
By default, `park()` uses `shutdown_mode::gentle` shutdown handling and
exits after executing work that is already queued and wait operations whose
completion notifications have already arrived.
Specify `park(shutdown_mode::unsafe_immediate)` if you want to force
termination after `shutdown()` while leaving queued work or incomplete waits
behind.

`switch_to()` posts the continuation to the specified `dispatcher`.
When switching to the same `dispatcher`, it does not wait and continues
processing as-is.
The destination `dispatcher` must remain alive until the posted continuation is
complete and must be `park()`ed by some thread.

The following example switches between a `dispatcher` for the main thread and a
`dispatcher` that is `park()`ed by two worker threads:

```cpp
#include <iostream>
#include <thread>

static cardio::promise<void> main_async(
    cardio::dispatcher& md, cardio::dispatcher& wd) noexcept {
  // Still running on the main thread here.
  std::cout << "main thread: " << std::this_thread::get_id() << '\n';

  // Move the continuation to the worker dispatcher.
  // (Which worker thread runs it is unspecified.)
  co_await cardio::switch_to(wd);
  std::cout << "worker thread (arbitrary): " << std::this_thread::get_id() << '\n';

  // Move the continuation back to the main-thread dispatcher.
  co_await cardio::switch_to(md);
  std::cout << "main thread again: " << std::this_thread::get_id() << '\n';
}

int main() {
  // Use dispatcher_group to manage shutdown of multiple dispatchers.
  cardio::dispatcher_group group;

  // The first dispatcher associated with the dispatcher_group (md here)
  // is automatically associated as the current thread's (main thread's) dispatcher.
  cardio::dispatcher_host md(group);
  cardio::dispatcher_host wd(group);

  // Let worker threads wait on wd.
  auto runner = [&]() {
    // In other threads, the dispatcher is not automatically associated,
    // so it must be associated explicitly.
    cardio::set_current_dispatcher(&wd);

    // Park this thread and execute asynchronous continuations.
    wd.park();
  };

  // Create two worker threads.
  std::thread worker1(runner);
  std::thread worker2(runner);

  // Run the asynchronous function.
  auto p = main_async(md, wd);
  (void)p;

  // Park the main thread and execute asynchronous continuations.
  md.park();

  // Wait for the worker threads to complete.
  worker1.join();
  worker2.join();

  return 0;
}
```

---

## Special Promise Usage (Advanced Topic)

Usually, results from asynchronous functions are obtained with `co_await`.
cardio strongly recommends using `co_await` unless there is a specific reason
not to.

However, in special situations, you may need to obtain a result without using
`co_await`.
In those cases, `unsafe_result()`, `is_ready()`, and `try_result()` can be used
to obtain values outside an asynchronous function context.

`is_ready()` can directly check whether `promise<T>` has completed:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  // Start the asynchronous function and get a promise.
  auto p = calc_async(1, 2);

  // (The promise may not be complete yet here.)
  if (p.is_ready()) {
    printf("promise is done, r=%d\n", p.unsafe_result());
  }

  // Park this thread and execute asynchronous continuations.
  d.park();

  // (The promise should be complete here.)
  ASSERT(p.is_ready());
  // Calling unsafe_result() is valid.
  printf("promise is done, r=%d\n", p.unsafe_result());

  return 0;
}
```

- `unsafe_result()` has undefined behavior if you try to retrieve a result that
  has not completed yet.
  In the example above, this is fine because all asynchronous processing should
  be complete when `park()` returns.

Alternatively, `try_result()` may be smarter:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  auto p = calc_async(1, 2);

  // If the promise is not complete, nullptr is returned.
  if (auto r = p.try_result(); r != nullptr) {
    printf("promise is done, r=%d\n", *r);
  }

  // Park this thread and execute asynchronous continuations.
  d.park();

  // If the promise is complete, a pointer to the value is returned.
  if (auto r = p.try_result(); r != nullptr) {
    printf("promise is done, r=%d\n", *r);
  }

  return 0;
}
```

- Note that `unsafe_result()` and `try_result()` rethrow the stored exception if
  `promise<T>` has failed.
  Catch the exception with a `catch` block if necessary.

Again, trying to retrieve results with these functions should be avoided in
ordinary code.
Writing asynchronous processing straightforwardly with `co_await` is
recommended.

---

## Shared Runtime Library (Advanced Topic)

cardio is header-only by default. If an application loads C++ plugins or other
dynamic modules that also use cardio, each module may otherwise get its own
header-defined thread-local runtime state. In that case, build the optional
shared runtime library and compile every module with `CARDIO_SHARED_LIB=1`:

```bash
make -C samples/libcardio -f Makefile.posix
g++ -std=c++20 -Iinclude -DCARDIO_SHARED_LIB=1 app.cpp \
  -Lbuild/libcardio -lcardio -Wl,-rpath,'$ORIGIN/build/libcardio'
```

When building the shared runtime itself, define both `CARDIO_SHARED_LIB=1` and
`CARDIO_BUILD_SHARED_LIB=1`. The bundled `samples/libcardio/Makefile.posix`
does this for `libcardio.so`, and `samples/libcardio/Makefile.win32` builds
`libcardio.dll` with a MinGW import library.

All modules that use the shared runtime must be built with the same cardio configuration macros,
such as `CARDIO_WITH_GLIB`,
`CARDIO_WITH_LINUX_IO_URING`, `CARDIO_HAS_POSIX_FD`, and
`CARDIO_HAS_EXCEPTIONS`. They must also use a compatible C++ ABI. This mode only
shares cardio's runtime thread-local state; it does not make the C++ API a stable plugin ABI.

Before unloading a plugin, stop new work, cancel or complete plugin-owned operations,
and drain pending promises and continuations. A queued coroutine or
callback whose code lives in an unloaded module can no longer be resumed safely.

---

## Exception-Disabled Builds (Advanced Topic)

In environments where C++ exceptions are disabled, such as with gcc's
`-fno-exceptions` option, cardio excludes exception-related features from
compilation.
In this case, `CARDIO_HAS_EXCEPTIONS` becomes `0`:

```bash
g++ -std=c++20 -fno-exceptions -Iinclude app.cpp
```

In exception-disabled builds, features that create failed promises cannot be
used.
Specifically, `rejected()`, `promise_source<T>::reject()`,
`promise_source<T>::try_reject()`, `promise_source<T>::cancel()`,
`promise_source<T>::try_cancel()`,
`cancellation::throw_if_cancellation_requested()`, and
`dispatcher::unhandled_exception()` are not declared.
Also, `try_result()` and `unsafe_result()` do not rethrow failure exceptions.
If an incomplete `promise_source<T>` is destroyed, failure cannot be represented
as a value, so `std::terminate()` is called.

Failures that could previously only be represented as exceptions, such as
library internal initialization failures, invalid arguments, and I/O submit
failures, are consolidated into `std::terminate()`.
If you want to handle errors as values, wrap them in a value type on the caller
side, such as `promise<expected-like-type>`.

Cancellation notifications themselves through `cancellation_source` and
`cancellation` can still be used in exception-disabled builds.
However, APIs that represent cancellation as a `promise<T>` failure are available
only in exception-enabled builds.

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
