# Supplemental Functions and Async Primitives

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

---

[All guides](../../README.md#documentation) · [日本語](../ja/helpers-and-primitives.md)
