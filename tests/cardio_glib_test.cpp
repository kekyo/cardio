// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <glib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <optional>
#include <thread>
#include <type_traits>

#include <unistd.h>

//-----------------------------------------------------------------------------------------------

template <typename T>
concept has_dispatcher_park = requires(T& dispatcher) {
  dispatcher.park();
};

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

static_assert(std::is_base_of_v<cardio::dispatcher, cardio::dispatcher_host_glib>);
static_assert(has_dispatcher_park<cardio::dispatcher_host_glib>);
static_assert(has_dispatcher_policy_park<cardio::dispatcher_host_glib>);
static_assert(std::is_base_of_v<cardio::dispatcher, cardio::dispatcher_host_glib_auto>);
static_assert(has_dispatcher_park<cardio::dispatcher_host_glib_auto>);
static_assert(!has_dispatcher_policy_park<cardio::dispatcher_host_glib_auto>);
static_assert(!has_auto_policy_park<cardio::dispatcher_host_glib_auto>);

//-----------------------------------------------------------------------------------------------

static void wait_until_true(const std::atomic<bool>& flag) {
  for (auto retry = 0; retry < 5000; ++retry) {
    if (flag.load(std::memory_order_acquire)) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  CHECK(flag.load(std::memory_order_acquire));
}

static void make_pipe(int (&fds)[2]) {
  CHECK_EQ(::pipe(fds), 0);
}

static void close_fd(int& fd) {
  if (fd >= 0) {
    CHECK_EQ(::close(fd), 0);
    fd = -1;
  }
}

static void write_byte(int fd) {
  const auto value = char{1};
  CHECK_EQ(::write(fd, &value, 1), 1);
}

struct glib_poll_probe_state {
  int read_fd;
  int write_fd;
  GPollFunc delegate;
  unsigned int self_wake_count = 0;
  bool observed_quiet_wait = false;
  bool data_written = false;
};

static glib_poll_probe_state* active_glib_poll_probe = nullptr;

static gint probe_glib_poll(GPollFD* fds, guint fd_count, gint timeout) {
  auto* probe = active_glib_poll_probe;
  if (probe == nullptr) {
    return g_poll(fds, fd_count, timeout);
  }

  auto monitors_target = false;
  for (auto index = guint{0}; index < fd_count; ++index) {
    if (fds[index].fd == probe->read_fd) {
      monitors_target = true;
      break;
    }
  }
  if (!monitors_target) {
    return probe->delegate(fds, fd_count, timeout);
  }

  // A zero-time probe deterministically distinguishes an internal self-wakeup
  // from the quiet wait that would otherwise block for the target fd.
  const auto result = probe->delegate(fds, fd_count, 0);
  auto target_ready = false;
  for (auto index = guint{0}; index < fd_count; ++index) {
    if (fds[index].fd == probe->read_fd &&
        (fds[index].revents & G_IO_IN) != 0) {
      target_ready = true;
      break;
    }
  }
  if (target_ready) {
    return result;
  }

  if (result == 0) {
    probe->observed_quiet_wait = true;
    write_byte(probe->write_fd);
    probe->data_written = true;
    return probe->delegate(fds, fd_count, 0);
  }

  ++probe->self_wake_count;
  if (probe->self_wake_count >= 4 && !probe->data_written) {
    // Bound the regression path so a broken dispatcher fails without hanging.
    write_byte(probe->write_fd);
    probe->data_written = true;
  }
  return result;
}

static GMainContext* new_test_context() {
  return g_main_context_new();
}

static void attach_idle(
    cardio::dispatcher_group_glib& group,
    GSourceFunc callback,
    gpointer data) {
  auto* source = g_idle_source_new();
  g_source_set_callback(source, callback, data, nullptr);
  g_source_attach(source, group.context());
  g_source_unref(source);
}

//-----------------------------------------------------------------------------------------------

static void glib_idle_dispatches_and_shutdown_returns() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  auto idle_ran = false;

  struct state {
    cardio::dispatcher_group_glib* group;
    bool* idle_ran;
  } callback_state{&group, &idle_ran};

  attach_idle(
      group,
      [](gpointer data) -> gboolean {
        auto* current = static_cast<state*>(data);
        *current->idle_ran = true;
        current->group->shutdown();
        return G_SOURCE_REMOVE;
      },
      &callback_state);

  dispatcher.park();

  CHECK(idle_ran);
}

