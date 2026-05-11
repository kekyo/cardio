// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_WIN32_HANDLE
template <typename T>
concept has_dispatcher_policy_park = requires(T& dispatcher) {
  dispatcher.park(
      cardio::shutdown_mode::gentle,
      cardio::park_policy::continuation_first);
};

template <typename T>
concept has_auto_policy_park = requires(T& dispatcher) {
  dispatcher.park(cardio::park_policy::continuation_first);
};

static_assert(std::is_base_of_v<cardio::dispatcher, cardio::dispatcher_host_win32>);
static_assert(std::is_same_v<cardio::dispatcher_host, cardio::dispatcher_host_win32>);
static_assert(std::is_base_of_v<cardio::dispatcher, cardio::dispatcher_host>);
static_assert(has_dispatcher_policy_park<cardio::dispatcher_host>);
static_assert(has_dispatcher_policy_park<cardio::dispatcher_host_win32>);
static_assert(std::is_base_of_v<cardio::dispatcher, cardio::dispatcher_host_win32_auto>);
static_assert(!has_dispatcher_policy_park<cardio::dispatcher_host_win32_auto>);
static_assert(!has_auto_policy_park<cardio::dispatcher_host_win32_auto>);

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

static bool wait_until_nonzero(const std::atomic<DWORD>& value) {
  for (auto retry = 0; retry < 5000; ++retry) {
    if (value.load(std::memory_order_acquire) != 0) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  return value.load(std::memory_order_acquire) != 0;
}

template <typename Predicate>
static void pump_messages_until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() +
      std::chrono::seconds(5);
  while (!predicate() && std::chrono::steady_clock::now() < deadline) {
    auto message = MSG{};
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
      if (message.message == WM_QUIT) {
        ::PostQuitMessage(static_cast<int>(message.wParam));
        CHECK(false);
      }
      (void)::TranslateMessage(&message);
      (void)::DispatchMessageW(&message);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(predicate());
}

static std::wstring make_pipe_name() {
  static auto counter = std::atomic<unsigned>{0};
  return L"\\\\.\\pipe\\cardio_win32_handle_test_" +
         std::to_wstring(::GetCurrentProcessId()) + L"_" +
         std::to_wstring(counter.fetch_add(1, std::memory_order_relaxed));
}

static HANDLE open_named_pipe_client(const std::wstring& name) {
  auto client = ::CreateFileW(
      name.c_str(),
      GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (client == INVALID_HANDLE_VALUE &&
      ::GetLastError() == ERROR_PIPE_BUSY) {
    CHECK(::WaitNamedPipeW(name.c_str(), 5000) != 0);
    client = ::CreateFileW(
        name.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
  }

  CHECK(client != INVALID_HANDLE_VALUE);
  return client;
}

static void connect_named_pipe(
    HANDLE server,
    const std::wstring& name,
    HANDLE& client) {
  auto connect_event = create_manual_event();
  auto connect_overlapped = OVERLAPPED{};
  connect_overlapped.hEvent = connect_event;

  const auto connect_result = ::ConnectNamedPipe(server, &connect_overlapped);
  CHECK(connect_result == 0);
  const auto connect_error = ::GetLastError();
  if (connect_error == ERROR_IO_PENDING) {
    client = open_named_pipe_client(name);
    auto ignored = DWORD{};
    CHECK(::GetOverlappedResult(
              server, &connect_overlapped, &ignored, TRUE) != 0);
  } else {
    CHECK_EQ(connect_error, static_cast<DWORD>(ERROR_PIPE_CONNECTED));
    client = open_named_pipe_client(name);
    CHECK(::SetEvent(connect_event) != 0);
  }

  close_handle(connect_event);
}

static constexpr auto message_order_id = UINT{WM_APP + 1};

struct message_order_state {
  cardio::dispatcher_group* group;
  int next_order;
  int message_order;
  int queue_order;
};

static const wchar_t* message_order_window_class_name() noexcept {
  return L"cardio_message_order_window";
}

static LRESULT CALLBACK message_order_window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam) {
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
    (void)::SetWindowLongPtrW(
        window,
        GWLP_USERDATA,
        reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    return TRUE;
  }

  if (message == message_order_id) {
    auto* state = reinterpret_cast<message_order_state*>(
        ::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state != nullptr) {
      state->message_order = state->next_order++;
      state->group->shutdown();
    }
    return 0;
  }

  return ::DefWindowProcW(window, message, wparam, lparam);
}

static void register_message_order_window_class() {
  static auto registered = false;
  if (registered) {
    return;
  }

  auto window_class = WNDCLASSW{};
  window_class.lpfnWndProc = message_order_window_proc;
  window_class.hInstance = ::GetModuleHandleW(nullptr);
  window_class.lpszClassName = message_order_window_class_name();

  const auto atom = ::RegisterClassW(&window_class);
  if (atom == 0) {
    CHECK_EQ(::GetLastError(), static_cast<DWORD>(ERROR_CLASS_ALREADY_EXISTS));
  }
  registered = true;
}

static HWND create_message_order_window(message_order_state& state) {
  register_message_order_window_class();
  auto* window = ::CreateWindowExW(
      0,
      message_order_window_class_name(),
      L"",
      0,
      0,
      0,
      0,
      0,
      HWND_MESSAGE,
      nullptr,
      ::GetModuleHandleW(nullptr),
      &state);
  CHECK(window != nullptr);
  return window;
}

//-----------------------------------------------------------------------------------------------

static void handle_event_resolves_after_signal() {
  test_dispatcher_host dispatcher;
  auto event = create_manual_event();

  auto promise = cardio::from_win32_handle(event);
  CHECK(!promise.is_ready());

  CHECK(::SetEvent(event) != 0);
  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK_EQ(promise.unsafe_result(), cardio::win32_handle_event::signaled);

  close_handle(event);
}

static void invalid_handle_creates_rejected_promise() {
  test_dispatcher_host dispatcher;

  auto null_handle = cardio::from_win32_handle(nullptr);
  CHECK(null_handle.is_ready());

  auto caught = false;
  try {
    (void)null_handle.unsafe_result();
  } catch (const std::invalid_argument&) {
    caught = true;
  }

  CHECK(caught);
}

static cardio::promise<void> set_flag_after_dispatch_async(
    std::atomic<bool>& flag) {
  co_await cardio::resolved();
  flag.store(true, std::memory_order_release);
}

static void queued_continuation_wakes_handle_wait_worker() {
  test_dispatcher_host dispatcher;
  auto event = create_manual_event();
  auto ran = std::atomic<bool>{false};

  auto pending = cardio::from_win32_handle(event);
  CHECK(!pending.is_ready());

  auto worker = std::thread([&] { park_current_dispatcher(dispatcher); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  auto queued = set_flag_after_dispatch_async(ran);
  (void)queued;
  const auto ran_before_fallback = wait_until_true(ran);

  CHECK(::SetEvent(event) != 0);
  worker.join();

  CHECK(ran_before_fallback);
  CHECK(pending.is_ready());

  close_handle(event);
}

static void default_policy_pumps_win32_message_before_queue() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  auto state = message_order_state{&group, 0, -1, -1};
  auto* window = create_message_order_window(state);

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    state.queue_order = state.next_order++;
  });
  CHECK(::PostMessageW(window, message_order_id, 0, 0) != 0);

  dispatcher.park();

  CHECK_EQ(state.message_order, 0);
  CHECK_EQ(state.queue_order, 1);
  CHECK(::DestroyWindow(window) != 0);
}

static void continuation_policy_runs_queue_before_win32_message() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  auto state = message_order_state{&group, 0, -1, -1};
  auto* window = create_message_order_window(state);

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    state.queue_order = state.next_order++;
  });
  CHECK(::PostMessageW(window, message_order_id, 0, 0) != 0);

  dispatcher.park(cardio::shutdown_mode::gentle, cardio::park_policy::continuation_first);

  CHECK_EQ(state.queue_order, 0);
  CHECK_EQ(state.message_order, 1);
  CHECK(::DestroyWindow(window) != 0);
}

