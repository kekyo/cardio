// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <span>
#include <string>
#include <thread>

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_WIN32_HANDLE
static void close_handle(HANDLE& handle) {
  if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
    CHECK(::CloseHandle(handle) != 0);
    handle = nullptr;
  }
}

static HANDLE create_manual_event() {
  auto handle = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  CHECK(handle != nullptr);
  return handle;
}

static bool wait_until_true(const std::atomic<bool>& flag) {
  for (auto retry = 0; retry < 5000; ++retry) {
    if (flag.load(std::memory_order_acquire)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return flag.load(std::memory_order_acquire);
}

static std::wstring make_pipe_name() {
  static auto counter = std::atomic<unsigned>{0};
  return L"\\\\.\\pipe\\cardio_win32_iocp_test_" +
         std::to_wstring(::GetCurrentProcessId()) + L"_" +
         std::to_wstring(counter.fetch_add(1, std::memory_order_relaxed));
}

static HANDLE open_named_pipe_client(
    const std::wstring& name,
    DWORD access,
    DWORD flags) {
  auto client = ::CreateFileW(
      name.c_str(),
      access,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | flags,
      nullptr);
  if (client == INVALID_HANDLE_VALUE &&
      ::GetLastError() == ERROR_PIPE_BUSY) {
    CHECK(::WaitNamedPipeW(name.c_str(), 5000) != 0);
    client = ::CreateFileW(
        name.c_str(),
        access,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | flags,
        nullptr);
  }

  CHECK(client != INVALID_HANDLE_VALUE);
  return client;
}

static void connect_named_pipe(
    HANDLE server,
    const std::wstring& name,
    HANDLE& client,
    DWORD client_access,
    DWORD client_flags) {
  auto connect_event = create_manual_event();
  auto connect_overlapped = OVERLAPPED{};
  connect_overlapped.hEvent = connect_event;

  const auto connect_result = ::ConnectNamedPipe(server, &connect_overlapped);
  CHECK(connect_result == 0);
  const auto connect_error = ::GetLastError();
  if (connect_error == ERROR_IO_PENDING) {
    client = open_named_pipe_client(name, client_access, client_flags);
    auto ignored = DWORD{};
    CHECK(::GetOverlappedResult(
              server, &connect_overlapped, &ignored, TRUE) != 0);
  } else {
    CHECK_EQ(connect_error, static_cast<DWORD>(ERROR_PIPE_CONNECTED));
    client = open_named_pipe_client(name, client_access, client_flags);
    CHECK(::SetEvent(connect_event) != 0);
  }

  close_handle(connect_event);
}

static std::wstring make_temp_file_path() {
  auto directory = std::array<wchar_t, MAX_PATH + 1>{};
  const auto directory_size = ::GetTempPathW(
      static_cast<DWORD>(directory.size()), directory.data());
  CHECK(directory_size != 0);
  CHECK(directory_size < directory.size());

  auto path = std::array<wchar_t, MAX_PATH + 1>{};
  CHECK(::GetTempFileNameW(directory.data(), L"cio", 0, path.data()) != 0);
  CHECK(::DeleteFileW(path.data()) != 0);
  return path.data();
}

static HANDLE create_overlapped_temp_file(std::wstring& path) {
  path = make_temp_file_path();
  auto file = ::CreateFileW(
      path.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OVERLAPPED,
      nullptr);
  CHECK(file != INVALID_HANDLE_VALUE);
  return file;
}

static HANDLE open_sync_reader(const std::wstring& path) {
  auto file = ::CreateFileW(
      path.c_str(),
      GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  CHECK(file != INVALID_HANDLE_VALUE);
  return file;
}

static void delete_file(const std::wstring& path) {
  if (!path.empty()) {
    (void)::DeleteFileW(path.c_str());
  }
}

static void set_overlapped_offset(
    OVERLAPPED& overlapped,
    std::uint64_t offset) {
  overlapped.Offset = static_cast<DWORD>(offset & 0xffffffffULL);
  overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
}

static DWORD wait_overlapped(HANDLE handle, OVERLAPPED& overlapped) {
  auto transferred = DWORD{};
  CHECK(::GetOverlappedResult(handle, &overlapped, &transferred, TRUE) != 0);
  return transferred;
}

static DWORD write_file_at(
    HANDLE file,
    const char* data,
    DWORD size,
    std::uint64_t offset) {
  auto event = create_manual_event();
  auto overlapped = OVERLAPPED{};
  overlapped.hEvent = event;
  set_overlapped_offset(overlapped, offset);

  const auto result =
      ::WriteFile(file, data, size, nullptr, &overlapped);
  if (result == 0) {
    CHECK_EQ(::GetLastError(), static_cast<DWORD>(ERROR_IO_PENDING));
  }

  const auto transferred = wait_overlapped(file, overlapped);
  close_handle(event);
  return transferred;
}

static std::array<char, 8> read_sync_at(
    const std::wstring& path,
    DWORD size,
    LONG distance) {
  auto file = open_sync_reader(path);
  auto offset = LARGE_INTEGER{};
  offset.QuadPart = distance;
  CHECK(::SetFilePointerEx(file, offset, nullptr, FILE_BEGIN) != 0);

  auto buffer = std::array<char, 8>{};
  auto read = DWORD{};
  CHECK(::ReadFile(file, buffer.data(), size, &read, nullptr) != 0);
  CHECK_EQ(read, size);
  close_handle(file);
  return buffer;
}

static void raw_submit_reports_pipe_read_completion() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  const auto name = make_pipe_name();
  auto server = ::CreateNamedPipeW(
      name.c_str(),
      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,
      4096,
      4096,
      0,
      nullptr);
  CHECK(server != INVALID_HANDLE_VALUE);

  auto client = HANDLE{INVALID_HANDLE_VALUE};
  connect_named_pipe(server, name, client, GENERIC_WRITE, 0);

  auto buffer = std::array<char, 8>{};
  auto promise = cardio::iocps::submit(
      port,
      server,
      [&buffer](HANDLE handle, OVERLAPPED& overlapped) {
        if (::ReadFile(
                handle, buffer.data(), 3, nullptr, &overlapped) != 0) {
          return static_cast<DWORD>(ERROR_SUCCESS);
        }
        return ::GetLastError();
      });
  CHECK(!promise.is_ready());

  auto written = DWORD{};
  CHECK(::WriteFile(client, "abc", 3, &written, nullptr) != 0);
  CHECK_EQ(written, static_cast<DWORD>(3));

  dispatcher.park();

  CHECK(promise.is_ready());
  const auto completion = promise.unsafe_result();
  CHECK_EQ(completion.error, static_cast<DWORD>(ERROR_SUCCESS));
  CHECK_EQ(completion.bytes_transferred, static_cast<DWORD>(3));
  CHECK_EQ(buffer[0], 'a');
  CHECK_EQ(buffer[1], 'b');
  CHECK_EQ(buffer[2], 'c');

  close_handle(client);
  close_handle(server);
}

static void read_helper_reads_from_pipe() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  const auto name = make_pipe_name();
  auto server = ::CreateNamedPipeW(
      name.c_str(),
      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,
      4096,
      4096,
      0,
      nullptr);
  CHECK(server != INVALID_HANDLE_VALUE);

  auto client = HANDLE{INVALID_HANDLE_VALUE};
  connect_named_pipe(server, name, client, GENERIC_WRITE, 0);

  auto buffer = std::array<char, 8>{};
  auto promise = cardio::iocps::read(
      port, server, std::as_writable_bytes(std::span<char>(buffer.data(), 4)));
  CHECK(!promise.is_ready());

  auto written = DWORD{};
  CHECK(::WriteFile(client, "read", 4, &written, nullptr) != 0);
  CHECK_EQ(written, static_cast<DWORD>(4));

  dispatcher.park();

  CHECK_EQ(promise.unsafe_result(), static_cast<std::size_t>(4));
  CHECK_EQ(buffer[0], 'r');
  CHECK_EQ(buffer[1], 'e');
  CHECK_EQ(buffer[2], 'a');
  CHECK_EQ(buffer[3], 'd');

  close_handle(client);
  close_handle(server);
}

static void write_helper_writes_to_file() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  auto path = std::wstring{};
  auto file = create_overlapped_temp_file(path);

  const auto payload = std::array<char, 5>{'w', 'r', 'i', 't', 'e'};
  auto promise = cardio::iocps::write(
      port,
      file,
      std::as_bytes(std::span<const char>(payload.data(), payload.size())));

  dispatcher.park();

  CHECK_EQ(promise.unsafe_result(), payload.size());

  const auto read_back = read_sync_at(
      path, static_cast<DWORD>(payload.size()), 0);
  CHECK_EQ(read_back[0], 'w');
  CHECK_EQ(read_back[1], 'r');
  CHECK_EQ(read_back[2], 'i');
  CHECK_EQ(read_back[3], 't');
  CHECK_EQ(read_back[4], 'e');

  close_handle(file);
  delete_file(path);
}