static void default_policy_dispatches_glib_idle_before_queue() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  auto next_order = 0;
  auto idle_order = -1;
  auto queue_order = -1;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    queue_order = next_order++;
    group.shutdown();
  });

  struct state {
    int* next_order;
    int* idle_order;
  } callback_state{&next_order, &idle_order};

  attach_idle(
      group,
      [](gpointer data) -> gboolean {
        auto* current = static_cast<state*>(data);
        *current->idle_order = (*current->next_order)++;
        return G_SOURCE_REMOVE;
      },
      &callback_state);

  dispatcher.park();

  CHECK_EQ(idle_order, 0);
  CHECK_EQ(queue_order, 1);
}

static void continuation_policy_dispatches_queue_before_glib_idle() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  auto next_order = 0;
  auto idle_order = -1;
  auto queue_order = -1;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    queue_order = next_order++;
    group.shutdown();
  });

  struct state {
    int* next_order;
    int* idle_order;
  } callback_state{&next_order, &idle_order};

  attach_idle(
      group,
      [](gpointer data) -> gboolean {
        auto* current = static_cast<state*>(data);
        *current->idle_order = (*current->next_order)++;
        return G_SOURCE_REMOVE;
      },
      &callback_state);

  dispatcher.park(cardio::shutdown_mode::gentle, cardio::park_policy::continuation_first);

  CHECK_EQ(queue_order, 0);
  CHECK_EQ(idle_order, 1);
}

struct glib_inline_order_state {
  int* next_order;
  int* idle_order;
};

static cardio::promise<void> glib_inline_order_async(
    cardio::dispatcher_group_glib& group,
    int& next_order,
    int& first_order,
    int& second_order,
    glib_inline_order_state& callback_state) {
  co_await cardio::resolved();
  first_order = next_order++;

  attach_idle(
      group,
      [](gpointer data) -> gboolean {
        auto* current = static_cast<glib_inline_order_state*>(data);
        *current->idle_order = (*current->next_order)++;
        return G_SOURCE_REMOVE;
      },
      &callback_state);

  co_await cardio::resolved();
  second_order = next_order++;
  group.shutdown();
}

static void default_policy_dispatches_glib_idle_before_inline_continuation() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  auto next_order = 0;
  auto first_order = -1;
  auto idle_order = -1;
  auto second_order = -1;
  auto callback_state = glib_inline_order_state{&next_order, &idle_order};

  auto promise = glib_inline_order_async(
      group, next_order, first_order, second_order, callback_state);
  (void)promise;

  dispatcher.park();

  CHECK_EQ(first_order, 0);
  CHECK_EQ(idle_order, 1);
  CHECK_EQ(second_order, 2);
}

static void continuation_policy_dispatches_inline_before_glib_idle() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  auto next_order = 0;
  auto first_order = -1;
  auto idle_order = -1;
  auto second_order = -1;
  auto callback_state = glib_inline_order_state{&next_order, &idle_order};

  auto promise = glib_inline_order_async(
      group, next_order, first_order, second_order, callback_state);
  (void)promise;

  dispatcher.park(cardio::shutdown_mode::gentle, cardio::park_policy::continuation_first);

  CHECK_EQ(first_order, 0);
  CHECK_EQ(second_order, 1);
  CHECK_EQ(idle_order, 2);
}

