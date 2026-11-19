# Working With Promises and Their Lifetimes

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

[All guides](../../README.md#documentation) · [日本語](../ja/promises.md)