static cardio::promise<void> win32_inline_order_async(
    message_order_state& state,
    HWND window,
    int& first_order,
    int& second_order) {
  co_await cardio::resolved();
  first_order = state.next_order++;
  CHECK(::PostMessageW(window, message_order_id, 0, 0) != 0);

  co_await cardio::resolved();
  second_order = state.next_order++;
}

static void default_policy_pumps_win32_message_before_inline_continuation() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  auto state = message_order_state{&group, 0, -1, -1};
  auto first_order = -1;
  auto second_order = -1;
  auto* window = create_message_order_window(state);

  auto promise =
      win32_inline_order_async(state, window, first_order, second_order);
  (void)promise;

  dispatcher.park();

  CHECK_EQ(first_order, 0);
  CHECK_EQ(state.message_order, 1);
  CHECK_EQ(second_order, 2);
  CHECK(::DestroyWindow(window) != 0);
}

static void continuation_policy_runs_inline_before_win32_message() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  auto state = message_order_state{&group, 0, -1, -1};
  auto first_order = -1;
  auto second_order = -1;
  auto* window = create_message_order_window(state);

  auto promise =
      win32_inline_order_async(state, window, first_order, second_order);
  (void)promise;

  dispatcher.park(cardio::shutdown_mode::gentle, cardio::park_policy::continuation_first);

  CHECK_EQ(first_order, 0);
  CHECK_EQ(second_order, 1);
  CHECK_EQ(state.message_order, 2);
  CHECK(::DestroyWindow(window) != 0);
}