static void glib_auto_park_dispatches_posted_work() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  auto ran = false;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    ran = true;
    group.shutdown();
  });

  dispatcher.park();
  CHECK(ran);

  g_main_context_unref(context);
}

static void external_loop_dispatches_posted_work_with_temporary_tls() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  auto* loop = g_main_loop_new(group.context(), FALSE);
  auto ran = false;
  auto dispatcher_matched = false;
  auto context_matched = false;

  cardio::set_current_dispatcher(nullptr);
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    ran = true;
    dispatcher_matched = &cardio::get_current_dispatcher() == &dispatcher;
    context_matched = g_main_context_get_thread_default() == group.context();
    g_main_loop_quit(loop);
  });

  g_main_loop_run(loop);

  CHECK(ran);
  CHECK(dispatcher_matched);
  CHECK(context_matched);
  CHECK(cardio::unsafe_get_current_dispatcher() == nullptr);

  g_main_loop_unref(loop);
}

static void external_loop_preserves_existing_matching_tls_and_context() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  auto* loop = g_main_loop_new(group.context(), FALSE);
  auto ran = false;
  auto dispatcher_matched = false;
  auto context_matched = false;

  cardio::set_current_dispatcher(&dispatcher);
  g_main_context_push_thread_default(group.context());
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    ran = true;
    dispatcher_matched = cardio::unsafe_get_current_dispatcher() == &dispatcher;
    context_matched = g_main_context_get_thread_default() == group.context();
    g_main_loop_quit(loop);
  });

  g_main_loop_run(loop);

  CHECK(ran);
  CHECK(dispatcher_matched);
  CHECK(context_matched);
  CHECK(cardio::unsafe_get_current_dispatcher() == &dispatcher);
  CHECK(g_main_context_get_thread_default() == group.context());

  g_main_context_pop_thread_default(group.context());
  g_main_loop_unref(loop);
}

static void external_loop_reports_mismatched_tls() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  cardio::dispatcher_group other_group;
  test_dispatcher_host other_dispatcher(other_group);
  auto* loop = g_main_loop_new(group.context(), FALSE);
  auto caught = false;
  auto work_ran = false;

  dispatcher.unhandled_exception([&](std::exception_ptr) {
    caught = true;
    g_main_loop_quit(loop);
  });
  cardio::set_current_dispatcher(&other_dispatcher);
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] { work_ran = true; });

  g_main_loop_run(loop);

  CHECK(caught);
  CHECK(!work_ran);

  cardio::set_current_dispatcher(nullptr);
  g_main_loop_unref(loop);
}

static void external_loop_reports_mismatched_thread_default_context() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  auto* other_context = new_test_context();
  auto* loop = g_main_loop_new(group.context(), FALSE);
  auto caught = false;
  auto work_ran = false;

  dispatcher.unhandled_exception([&](std::exception_ptr) {
    caught = true;
    g_main_loop_quit(loop);
  });
  cardio::set_current_dispatcher(&dispatcher);
  g_main_context_push_thread_default(other_context);
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] { work_ran = true; });

  g_main_loop_run(loop);

  CHECK(caught);
  CHECK(!work_ran);

  g_main_context_pop_thread_default(other_context);
  g_main_context_unref(other_context);
  g_main_loop_unref(loop);
}

//-----------------------------------------------------------------------------------------------

static void glib_group_can_host_regular_and_glib_hosts() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto glib_dispatcher(group);
  test_dispatcher_host worker_dispatcher(group);
  auto idle_ran = false;
  auto worker_ran = std::atomic<bool>{false};

  auto worker = std::thread([&] {
    cardio::set_current_dispatcher(&worker_dispatcher);
    worker_dispatcher.park();
  });

  struct state {
    cardio::dispatcher* worker_dispatcher;
    cardio::dispatcher_group_glib* group;
    bool* idle_ran;
    std::atomic<bool>* worker_ran;
  } callback_state{&worker_dispatcher, &group, &idle_ran, &worker_ran};

  attach_idle(
      group,
      [](gpointer data) -> gboolean {
        auto* current = static_cast<state*>(data);
        *current->idle_ran = true;
        cardio::internal::dangerous_schedule_later__(
            current->worker_dispatcher,
            [current] {
              current->worker_ran->store(true, std::memory_order_release);
              current->group->shutdown();
            });
        return G_SOURCE_REMOVE;
      },
      &callback_state);

  glib_dispatcher.park();
  worker.join();

  CHECK(idle_ran);
  CHECK(worker_ran.load(std::memory_order_acquire));
}

