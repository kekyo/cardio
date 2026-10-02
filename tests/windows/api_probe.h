// Test the real Windows cancellation APIs while controlling API availability.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>

static bool legacy_api = false;
static std::atomic<unsigned> cancel_io_calls{0};
static std::atomic<unsigned> cancel_io_ex_calls{0};
static std::atomic<DWORD> cancel_thread{0};
using cancel_io_ex_function = BOOL (WINAPI*)(HANDLE, LPOVERLAPPED);

static BOOL WINAPI probe_cancel_io(HANDLE handle) {
  cancel_thread.store(::GetCurrentThreadId());
  ++cancel_io_calls;
  return ::CancelIo(handle);
}

static BOOL WINAPI probe_cancel_io_ex(HANDLE handle, LPOVERLAPPED overlapped) {
  cancel_thread.store(::GetCurrentThreadId());
  ++cancel_io_ex_calls;
  auto address = ::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "CancelIoEx");
  cancel_io_ex_function function = nullptr;
  static_assert(sizeof(function) == sizeof(address));
  std::memcpy(&function, &address, sizeof(function));
  return function(handle, overlapped);
}

[[maybe_unused]] static FARPROC WINAPI probe_get_proc_address(HMODULE module, LPCSTR name) {
  if (std::strcmp(name, "CancelIoEx") == 0) {
    if (legacy_api) {
      return nullptr;
    }
    auto function = &probe_cancel_io_ex;
    FARPROC address = nullptr;
    std::memcpy(&address, &function, sizeof(address));
    return address;
  }
  return ::GetProcAddress(module, name);
}

#define GetProcAddress probe_get_proc_address
#define CancelIo probe_cancel_io
#define CancelIoEx probe_cancel_io_ex
#include "cardio.h"
#undef GetProcAddress
#undef CancelIo
#undef CancelIoEx
