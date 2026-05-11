// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
#if CARDIO_HAS_POSIX_FD
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if CARDIO_HAS_EXCEPTIONS
#error "CARDIO_HAS_EXCEPTIONS must be 0 for this test."
#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_POSIX_FD
static bool has_event(cardio::fd_event events, cardio::fd_event expected) {
  return (events & expected) != cardio::fd_event::none;
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
  const auto value = char{'x'};
  CHECK_EQ(::write(fd, &value, 1), static_cast<ssize_t>(1));
}

static std::string read_all_from_fd(int fd) {
  auto result = std::string{};
  char buffer[256]{};
  while (true) {
    const auto size = ::read(fd, buffer, sizeof(buffer));
    if (size > 0) {
      result.append(buffer, static_cast<std::size_t>(size));
      continue;
    }
    if (size == 0) {
      return result;
    }
    if (errno == EINTR) {
      continue;
    }
    CHECK(false);
  }
}

static int wait_for_child(pid_t pid) {
  auto status = int{};
  while (::waitpid(pid, &status, 0) == -1) {
    if (errno == EINTR) {
      continue;
    }
    CHECK(false);
  }
  return status;
}
#endif

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> await_resolved_async(
    bool& continued,
    int& result) {
  auto value = co_await cardio::resolved(41);
  continued = true;
  result = value + 1;
}

static cardio::promise<void> await_resolved_void_async(bool& continued) {
  co_await cardio::resolved();
  continued = true;
}

static cardio::promise<int> await_source_value_async(
    cardio::promise<int>& child) {
  auto value = co_await child;
  co_return value + 1;
}

static cardio::promise<int> await_source_void_async(
    cardio::promise<void>& child) {
  co_await child;
  co_return 19;
}

static cardio::promise<void> await_delay_without_exceptions_async(int& result) {
  co_await cardio::promises::delay(10);
  result = 29;
}

static cardio::promise<void> await_all_without_exceptions_async(int& result) {
  auto values = co_await cardio::promises::all(
      cardio::resolved(13), cardio::resolved(17), cardio::resolved());
  result = std::get<0>(values) + std::get<1>(values);
}

static cardio::promise<void> fire_and_forget_without_exceptions_async(
    int& result) {
  co_await cardio::promises::delay(10);
  result = 43;
}

//-----------------------------------------------------------------------------------------------

static void resolved_value_promise_works_without_exceptions() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::resolved(7);

  CHECK(promise.is_ready());
  CHECK(promise.try_result() != nullptr);
  CHECK_EQ(promise.unsafe_result(), 7);
}

static void resolved_void_promise_works_without_exceptions() {
  test_dispatcher_host dispatcher;

  auto promise = cardio::resolved();

  CHECK(promise.is_ready());
  CHECK(promise.try_result());
  promise.unsafe_result();
}

static void dispatcher_resumes_coroutines_without_exceptions() {
  test_dispatcher_host dispatcher;
  auto value_continued = false;
  auto void_continued = false;
  auto value_result = 0;

  auto value_promise = await_resolved_async(value_continued, value_result);
  auto void_promise = await_resolved_void_async(void_continued);
  (void)value_promise;
  (void)void_promise;

  CHECK(!value_continued);
  CHECK(!void_continued);

  dispatcher.park();

  CHECK(value_continued);
  CHECK(void_continued);
  CHECK_EQ(value_result, 42);
}

static void posted_callback_runs_without_exceptions() {
  test_dispatcher_host dispatcher;
  auto ran = false;

  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] { ran = true; });
  dispatcher.park();

  CHECK(ran);
}

static void fire_and_forget_runs_without_exceptions() {
  test_dispatcher_host dispatcher;
  auto result = 0;

  cardio::fire_and_forget(fire_and_forget_without_exceptions_async(result));
  dispatcher.park();

  CHECK_EQ(result, 43);
}