static cardio::promise<void> switch_between_glib_and_regular_dispatchers_async(
    cardio::dispatcher_group_glib& group,
    cardio::dispatcher& glib_dispatcher,
    cardio::dispatcher& worker_dispatcher,
    GMainLoop* loop,
    std::thread::id& initial_thread_id,
    std::thread::id& worker_resume_thread_id,
    std::thread::id& glib_resume_thread_id) {
  initial_thread_id = std::this_thread::get_id();
  CHECK(&cardio::get_current_dispatcher() == &glib_dispatcher);

  co_await cardio::switch_to(worker_dispatcher);
  worker_resume_thread_id = std::this_thread::get_id();
  CHECK(&cardio::get_current_dispatcher() == &worker_dispatcher);

  co_await cardio::switch_to(glib_dispatcher);
  glib_resume_thread_id = std::this_thread::get_id();
  CHECK(&cardio::get_current_dispatcher() == &glib_dispatcher);

  group.shutdown();
  g_main_loop_quit(loop);
}

static void switch_to_roundtrips_between_glib_and_regular_dispatchers() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto glib_dispatcher(group);
  test_dispatcher_host worker_dispatcher(group);
  auto* loop = g_main_loop_new(group.context(), FALSE);
  auto worker_ready = std::atomic<bool>{false};
  auto worker_thread_id = std::thread::id{};
  auto initial_thread_id = std::thread::id{};
  auto worker_resume_thread_id = std::thread::id{};
  auto glib_resume_thread_id = std::thread::id{};
  auto promise = std::optional<cardio::promise<void>>{};
  const auto glib_thread_id = std::this_thread::get_id();

  auto worker = std::thread([&] {
    cardio::set_current_dispatcher(&worker_dispatcher);
    worker_thread_id = std::this_thread::get_id();
    worker_ready.store(true, std::memory_order_release);
    worker_dispatcher.park();
  });
  wait_until_true(worker_ready);

  cardio::set_current_dispatcher(nullptr);
  cardio::internal::dangerous_schedule_later__(&glib_dispatcher, [&] {
    promise.emplace(
        switch_between_glib_and_regular_dispatchers_async(
            group,
            glib_dispatcher,
            worker_dispatcher,
            loop,
            initial_thread_id,
            worker_resume_thread_id,
            glib_resume_thread_id));
  });

  g_main_loop_run(loop);
  worker.join();

  CHECK(promise.has_value());
  CHECK(promise->is_ready());
  promise->unsafe_result();
  CHECK_EQ(initial_thread_id, glib_thread_id);
  CHECK_EQ(worker_resume_thread_id, worker_thread_id);
  CHECK_EQ(glib_resume_thread_id, glib_thread_id);

  g_main_loop_unref(loop);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> wait_for_read_async(
    int fd,
    cardio::fd_event& result,
    bool& completed) {
  result = co_await cardio::from_fd(fd, cardio::fd_event::read);
  completed = true;
}

static cardio::promise<void> wait_for_read_and_quit_async(
    int fd,
    cardio::fd_event& result,
    bool& completed,
    GMainLoop* loop) {
  result = co_await cardio::from_fd(fd, cardio::fd_event::read);
  completed = true;
  g_main_loop_quit(loop);
}

