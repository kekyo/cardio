# Cancellation

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

## Handling Completion Races

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

[All guides](../../README.md#documentation) · [日本語](../ja/cancellation.md)