#if CARDIO_HAS_POSIX_FD
static void fd_readiness_resolves_without_exceptions() {
  test_dispatcher_host dispatcher;
  int fds[2]{-1, -1};
  make_pipe(fds);

  auto promise = cardio::from_fd(fds[0], cardio::fd_event::read);
  CHECK(!promise.is_ready());

  write_byte(fds[1]);
  dispatcher.park();

  CHECK(promise.is_ready());
  CHECK(has_event(promise.unsafe_result(), cardio::fd_event::read));

  close_fd(fds[0]);
  close_fd(fds[1]);
}
#endif

static void promise_source_resolve_works_without_exceptions() {
  test_dispatcher_host dispatcher;
  cardio::promise_source<int> source;
  auto child = source.get_promise();
  auto parent = await_source_value_async(child);

  source.resolve(40);

  cardio::promise_source<void> void_source;
  auto void_child = void_source.get_promise();
  auto void_parent = await_source_void_async(void_child);

  void_source.resolve();

  dispatcher.park();

  CHECK(child.is_ready());
  CHECK_EQ(child.unsafe_result(), 40);
  CHECK_EQ(parent.unsafe_result(), 41);
  CHECK(void_child.is_ready());
  CHECK(void_child.try_result());
  CHECK_EQ(void_parent.unsafe_result(), 19);
}

static void cancellation_source_notifies_without_exceptions() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source source;
  auto cancellation = source.get_cancellation();
  auto callbacks = 0;

  CHECK(!cancellation.is_cancellation_requested());

  auto registration = cancellation.on_cancellation_requested([&] {
    callbacks += 1;
  });

  CHECK(source.cancel());
  CHECK(!source.cancel());
  CHECK(cancellation.is_cancellation_requested());
  CHECK_EQ(callbacks, 0);

  dispatcher.park();

  CHECK_EQ(callbacks, 1);

  registration.reset();
}

static void supplemental_delay_resolves_without_exceptions() {
  test_dispatcher_host dispatcher;
  auto result = 0;

  auto promise = await_delay_without_exceptions_async(result);
  (void)promise;

  dispatcher.park();

  CHECK_EQ(result, 29);
}

static void helper_all_resolves_without_exceptions() {
  test_dispatcher_host dispatcher;
  auto tuple_result = 0;

  auto tuple_promise = await_all_without_exceptions_async(tuple_result);
  (void)tuple_promise;

  auto vector_inputs = std::vector<cardio::promise<int>>{};
  vector_inputs.push_back(cardio::resolved(2));
  vector_inputs.push_back(cardio::resolved(3));
  auto vector_promise = cardio::promises::all(std::move(vector_inputs));

  CHECK(!vector_promise.is_ready());

  dispatcher.park();

  CHECK_EQ(tuple_result, 30);

  CHECK(vector_promise.is_ready());
  auto& values = vector_promise.unsafe_result();
  CHECK_EQ(values.size(), static_cast<std::size_t>(2));
  CHECK_EQ(values[0], 2);
  CHECK_EQ(values[1], 3);
}

static void primitives_resolve_without_exceptions() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;
  cardio::primitives::semaphore semaphore(1);
  cardio::primitives::manually_conditional condition(true);

  auto lock_promise = locker.lock();
  CHECK(lock_promise.is_ready());
  auto lock_handle = std::move(lock_promise).unsafe_result();
  CHECK(locker.is_locked());
  lock_handle.release();
  CHECK(!locker.is_locked());

  auto semaphore_promise = semaphore.acquire();
  CHECK(semaphore_promise.is_ready());
  auto semaphore_handle = std::move(semaphore_promise).unsafe_result();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(0));
  semaphore_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(1));

  auto condition_promise = condition.wait();
  CHECK(condition_promise.is_ready());
}

static void helper_timeout_cancels_without_exceptions() {
  test_dispatcher_host dispatcher;
  auto source = cardio::cancellations::timeout(10);
  auto cancellation = source.get_cancellation();

  CHECK(!cancellation.is_cancellation_requested());
  dispatcher.park();
  CHECK(cancellation.is_cancellation_requested());
  CHECK(!source.cancel());
}