static cardio::promise<void> delay_and_quit_async(
    bool& completed,
    GMainLoop* loop) {
  co_await cardio::promises::delay(5);
  completed = true;
  g_main_loop_quit(loop);
}

static void external_loop_can_resume_delay() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  auto* loop = g_main_loop_new(group.context(), FALSE);
  auto completed = false;
  auto promise = std::optional<cardio::promise<void>>{};

  cardio::set_current_dispatcher(nullptr);
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    promise.emplace(delay_and_quit_async(completed, loop));
  });

  g_main_loop_run(loop);

  CHECK(completed);
  CHECK(promise.has_value());
  CHECK(promise->is_ready());

  g_main_loop_unref(loop);
}

static void external_loop_can_resume_fd_wait() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  auto* loop = g_main_loop_new(group.context(), FALSE);
  int fds[2]{-1, -1};
  make_pipe(fds);

  cardio::set_current_dispatcher(nullptr);
  auto result = cardio::fd_event::none;
  auto completed = false;
  auto promise = std::optional<cardio::promise<void>>{};

  struct state {
    int write_fd;
  } callback_state{fds[1]};

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    promise.emplace(
        wait_for_read_and_quit_async(fds[0], result, completed, loop));
  });

  attach_idle(
      group,
      [](gpointer data) -> gboolean {
        auto* current = static_cast<state*>(data);
        write_byte(current->write_fd);
        return G_SOURCE_REMOVE;
      },
      &callback_state);

  g_main_loop_run(loop);

  CHECK(completed);
  CHECK(promise.has_value());
  CHECK(promise->is_ready());
  CHECK((result & cardio::fd_event::read) != cardio::fd_event::none);

  close_fd(fds[0]);
  close_fd(fds[1]);
  g_main_loop_unref(loop);
}

static void external_loop_fd_wait_does_not_self_wake() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  auto* loop = g_main_loop_new(group.context(), FALSE);
  int fds[2]{-1, -1};
  make_pipe(fds);

  cardio::set_current_dispatcher(nullptr);
  auto result = cardio::fd_event::none;
  auto completed = false;
  auto promise = std::optional<cardio::promise<void>>{};
  auto probe = glib_poll_probe_state{
      .read_fd = fds[0],
      .write_fd = fds[1],
      .delegate = g_main_context_get_poll_func(group.context()),
  };
  active_glib_poll_probe = &probe;
  g_main_context_set_poll_func(group.context(), probe_glib_poll);

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    promise.emplace(
        wait_for_read_and_quit_async(fds[0], result, completed, loop));
  });

  g_main_loop_run(loop);

  g_main_context_set_poll_func(group.context(), probe.delegate);
  active_glib_poll_probe = nullptr;
  CHECK(completed);
  CHECK(promise.has_value());
  CHECK(promise->is_ready());
  CHECK((result & cardio::fd_event::read) != cardio::fd_event::none);
  CHECK(probe.observed_quiet_wait);

  close_fd(fds[0]);
  close_fd(fds[1]);
  g_main_loop_unref(loop);
}

//-----------------------------------------------------------------------------------------------

static void immediate_shutdown_skips_pending_dispatcher_work() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  auto first_ran = false;
  auto second_ran = false;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    first_ran = true;
    group.shutdown();
  });
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] { second_ran = true; });

  dispatcher.park(cardio::shutdown_mode::unsafe_immediate);

  CHECK(first_ran);
  CHECK(!second_ran);
}