static void posted_callback_wakes_handle_wait_worker() {
  test_dispatcher_host dispatcher;
  auto event = create_manual_event();
  auto ran = std::atomic<bool>{false};

  auto pending = cardio::from_win32_handle(event);
  CHECK(!pending.is_ready());

  auto worker = std::thread([&] { park_current_dispatcher(dispatcher); });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    ran.store(true, std::memory_order_release);
  });
  const auto ran_before_fallback = wait_until_true(ran);

  CHECK(::SetEvent(event) != 0);
  worker.join();

  CHECK(ran_before_fallback);
  CHECK(pending.is_ready());

  close_handle(event);
}

static void overlapped_read_reports_transferred_bytes() {
  test_dispatcher_host dispatcher;
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
  connect_named_pipe(server, name, client);

  auto event = create_manual_event();
  auto overlapped = OVERLAPPED{};
  overlapped.hEvent = event;
  char buffer[8]{};
  auto immediate_bytes = DWORD{};
  const auto read_result =
      ::ReadFile(server, buffer, 3, &immediate_bytes, &overlapped);
  CHECK(read_result == 0);
  CHECK_EQ(::GetLastError(), static_cast<DWORD>(ERROR_IO_PENDING));

  auto promise = cardio::from_win32_overlapped(server, overlapped);
  CHECK(!promise.is_ready());

  auto written = DWORD{};
  CHECK(::WriteFile(client, "abc", 3, &written, nullptr) != 0);
  CHECK_EQ(written, static_cast<DWORD>(3));

  dispatcher.park();

  CHECK(promise.is_ready());
  const auto result = promise.unsafe_result();
  CHECK_EQ(result.bytes_transferred, static_cast<DWORD>(3));
  CHECK_EQ(buffer[0], 'a');
  CHECK_EQ(buffer[1], 'b');
  CHECK_EQ(buffer[2], 'c');

  close_handle(event);
  close_handle(client);
  close_handle(server);
}

static cardio::promise<void> await_canceled_handle_async(
    cardio::promise<cardio::win32_handle_event>& child,
    int& result) {
  try {
    (void)co_await child;
  } catch (const cardio::canceled_exception&) {
    result = 1;
    co_return;
  }

  result = 0;
}

static void handle_wait_cancellation_resumes_with_canceled_exception() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;
  auto event = create_manual_event();

  auto child =
      cardio::from_win32_handle(event, source.get_cancellation());
  auto parent_result = 0;
  auto parent = await_canceled_handle_async(child, parent_result);
  (void)parent;

  CHECK(source.cancel());
  dispatcher.park();

  CHECK_EQ(parent_result, 1);

  auto caught = false;
  try {
    (void)child.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }

  CHECK(caught);
  close_handle(event);
}

static void message_pump_quit_wakes_manual_dispatcher() {
  cardio::dispatcher_group group(cardio::exit_condition::exit_by_manual);
  test_dispatcher_host dispatcher(group);
  auto thread_id = std::atomic<DWORD>{0};
  auto returned = std::atomic<bool>{false};

  auto worker = std::thread([&] {
    cardio::set_current_dispatcher(&dispatcher);
    auto message = MSG{};
    (void)::PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    thread_id.store(::GetCurrentThreadId(), std::memory_order_release);
    dispatcher.park();
    returned.store(true, std::memory_order_release);
  });

  CHECK(wait_until_nonzero(thread_id));
  CHECK(::PostThreadMessageW(
            thread_id.load(std::memory_order_acquire), WM_QUIT, 0, 0) != 0);
  CHECK(wait_until_true(returned));
  worker.join();
}

//-----------------------------------------------------------------------------------------------

