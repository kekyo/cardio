// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "api_probe.h"
#include "test_helpers.h"

struct pipe_pair {
  HANDLE server;
  HANDLE client;
  pipe_pair() {
    static unsigned sequence = 0;
    const auto name = L"\\\\.\\pipe\\cardio_xp_iocp_" +
        std::to_wstring(::GetCurrentProcessId()) + L"_" + std::to_wstring(++sequence);
    server = ::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
    CHECK(server != INVALID_HANDLE_VALUE);
    client = ::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(client != INVALID_HANDLE_VALUE);
  }
  ~pipe_pair() {
    CHECK(::CloseHandle(client));
    CHECK(::CloseHandle(server));
  }
};

static cardio::promise<void> expect_canceled(
    cardio::promise<cardio::win32_iocp_completion>& pending) {
  auto canceled = false;
  try {
    (void)co_await pending;
  } catch (const cardio::canceled_exception&) {
    canceled = true;
  }
  CHECK(canceled);
}

static cardio::promise<void> expect_error(
    cardio::promise<cardio::win32_iocp_completion>& pending, DWORD expected) {
  auto matched = false;
  try {
    (void)co_await pending;
  } catch (const std::system_error& error) {
    matched = error.code().value() == static_cast<int>(expected);
  }
  CHECK(matched);
}

static void cancellation_and_shutdown_use_issuer(bool shutdown) {
  cardio::dispatcher_host dispatcher;
  pipe_pair pipe;
  auto port = std::make_unique<cardio::io_completion_port>();
  cardio::cancellation_source source;
  auto issued = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(issued != nullptr);
  auto issuing_thread = DWORD{};
  auto buffer = char{};
  auto pending = cardio::iocps::submit(*port, pipe.server,
      [&](HANDLE handle, OVERLAPPED& overlapped) {
        issuing_thread = ::GetCurrentThreadId();
        const auto result = ::ReadFile(handle, &buffer, 1, nullptr, &overlapped);
        const auto error = result ? ERROR_SUCCESS : ::GetLastError();
        CHECK_EQ(error, static_cast<DWORD>(ERROR_IO_PENDING));
        CHECK(::SetEvent(issued));
        return error;
      }, source.get_cancellation());
  CHECK_EQ(::WaitForSingleObject(issued, INFINITE), WAIT_OBJECT_0);
  CHECK(issuing_thread != ::GetCurrentThreadId());
  auto watcher = expect_canceled(pending);
  if (shutdown) {
    port.reset();
  } else {
    CHECK(source.cancel());
  }
  dispatcher.park();
  CHECK(watcher.is_ready());
  CHECK_EQ(cancel_thread.load(), issuing_thread);
  CHECK(::CloseHandle(issued));
}

static void queued_cancellation_and_capacity() {
  cardio::dispatcher_host dispatcher;
  pipe_pair first_pipe;
  pipe_pair second_pipe;
  cardio::io_completion_port port(2);
  auto entered = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  auto release = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(entered != nullptr && release != nullptr);
  // Hold the pump at a deterministic barrier to test queued work. Application
  // starters must return promptly; this block exists only in the test driver.
  auto first = cardio::iocps::submit(port, first_pipe.server,
      [&](HANDLE, OVERLAPPED&) {
        CHECK(::SetEvent(entered));
        CHECK_EQ(::WaitForSingleObject(release, INFINITE), WAIT_OBJECT_0);
        return DWORD{ERROR_ACCESS_DENIED};
      });
  CHECK_EQ(::WaitForSingleObject(entered, INFINITE), WAIT_OBJECT_0);
  cardio::cancellation_source source;
  auto second = cardio::iocps::submit(port, second_pipe.server,
      [](HANDLE, OVERLAPPED&) {
        CHECK(false);
        return DWORD{ERROR_ACCESS_DENIED};
      }, source.get_cancellation());
  auto overflow = cardio::iocps::submit(port, second_pipe.server,
      [](HANDLE, OVERLAPPED&) {
        CHECK(false);
        return DWORD{ERROR_ACCESS_DENIED};
      });
  auto first_watcher = expect_error(first, ERROR_ACCESS_DENIED);
  auto second_watcher = expect_canceled(second);
  auto overflow_watcher = expect_error(overflow, ERROR_NOT_ENOUGH_QUOTA);
  auto unblock = source.get_cancellation().on_cancellation_requested([&] {
    CHECK(::SetEvent(release));
  });
  CHECK(source.cancel());
  dispatcher.park();
  CHECK(first_watcher.is_ready() && second_watcher.is_ready() && overflow_watcher.is_ready());
  CHECK(::CloseHandle(entered));
  CHECK(::CloseHandle(release));
}

