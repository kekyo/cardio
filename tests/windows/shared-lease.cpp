// Verify legacy HANDLE exclusion across a host and a DLL using libcardio.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
#include "api_probe.h"
#include "test_helpers.h"

#if defined(CARDIO_TEST_PLUGIN)
extern "C" __declspec(dllexport) bool duplicate_is_rejected(HANDLE handle) {
  legacy_api = true;
  auto duplicate = cardio::win32::submit(handle, [](HANDLE, OVERLAPPED&) {
    return DWORD{ERROR_ACCESS_DENIED};
  });
  try {
    (void)duplicate.unsafe_result();
  } catch (const std::system_error& error) {
    return error.code().value() == ERROR_BUSY;
  }
  return false;
}
#else
int main(int argc, char** argv) {
  CHECK_EQ(argc, 2);
  legacy_api = true;
  cardio::dispatcher_host dispatcher(cardio::dispatcher_thread_policy::current_thread);
  auto plugin = ::LoadLibraryA(argv[1]);
  CHECK(plugin != nullptr);
  auto address = ::GetProcAddress(plugin, "duplicate_is_rejected");
  CHECK(address != nullptr);
  using probe = bool (*)(HANDLE);
  probe duplicate_is_rejected = nullptr;
  static_assert(sizeof(address) == sizeof(duplicate_is_rejected));
  std::memcpy(&duplicate_is_rejected, &address, sizeof(address));
  const auto name = L"\\\\.\\pipe\\cardio_shared_lease_" + std::to_wstring(::GetCurrentProcessId());
  auto pipe = ::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
  CHECK(pipe != INVALID_HANDLE_VALUE);
  auto client = ::CreateFileW(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  CHECK(client != INVALID_HANDLE_VALUE);
  auto buffer = std::byte{};
  auto pending = cardio::win32::read(pipe, std::span<std::byte>(&buffer, 1));
  CHECK(duplicate_is_rejected(pipe));
  auto written = DWORD{};
  CHECK(::WriteFile(client, "s", 1, &written, nullptr));
  dispatcher.park();
  CHECK_EQ(pending.unsafe_result(), std::size_t{1});
  CHECK(::CloseHandle(client));
  CHECK(::CloseHandle(pipe));
  CHECK(::FreeLibrary(plugin));
  std::puts("Shared XP HANDLE exclusion: PASS");
}
#endif