static void auto_dispatcher_external_pump_dispatches_posted_work() {
  cardio::dispatcher_host_win32_auto dispatcher;
  auto ran = false;
  auto dispatcher_matched = false;

  cardio::set_current_dispatcher(nullptr);
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    ran = true;
    dispatcher_matched = &cardio::get_current_dispatcher() == &dispatcher;
  });

  pump_messages_until([&] { return ran; });

  CHECK(dispatcher_matched);
  CHECK(cardio::unsafe_get_current_dispatcher() == nullptr);
}

static cardio::promise<void> auto_delay_async(bool& completed) {
  co_await cardio::promises::delay(5);
  completed = true;
}

static void auto_dispatcher_external_pump_resumes_delay() {
  cardio::dispatcher_host_win32_auto dispatcher;
  auto completed = false;

  cardio::set_current_dispatcher(&dispatcher);
  auto promise = auto_delay_async(completed);

  pump_messages_until([&] { return promise.is_ready(); });

  CHECK(completed);
  promise.unsafe_result();
}

static void auto_dispatcher_external_pump_resolves_handle_wait() {
  cardio::dispatcher_host_win32_auto dispatcher;
  auto event = create_manual_event();

  cardio::set_current_dispatcher(&dispatcher);
  auto promise = cardio::from_win32_handle(event);
  CHECK(!promise.is_ready());

  CHECK(::SetEvent(event) != 0);
  pump_messages_until([&] { return promise.is_ready(); });

  CHECK_EQ(promise.unsafe_result(), cardio::win32_handle_event::signaled);
  close_handle(event);
}

static void auto_dispatcher_external_pump_resolves_overlapped_wait() {
  cardio::dispatcher_host_win32_auto dispatcher;
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
  connect_named_pipe(server, name, client);

  auto event = create_manual_event();
  auto overlapped = OVERLAPPED{};
  overlapped.hEvent = event;
  char buffer[8]{};
  auto immediate_bytes = DWORD{};
  const auto read_result =
      ::ReadFile(server, buffer, 3, &immediate_bytes, &overlapped);
  CHECK(read_result == 0);
  CHECK_EQ(::GetLastError(), static_cast<DWORD>(ERROR_IO_PENDING));

  cardio::set_current_dispatcher(&dispatcher);
  auto promise = cardio::from_win32_overlapped(server, overlapped);
  CHECK(!promise.is_ready());

  auto written = DWORD{};
  CHECK(::WriteFile(client, "abc", 3, &written, nullptr) != 0);
  CHECK_EQ(written, static_cast<DWORD>(3));
  pump_messages_until([&] { return promise.is_ready(); });

  const auto result = promise.unsafe_result();
  CHECK_EQ(result.bytes_transferred, static_cast<DWORD>(3));
  CHECK_EQ(buffer[0], 'a');
  CHECK_EQ(buffer[1], 'b');
  CHECK_EQ(buffer[2], 'c');

  close_handle(event);
  close_handle(client);
  close_handle(server);
}

static void auto_dispatcher_park_rejects_non_owner_thread() {
  cardio::dispatcher_host_win32_auto dispatcher;
  auto caught = std::atomic<bool>{false};

  auto worker = std::thread([&] {
    try {
      dispatcher.park();
    } catch (const std::runtime_error&) {
      caught.store(true, std::memory_order_release);
    }
  });
  worker.join();

  CHECK(caught.load(std::memory_order_acquire));
}

//-----------------------------------------------------------------------------------------------

int main() {
  handle_event_resolves_after_signal();
  invalid_handle_creates_rejected_promise();
  queued_continuation_wakes_handle_wait_worker();
  default_policy_pumps_win32_message_before_queue();
  continuation_policy_runs_queue_before_win32_message();
  default_policy_pumps_win32_message_before_inline_continuation();
  continuation_policy_runs_inline_before_win32_message();
  posted_callback_wakes_handle_wait_worker();
  overlapped_read_reports_transferred_bytes();
  handle_wait_cancellation_resumes_with_canceled_exception();
  message_pump_quit_wakes_manual_dispatcher();
  auto_dispatcher_external_pump_dispatches_posted_work();
  auto_dispatcher_external_pump_resumes_delay();
  auto_dispatcher_external_pump_resolves_handle_wait();
  auto_dispatcher_external_pump_resolves_overlapped_wait();
  auto_dispatcher_park_rejects_non_owner_thread();

  std::puts("cardio_win32_handle_test: PASS");
  return 0;
}
#else
int main() {
  std::puts("cardio_win32_handle_test: SKIP");
  return 0;
}
#endif
