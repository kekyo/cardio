# Waiting for Win32 Handles

On Windows, Win32 handles can be awaited as a `promise` when `CARDIO_HAS_WIN32_HANDLE=1` is enabled.
This is the default for `_WIN32` builds.

For Windows XP targets, set `_WIN32_WINNT=0x0501` for i686 or `0x0502` for
amd64, and set `WINVER` to the same value. cardio's synchronization and threading
features also work with the MinGW GCC 12 win32 thread model, including builds
with exceptions disabled.

The tested MinGW GCC 12.2 miscompiles coroutine addresses when an await is used
directly in a condition such as `if (co_await ...)`. First assign its result,
for example `const auto value = co_await operation;`, then test that variable.
See the [standalone reproduction without cardio](../../tests/windows/compiler-await.cpp)
and the [GCC 12.2 coroutine implementation](https://github.com/gcc-mirror/gcc/blob/releases/gcc-12.2.0/gcc/cp/coroutines.cc).

XP-targeted binaries resolve `CancelIoEx()` at runtime using `GetProcAddress()`.
When available, cancellation targets the individual operation; otherwise cardio
calls `CancelIo()` on the thread that issued the I/O. Targets with
`_WIN32_WINNT >= 0x0600` call `CancelIoEx()` directly. These macros describe the
build target, not the OS currently running the executable. See
[Windows header configuration](https://learn.microsoft.com/en-us/windows/win32/winprog/using-the-windows-headers)
and [runtime API lookup](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress).

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

To bind a manual dispatcher to its constructing thread, use the following
constructor. Calling `park()` from another thread then fails. Omitting the
argument retains the default ability to park from multiple threads.

```cpp
cardio::dispatcher_host_win32 dispatcher(
    cardio::dispatcher_thread_policy::current_thread);
```

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
  does not cancel native I/O or close the handle. Cancellation for
  `win32` operations calls the available cancellation API, then completes the returned
  promise after the native operation has completed.
- The Win32 wait backend is subject to the `MAXIMUM_WAIT_OBJECTS` limit. Typically, this value is 64.
  Since the dispatcher's wakeup events and message queue consume wait slots,
  the maximum number of user handles that can wait simultaneously on a single dispatcher is `MAXIMUM_WAIT_OBJECTS - 2`.

On systems without `CancelIoEx()`, call `win32::submit()`, `read()`, and `write()`
on the owner thread of a bound dispatcher. Use `dispatcher_host_win32_auto` or
the `current_thread` policy above. Submitting through an unbound dispatcher
fails with `ERROR_NOT_SUPPORTED` before starting I/O. Destroy unfinished
promises on the owner thread as well, and keep the dispatcher alive until all
operations finish. Use the auto host when cancellation must run inside a modal
GUI loop.

On those systems, only one cardio operation may be outstanding per handle.
Additional submissions fail with `ERROR_BUSY` before starting. Do not issue
external I/O on the same handle: [CancelIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelio)
cancels all pending I/O issued by that thread on that handle. This concurrency
restriction does not apply when `CancelIoEx()` is available at runtime.

Successful cancellation requests do not bound completion time; the driver
determines when I/O finishes. Keep handles, buffers, and `OVERLAPPED` storage
valid until completion. An operation can finish normally when completion races
with cancellation. See the [CancelIoEx contract](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex).

## Win32 I/O Completion Port Helpers

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
  thread starts, cancels, and completes I/O, and promise continuations return
  to the submitting dispatcher.
- `iocps::submit()` can adapt another single-shot `OVERLAPPED` operation. The
  start and completion callbacks run on the pump thread. The starter must return
  promptly after submitting asynchronous I/O; the completion callback receives
  `win32_iocp_completion`.
- The handle and buffers are not owned and must remain valid until completion.
- Do not use the same handle with `from_win32_overlapped()` or another
  completion port. IOCP helpers associate the handle with their completion port.
  Keep associated handles open while using the port, because it retains their associations.

IOCP also performs the `CancelIo()` fallback on its pump thread, so the submitting
dispatcher need not be bound to a thread. The same restriction on outstanding
operations per handle applies as for event-based helpers. Keep the registering
dispatcher running to deliver cancellation notifications. Starting an operation
is queued to the pump thread, so start failures may also be reported asynchronously.

The default capacity is 256 queued and in-flight operations combined. Set a
different capacity when constructing the port, for example
`cardio::io_completion_port port(64);`. Excess submissions fail with
`ERROR_NOT_ENOUGH_QUOTA` without starting I/O. Neither callback should block the
pump thread, and a starter must not throw after issuing native I/O. Destroy the
port outside these callbacks. Destruction cancels outstanding I/O, collects its
completions, and joins the pump thread. It continues waiting if the driver does
not finish cancellation.

---

[All guides](../../README.md#documentation) · [日本語](../ja/win32.md)