static void read_helper_uses_explicit_offset() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  auto path = std::wstring{};
  auto file = create_overlapped_temp_file(path);

  CHECK_EQ(write_file_at(file, "abcdef", 6, 0), static_cast<DWORD>(6));

  auto buffer = std::array<char, 3>{};
  auto promise = cardio::iocps::read(
      port,
      file,
      std::as_writable_bytes(std::span<char>(buffer.data(), buffer.size())),
      2);

  dispatcher.park();

  CHECK_EQ(promise.unsafe_result(), buffer.size());
  CHECK_EQ(buffer[0], 'c');
  CHECK_EQ(buffer[1], 'd');
  CHECK_EQ(buffer[2], 'e');

  close_handle(file);
  delete_file(path);
}

static void write_helper_uses_explicit_offset() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  auto path = std::wstring{};
  auto file = create_overlapped_temp_file(path);

  CHECK_EQ(write_file_at(file, "abcxyz", 6, 0), static_cast<DWORD>(6));

  const auto payload = std::array<char, 2>{'D', 'E'};
  auto promise = cardio::iocps::write(
      port,
      file,
      std::as_bytes(std::span<const char>(payload.data(), payload.size())),
      3);

  dispatcher.park();

  CHECK_EQ(promise.unsafe_result(), payload.size());
  const auto read_back = read_sync_at(path, 6, 0);
  CHECK_EQ(read_back[0], 'a');
  CHECK_EQ(read_back[1], 'b');
  CHECK_EQ(read_back[2], 'c');
  CHECK_EQ(read_back[3], 'D');
  CHECK_EQ(read_back[4], 'E');
  CHECK_EQ(read_back[5], 'z');

  close_handle(file);
  delete_file(path);
}