static void late_cancellation_preserves_next_operation() {
  cardio::dispatcher_host dispatcher;
  pipe_pair pipe;
  cardio::io_completion_port port;
  cardio::cancellation_source source;
  auto completed = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  auto release = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(completed != nullptr && release != nullptr);
  auto first_buffer = char{};
  auto second_buffer = char{};
  const auto previous_calls = cancel_io_calls.load() + cancel_io_ex_calls.load();
  auto first = port.submit<cardio::win32_iocp_completion>(pipe.server,
      [&](HANDLE handle, OVERLAPPED& overlapped) {
        return ::ReadFile(handle, &first_buffer, 1, nullptr, &overlapped) ?
            DWORD{ERROR_SUCCESS} : ::GetLastError();
      }, [&](cardio::win32_iocp_completion result) {
        CHECK(::SetEvent(completed));
        CHECK_EQ(::WaitForSingleObject(release, INFINITE), WAIT_OBJECT_0);
        return result;
      }, source.get_cancellation());
  auto unblock = source.get_cancellation().on_cancellation_requested([&] {
    CHECK(::SetEvent(release));
    auto written = DWORD{};
    CHECK(::WriteFile(pipe.client, "b", 1, &written, nullptr));
  });
  // Cancellation is queued on the caller dispatcher, then native completion
  // retires the first operation before that dispatcher processes the callback.
  CHECK(source.cancel());
  auto written = DWORD{};
  CHECK(::WriteFile(pipe.client, "a", 1, &written, nullptr));
  CHECK_EQ(::WaitForSingleObject(completed, INFINITE), WAIT_OBJECT_0);
  auto second = cardio::iocps::submit(port, pipe.server,
      [&](HANDLE handle, OVERLAPPED& overlapped) {
        return ::ReadFile(handle, &second_buffer, 1, nullptr, &overlapped) ?
            DWORD{ERROR_SUCCESS} : ::GetLastError();
      });
  dispatcher.park();
  CHECK_EQ(first.unsafe_result().error, DWORD{ERROR_SUCCESS});
  CHECK_EQ(second.unsafe_result().error, DWORD{ERROR_SUCCESS});
  CHECK_EQ(first_buffer, 'a');
  CHECK_EQ(second_buffer, 'b');
  CHECK_EQ(cancel_io_calls.load() + cancel_io_ex_calls.load(), previous_calls);
  CHECK(::CloseHandle(completed));
  CHECK(::CloseHandle(release));
}

static void starter_exception_is_delivered() {
  cardio::dispatcher_host dispatcher;
  pipe_pair pipe;
  cardio::io_completion_port port;
  auto pending = cardio::iocps::submit(port, pipe.server,
      [](HANDLE, OVERLAPPED&) {
        throw std::runtime_error("starter failed");
        return DWORD{ERROR_SUCCESS};
      });
  dispatcher.park();
  auto caught = false;
  try {
    (void)pending.unsafe_result();
  } catch (const std::runtime_error& error) {
    caught = std::strcmp(error.what(), "starter failed") == 0;
  }
  CHECK(caught);
}

static void handles_complete_independently() {
  cardio::dispatcher_host dispatcher;
  pipe_pair first_pipe;
  pipe_pair second_pipe;
  cardio::io_completion_port port;
  cardio::cancellation_source source;
  auto issued = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(issued != nullptr);
  auto first_buffer = char{};
  auto second_buffer = char{};
  auto first = cardio::iocps::submit(port, first_pipe.server,
      [&](HANDLE handle, OVERLAPPED& overlapped) {
        const auto ok = ::ReadFile(handle, &first_buffer, 1, nullptr, &overlapped);
        const auto error = ok ? DWORD{ERROR_SUCCESS} : ::GetLastError();
        CHECK(::SetEvent(issued));
        return error;
      }, source.get_cancellation());
  CHECK_EQ(::WaitForSingleObject(issued, INFINITE), WAIT_OBJECT_0);
  auto second = cardio::iocps::submit(port, second_pipe.server,
      [&](HANDLE handle, OVERLAPPED& overlapped) {
        return ::ReadFile(handle, &second_buffer, 1, nullptr, &overlapped) ?
            DWORD{ERROR_SUCCESS} : ::GetLastError();
      });
  auto duplicate = std::optional<cardio::promise<cardio::win32_iocp_completion>>{};
  auto duplicate_watcher = std::optional<cardio::promise<void>>{};
  if (legacy_api) {
    duplicate.emplace(cardio::iocps::submit(port, first_pipe.server,
        [](HANDLE, OVERLAPPED&) {
          CHECK(false);
          return DWORD{ERROR_ACCESS_DENIED};
        }));
    duplicate_watcher.emplace(expect_error(*duplicate, ERROR_BUSY));
  }
  auto first_watcher = expect_canceled(first);
  CHECK(source.cancel());
  auto written = DWORD{};
  CHECK(::WriteFile(second_pipe.client, "z", 1, &written, nullptr));
  dispatcher.park();
  CHECK(first_watcher.is_ready());
  CHECK_EQ(second.unsafe_result().error, DWORD{ERROR_SUCCESS});
  CHECK_EQ(second_buffer, 'z');
  CHECK(::CloseHandle(issued));
}

int main(int argc, char**) {
  legacy_api = argc > 1;
  std::puts("IOCP: cancellation");
  cancellation_and_shutdown_use_issuer(false);
  std::puts("IOCP: shutdown");
  cancellation_and_shutdown_use_issuer(true);
  std::puts("IOCP: queue capacity");
  queued_cancellation_and_capacity();
  std::puts("IOCP: late cancellation");
  late_cancellation_preserves_next_operation();
  starter_exception_is_delivered();
  handles_complete_independently();
  CHECK(legacy_api ? cancel_io_ex_calls.load() == 0 : cancel_io_calls.load() == 0);
  std::puts(legacy_api ? "IOCP CancelIo fallback: PASS" : "IOCP CancelIoEx: PASS");
}
