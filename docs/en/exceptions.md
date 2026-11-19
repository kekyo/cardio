# Exceptions

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

[All guides](../../README.md#documentation) · [日本語](../ja/exceptions.md)
