// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

static cardio::promise<void> release_held(
    cardio::promise<cardio::win32_handle_event>& ready_wait,
    cardio::promise<cardio::win32_handle_event>& held_wait,
    HANDLE held) {
  const auto event = co_await ready_wait;
  CHECK_EQ(event, cardio::win32_handle_event::signaled);
  CHECK(!held_wait.is_ready());
  CHECK(::SetEvent(held));
}

int main() {
  cardio::dispatcher_host dispatcher(cardio::dispatcher_thread_policy::current_thread);
  auto held = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  auto ready = ::CreateEventW(nullptr, TRUE, TRUE, nullptr);
  CHECK(held != nullptr && ready != nullptr);
  auto held_wait = cardio::from_win32_handle(held);
  auto ready_wait = cardio::from_win32_handle(ready);
  auto task = release_held(ready_wait, held_wait, held);
  dispatcher.park();
  CHECK(task.is_ready());
  CHECK_EQ(held_wait.unsafe_result(), cardio::win32_handle_event::signaled);
  CHECK(::CloseHandle(ready));
  CHECK(::CloseHandle(held));

  const auto name = L"\\\\.\\pipe\\cardio_native_" + std::to_wstring(::GetCurrentProcessId());
  auto pipe = ::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
  CHECK(pipe != INVALID_HANDLE_VALUE);
  auto client = ::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(client != INVALID_HANDLE_VALUE);
  cardio::io_completion_port port;
  auto buffer = char{};
  auto read = port.submit<cardio::win32_iocp_completion>(pipe,
      [&](HANDLE handle, OVERLAPPED& overlapped) {
        return ::ReadFile(handle, &buffer, 1, nullptr, &overlapped) ?
            DWORD{ERROR_SUCCESS} : ::GetLastError();
      }, [](cardio::win32_iocp_completion result) { return result; });
  auto written = DWORD{};
  CHECK(::WriteFile(client, "n", 1, &written, nullptr));
  dispatcher.park();
  CHECK_EQ(read.unsafe_result().error, DWORD{ERROR_SUCCESS});
  CHECK_EQ(read.unsafe_result().bytes_transferred, DWORD{1});
  CHECK_EQ(buffer, 'n');
  CHECK(::CloseHandle(client));
  CHECK(::CloseHandle(pipe));
  std::puts("Windows native/no-exceptions: PASS");
}
