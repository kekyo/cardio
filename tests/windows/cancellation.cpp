// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "api_probe.h"
#include "test_helpers.h"
#include <array>

static unsigned __stdcall cancel_from_thread(void* argument) {
  auto& source = *static_cast<cardio::cancellation_source*>(argument);
  CHECK(source.cancel());
  return 0;
}

static cardio::promise<void> expect_canceled(cardio::promise<std::size_t>& pending) {
  auto canceled = false;
  try {
    (void)co_await pending;
  } catch (const cardio::canceled_exception&) {
    canceled = true;
  }
  CHECK(canceled);
}

static unsigned __stdcall try_foreign_park(void* argument) {
  auto& dispatcher = *static_cast<cardio::dispatcher_host*>(argument);
  CHECK(!dispatcher.is_current_thread_owner());
  auto rejected = false;
  try {
    dispatcher.park();
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  CHECK(rejected);
  return 0;
}

static void thread_binding_is_enforced() {
  cardio::dispatcher_host dispatcher(cardio::dispatcher_thread_policy::current_thread);
  CHECK(dispatcher.is_current_thread_owner());
  auto thread = reinterpret_cast<HANDLE>(::_beginthreadex(
      nullptr, 0, try_foreign_park, &dispatcher, 0, nullptr));
  CHECK(thread != nullptr);
  CHECK_EQ(::WaitForSingleObject(thread, INFINITE), WAIT_OBJECT_0);
  CHECK(::CloseHandle(thread));
}

struct modal_context {
  cardio::cancellation_source* source;
  cardio::promise<std::size_t>* pending;
  std::optional<cardio::promise<void>> watcher;
  HANDLE thread = nullptr;
};

static cardio::promise<void> finish_dialog(
    HWND dialog, cardio::promise<std::size_t>& pending) {
  co_await expect_canceled(pending);
  CHECK(::EndDialog(dialog, IDOK));
}

static INT_PTR CALLBACK modal_proc(HWND dialog, UINT message, WPARAM, LPARAM parameter) {
  if (message == WM_INITDIALOG) {
    auto& context = *reinterpret_cast<modal_context*>(parameter);
    context.watcher.emplace(finish_dialog(dialog, *context.pending));
    context.thread = reinterpret_cast<HANDLE>(::_beginthreadex(
        nullptr, 0, cancel_from_thread, context.source, 0, nullptr));
    CHECK(context.thread != nullptr);
    return TRUE;
  }
  return FALSE;
}

static void cancel_in_modal_loop() {
  cardio::dispatcher_host_win32_auto dispatcher;
  const auto name = L"\\\\.\\pipe\\cardio_xp_modal_" + std::to_wstring(::GetCurrentProcessId());
  auto pipe = ::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
  CHECK(pipe != INVALID_HANDLE_VALUE);
  auto client = ::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(client != INVALID_HANDLE_VALUE);
  cardio::cancellation_source source;
  std::array<std::byte, 1> buffer{};
  auto pending = cardio::win32::read(pipe, buffer, source.get_cancellation());
  auto context = modal_context{&source, &pending, {}, nullptr};
  struct dialog_template {
    DLGTEMPLATE dialog;
    WORD menu;
    WORD window_class;
    WORD title;
  } layout{};
  layout.dialog.style = WS_POPUP | WS_CAPTION | DS_MODALFRAME;
  layout.dialog.cx = 100;
  layout.dialog.cy = 40;
  CHECK_EQ(::DialogBoxIndirectParamW(::GetModuleHandleW(nullptr), &layout.dialog,
      nullptr, modal_proc, reinterpret_cast<LPARAM>(&context)), IDOK);
  CHECK_EQ(::WaitForSingleObject(context.thread, INFINITE), WAIT_OBJECT_0);
  CHECK(::CloseHandle(context.thread));
  CHECK(context.watcher->is_ready());
  CHECK_EQ(cancel_thread.load(), ::GetCurrentThreadId());
  CHECK(::CloseHandle(client));
  CHECK(::CloseHandle(pipe));
}

static void cancel_and_reuse() {
  cardio::dispatcher_host dispatcher(cardio::dispatcher_thread_policy::current_thread);
  const auto owner = ::GetCurrentThreadId();
  const auto name = L"\\\\.\\pipe\\cardio_xp_cancel_" + std::to_wstring(::GetCurrentProcessId());
  auto pipe = ::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
  CHECK(pipe != INVALID_HANDLE_VALUE);
  auto client = ::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(client != INVALID_HANDLE_VALUE);
  cardio::cancellation_source source;
  std::array<std::byte, 1> buffer{};
  auto pending = cardio::win32::read(pipe, buffer, source.get_cancellation());
  CHECK(!pending.is_ready());

  if (legacy_api) {
    auto duplicate = cardio::win32::read(pipe, buffer);
    CHECK(duplicate.is_ready());
    auto busy = false;
    try {
      (void)duplicate.unsafe_result();
    } catch (const std::system_error& error) {
      busy = error.code().value() == ERROR_BUSY;
    }
    CHECK(busy);
  }

  auto watcher = expect_canceled(pending);
  auto thread = reinterpret_cast<HANDLE>(::_beginthreadex(
      nullptr, 0, cancel_from_thread, &source, 0, nullptr));
  CHECK(thread != nullptr);
  dispatcher.park();
  CHECK_EQ(::WaitForSingleObject(thread, INFINITE), WAIT_OBJECT_0);
  CHECK(::CloseHandle(thread));
  CHECK(watcher.is_ready());
  CHECK_EQ(cancel_thread.load(), owner);
  CHECK_EQ(cancel_io_calls.load(), legacy_api ? 1U : 0U);
  CHECK_EQ(cancel_io_ex_calls.load(), legacy_api ? 0U : 1U);

  auto next = cardio::win32::read(pipe, buffer);
  auto written = DWORD{};
  CHECK(::WriteFile(client, "x", 1, &written, nullptr));
  dispatcher.park();
  CHECK_EQ(next.unsafe_result(), std::size_t{1});
  CHECK_EQ(buffer[0], std::byte{'x'});
  CHECK(::CloseHandle(client));
  CHECK(::CloseHandle(pipe));
}

int main(int argc, char**) {
  legacy_api = argc > 1;
  thread_binding_is_enforced();
  cancel_and_reuse();
  cancel_in_modal_loop();
  std::puts(legacy_api ? "CancelIo fallback: PASS" : "CancelIoEx runtime: PASS");
}
