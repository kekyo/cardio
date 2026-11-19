# Implementing Asynchronous I/O With Linux io_uring

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
  operations. See [Supplemental Functions](./helpers-and-primitives.md#supplemental-functions).
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

## Implementing Additional io_uring Operations

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

[All guides](../../README.md#documentation) · [日本語](../ja/linux-io-uring.md)