static void immediate_shutdown_from_thread_skips_pending_fd_wait() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  int fds[2]{-1, -1};
  make_pipe(fds);
  auto returned = std::atomic<bool>{false};

  cardio::set_current_dispatcher(&dispatcher);
  auto result = cardio::fd_event::none;
  auto completed = false;
  auto promise = wait_for_read_async(fds[0], result, completed);
  (void)promise;

  auto worker = std::thread([&] {
    cardio::set_current_dispatcher(&dispatcher);
    dispatcher.park(cardio::shutdown_mode::unsafe_immediate);
    returned.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  group.shutdown();
  worker.join();

  CHECK(returned.load(std::memory_order_acquire));
  CHECK(!completed);

  close_fd(fds[0]);
  close_fd(fds[1]);
}

//-----------------------------------------------------------------------------------------------

static void gentle_shutdown_collects_ready_fd_wait() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  int fds[2]{-1, -1};
  make_pipe(fds);

  cardio::set_current_dispatcher(&dispatcher);
  auto result = cardio::fd_event::none;
  auto completed = false;
  auto promise = wait_for_read_async(fds[0], result, completed);
  (void)promise;
  write_byte(fds[1]);
  group.shutdown();

  dispatcher.park();

  CHECK(completed);
  CHECK((result & cardio::fd_event::read) != cardio::fd_event::none);

  close_fd(fds[0]);
  close_fd(fds[1]);
}

static void gentle_shutdown_returns_without_pending_fd_wait() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  int fds[2]{-1, -1};
  make_pipe(fds);

  cardio::set_current_dispatcher(&dispatcher);
  auto result = cardio::fd_event::none;
  auto completed = false;
  auto promise = wait_for_read_async(fds[0], result, completed);
  (void)promise;
  group.shutdown();

  dispatcher.park();

  CHECK(!completed);

  close_fd(fds[0]);
  close_fd(fds[1]);
}

//-----------------------------------------------------------------------------------------------

static void user_main_loop_quit_does_not_stop_dispatcher_host_glib() {
  auto* context = new_test_context();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib dispatcher(group);
  auto* loop = g_main_loop_new(group.context(), false);
  auto quit_called = std::atomic<bool>{false};
  auto returned = std::atomic<bool>{false};

  struct state {
    GMainLoop* loop;
    std::atomic<bool>* quit_called;
  } callback_state{loop, &quit_called};

  attach_idle(
      group,
      [](gpointer data) -> gboolean {
        auto* current = static_cast<state*>(data);
        g_main_loop_quit(current->loop);
        current->quit_called->store(true, std::memory_order_release);
        return G_SOURCE_REMOVE;
      },
      &callback_state);

  auto worker = std::thread([&] {
    cardio::set_current_dispatcher(&dispatcher);
    dispatcher.park();
    returned.store(true, std::memory_order_release);
  });

  wait_until_true(quit_called);
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  CHECK(!returned.load(std::memory_order_acquire));

  group.shutdown();
  worker.join();
  CHECK(returned.load(std::memory_order_acquire));

  g_main_loop_unref(loop);
}

//-----------------------------------------------------------------------------------------------

int main() {
  glib_idle_dispatches_and_shutdown_returns();
  default_policy_dispatches_glib_idle_before_queue();
  continuation_policy_dispatches_queue_before_glib_idle();
  default_policy_dispatches_glib_idle_before_inline_continuation();
  continuation_policy_dispatches_inline_before_glib_idle();
  glib_auto_park_dispatches_posted_work();
  external_loop_dispatches_posted_work_with_temporary_tls();
  external_loop_preserves_existing_matching_tls_and_context();
  external_loop_reports_mismatched_tls();
  external_loop_reports_mismatched_thread_default_context();
  glib_group_can_host_regular_and_glib_hosts();
  switch_to_roundtrips_between_glib_and_regular_dispatchers();
  external_loop_can_resume_delay();
  external_loop_can_resume_fd_wait();
  external_loop_fd_wait_does_not_self_wake();
  immediate_shutdown_skips_pending_dispatcher_work();
  immediate_shutdown_from_thread_skips_pending_fd_wait();
  gentle_shutdown_collects_ready_fd_wait();
  gentle_shutdown_returns_without_pending_fd_wait();
  user_main_loop_quit_does_not_stop_dispatcher_host_glib();

  std::puts("cardio_glib_test: PASS");
  return 0;
}
