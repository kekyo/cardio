# Waiting for POSIX File Descriptors

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

[All guides](../../README.md#documentation) · [日本語](../ja/posix-fd.md)