static void invalid_handle_is_rejected() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  auto buffer = std::array<char, 1>{};
  auto promise = cardio::iocps::read(
      port, nullptr, std::as_writable_bytes(std::span<char>(buffer.data(), 1)));
  CHECK(promise.is_ready());

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const std::invalid_argument&) {
    caught = true;
  }
  CHECK(caught);
}

static void start_failure_is_rejected() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  auto path = std::wstring{};
  auto file = create_overlapped_temp_file(path);

  auto promise = cardio::iocps::submit(
      port,
      file,
      [](HANDLE, OVERLAPPED&) {
        return static_cast<DWORD>(ERROR_ACCESS_DENIED);
      });
  CHECK(promise.is_ready());

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const std::system_error& error) {
    caught =
        error.code().value() == static_cast<int>(ERROR_ACCESS_DENIED);
  }
  CHECK(caught);

  close_handle(file);
  delete_file(path);
}

static void too_large_buffer_is_rejected() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  auto path = std::wstring{};
  auto file = create_overlapped_temp_file(path);

  auto dummy = std::byte{};
  auto promise = cardio::iocps::read(
      port,
      file,
      std::span<std::byte>(
          &dummy,
          static_cast<std::size_t>(
              (std::numeric_limits<DWORD>::max)()) + 1));
  CHECK(promise.is_ready());

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const std::invalid_argument&) {
    caught = true;
  }
  CHECK(caught);

  close_handle(file);
  delete_file(path);
}

static void already_canceled_submit_does_not_start() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  cardio::cancellation_source source;
  CHECK(source.cancel());

  auto path = std::wstring{};
  auto file = create_overlapped_temp_file(path);
  auto started = false;
  auto promise = cardio::iocps::submit(
      port,
      file,
      [&started](HANDLE, OVERLAPPED&) {
        started = true;
        return static_cast<DWORD>(ERROR_SUCCESS);
      },
      source.get_cancellation());

  CHECK(!started);
  CHECK(promise.is_ready());

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }
  CHECK(caught);

  close_handle(file);
  delete_file(path);
}

static cardio::promise<void> await_canceled_size_async(
    cardio::promise<std::size_t>& child,
    int& result) {
  try {
    (void)co_await child;
  } catch (const cardio::canceled_exception&) {
    result = 1;
    co_return;
  }

  result = 0;
}