static void cancellations_any_combines_without_exceptions() {
  test_dispatcher_host dispatcher;
  cardio::cancellation_source first;
  cardio::cancellation_source second;

  auto source = cardio::cancellations::any(
      first.get_cancellation(), second.get_cancellation());
  auto cancellation = source.get_cancellation();

  CHECK(!cancellation.is_cancellation_requested());
  CHECK(second.cancel());
  CHECK(!cancellation.is_cancellation_requested());

  dispatcher.park();

  CHECK(cancellation.is_cancellation_requested());
  CHECK(!source.cancel());
}

#if CARDIO_HAS_POSIX_FD
static void terminate_logs_message_without_exceptions() {
  int pipe_fds[2]{-1, -1};
  make_pipe(pipe_fds);

  const auto pid = ::fork();
  CHECK(pid >= 0);

  if (pid == 0) {
    CHECK_EQ(::close(pipe_fds[0]), 0);
    CHECK_EQ(::dup2(pipe_fds[1], STDERR_FILENO), STDERR_FILENO);
    CHECK_EQ(::close(pipe_fds[1]), 0);

    (void)cardio::from_fd(-1, cardio::fd_event::read);
    _exit(0);
  }

  CHECK_EQ(::close(pipe_fds[1]), 0);
  pipe_fds[1] = -1;

  const auto output = read_all_from_fd(pipe_fds[0]);
  close_fd(pipe_fds[0]);

  const auto status = wait_for_child(pid);
  CHECK(WIFSIGNALED(status) ||
        (WIFEXITED(status) && WEXITSTATUS(status) != 0));
  CHECK(output.find(
            "cardio: terminating: "
            "cardio: file descriptor must not be negative") !=
          std::string::npos);
}
#endif

#if CARDIO_HAS_POSIX_FD
static void abandoned_promise_source_terminates_without_exceptions() {
  int pipe_fds[2]{-1, -1};
  make_pipe(pipe_fds);

  const auto pid = ::fork();
  CHECK(pid >= 0);

  if (pid == 0) {
    CHECK_EQ(::close(pipe_fds[0]), 0);
    CHECK_EQ(::dup2(pipe_fds[1], STDERR_FILENO), STDERR_FILENO);
    CHECK_EQ(::close(pipe_fds[1]), 0);

    test_dispatcher_host dispatcher;
    {
      cardio::promise_source<int> source;
      (void)source.get_promise();
    }
    _exit(0);
  }

  CHECK_EQ(::close(pipe_fds[1]), 0);
  pipe_fds[1] = -1;

  const auto output = read_all_from_fd(pipe_fds[0]);
  close_fd(pipe_fds[0]);

  const auto status = wait_for_child(pid);
  CHECK(WIFSIGNALED(status) ||
        (WIFEXITED(status) && WEXITSTATUS(status) != 0));
  CHECK(output.find(
            "cardio: terminating: cardio: broken promise_source") !=
        std::string::npos);
}
#endif

//-----------------------------------------------------------------------------------------------

int main() {
  resolved_value_promise_works_without_exceptions();
  resolved_void_promise_works_without_exceptions();
  dispatcher_resumes_coroutines_without_exceptions();
  posted_callback_runs_without_exceptions();
  fire_and_forget_runs_without_exceptions();
#if CARDIO_HAS_POSIX_FD
  fd_readiness_resolves_without_exceptions();
#endif
  promise_source_resolve_works_without_exceptions();
  cancellation_source_notifies_without_exceptions();
  supplemental_delay_resolves_without_exceptions();
  helper_all_resolves_without_exceptions();
  primitives_resolve_without_exceptions();
  helper_timeout_cancels_without_exceptions();
  cancellations_any_combines_without_exceptions();
#if CARDIO_HAS_POSIX_FD
  terminate_logs_message_without_exceptions();
  abandoned_promise_source_terminates_without_exceptions();
#endif

  std::puts("cardio_no_exceptions_test: PASS");
  return 0;
}