static void pending_cancellation_completes_returned_promise() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  cardio::cancellation_source source;
  const auto name = make_pipe_name();
  auto server = ::CreateNamedPipeW(
      name.c_str(),
      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,
      4096,
      4096,
      0,
      nullptr);
  CHECK(server != INVALID_HANDLE_VALUE);

  auto client = HANDLE{INVALID_HANDLE_VALUE};
  connect_named_pipe(server, name, client, GENERIC_WRITE, 0);

  auto buffer = std::array<char, 8>{};
  auto promise = cardio::iocps::read(
      port,
      server,
      std::as_writable_bytes(std::span<char>(buffer.data(), 1)),
      source.get_cancellation());
  CHECK(!promise.is_ready());
  auto parent_result = 0;
  auto parent = await_canceled_size_async(promise, parent_result);
  (void)parent;

  auto returned = std::atomic<bool>{false};
  auto worker = std::thread([&] {
    park_current_dispatcher(dispatcher);
    returned.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(source.cancel());
  CHECK(wait_until_true(returned));
  worker.join();

  CHECK(promise.is_ready());
  CHECK_EQ(parent_result, 1);

  close_handle(client);
  close_handle(server);
}

static void multiple_operations_complete_on_one_port() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  auto path = std::wstring{};
  auto file = create_overlapped_temp_file(path);

  const auto first = std::array<char, 3>{'o', 'n', 'e'};
  const auto second = std::array<char, 3>{'t', 'w', 'o'};
  auto first_p = cardio::iocps::write(
      port,
      file,
      std::as_bytes(std::span<const char>(first.data(), first.size())),
      0);
  auto second_p = cardio::iocps::write(
      port,
      file,
      std::as_bytes(std::span<const char>(second.data(), second.size())),
      3);

  dispatcher.park();

  CHECK_EQ(first_p.unsafe_result(), first.size());
  CHECK_EQ(second_p.unsafe_result(), second.size());
  const auto read_back = read_sync_at(path, 6, 0);
  CHECK_EQ(read_back[0], 'o');
  CHECK_EQ(read_back[1], 'n');
  CHECK_EQ(read_back[2], 'e');
  CHECK_EQ(read_back[3], 't');
  CHECK_EQ(read_back[4], 'w');
  CHECK_EQ(read_back[5], 'o');

  close_handle(file);
  delete_file(path);
}

static cardio::promise<void> await_size_records_thread_async(
    cardio::promise<std::size_t>& child,
    std::atomic<DWORD>& continuation_thread_id,
    std::size_t& result) {
  result = co_await child;
  continuation_thread_id.store(::GetCurrentThreadId(), std::memory_order_release);
}

static void continuation_runs_on_dispatcher_thread() {
  test_dispatcher_host dispatcher;
  cardio::io_completion_port port;
  const auto name = make_pipe_name();
  auto server = ::CreateNamedPipeW(
      name.c_str(),
      PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1,
      4096,
      4096,
      0,
      nullptr);
  CHECK(server != INVALID_HANDLE_VALUE);

  auto client = HANDLE{INVALID_HANDLE_VALUE};
  connect_named_pipe(server, name, client, GENERIC_WRITE, 0);

  auto buffer = std::array<char, 4>{};
  auto promise = cardio::iocps::read(
      port, server, std::as_writable_bytes(std::span<char>(buffer.data(), 4)));
  auto continuation_thread_id = std::atomic<DWORD>{0};
  auto parent_result = std::size_t{};
  auto parent = await_size_records_thread_async(
      promise, continuation_thread_id, parent_result);
  (void)parent;

  auto dispatcher_thread_id = std::atomic<DWORD>{0};
  auto worker = std::thread([&] {
    cardio::set_current_dispatcher(&dispatcher);
    dispatcher_thread_id.store(::GetCurrentThreadId(), std::memory_order_release);
    dispatcher.park();
  });
  while (dispatcher_thread_id.load(std::memory_order_acquire) == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  auto written = DWORD{};
  CHECK(::WriteFile(client, "hop!", 4, &written, nullptr) != 0);
  CHECK_EQ(written, static_cast<DWORD>(4));

  worker.join();

  CHECK_EQ(parent_result, static_cast<std::size_t>(4));
  CHECK_EQ(
      continuation_thread_id.load(std::memory_order_acquire),
      dispatcher_thread_id.load(std::memory_order_acquire));

  close_handle(client);
  close_handle(server);
}

//-----------------------------------------------------------------------------------------------

int main() {
  raw_submit_reports_pipe_read_completion();
  read_helper_reads_from_pipe();
  write_helper_writes_to_file();
  read_helper_uses_explicit_offset();
  write_helper_uses_explicit_offset();
  invalid_handle_is_rejected();
  start_failure_is_rejected();
  too_large_buffer_is_rejected();
  already_canceled_submit_does_not_start();
  pending_cancellation_completes_returned_promise();
  multiple_operations_complete_on_one_port();
  continuation_runs_on_dispatcher_thread();

  std::puts("cardio_win32_iocp_test: PASS");
  return 0;
}
#else
int main() {
  std::puts("cardio_win32_iocp_test: SKIP");
  return 0;
}
#endif
