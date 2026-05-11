// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>
#if CARDIO_HAS_POSIX_FD
#include <unistd.h>
#endif

//-----------------------------------------------------------------------------------------------

namespace {
  using clock_type = std::chrono::steady_clock;

  struct metric {
    const char* name;
    std::size_t iterations;
    std::chrono::nanoseconds elapsed;
  };

  static std::size_t perf_iterations() {
    const auto* value = std::getenv("CARDIO_SCHEDULER_PERF_ITERS");
    if (value == nullptr || *value == '\0') {
      return 200;
    }

    const auto parsed = std::strtoull(value, nullptr, 10);
    return parsed == 0 ? 200 : static_cast<std::size_t>(parsed);
  }

  template <typename TAction>
  static metric measure(
      const char* name,
      std::size_t iterations,
      TAction action) {
    const auto started = clock_type::now();
    for (auto index = std::size_t{0}; index < iterations; ++index) {
      action();
    }
    const auto elapsed = clock_type::now() - started;
    return metric{
        name,
        iterations,
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)};
  }

  static void print_metric(const metric& result) {
    const auto elapsed_ns =
        static_cast<unsigned long long>(result.elapsed.count());
    const auto per_iteration =
        result.iterations == 0 ? 0 : elapsed_ns / result.iterations;
    std::printf(
        "scheduler_perf\t%s\titerations=%zu\telapsed_ns=%llu\tper_iteration_ns=%llu\n",
        result.name,
        result.iterations,
        elapsed_ns,
        static_cast<unsigned long long>(per_iteration));
  }

  static cardio::promise<void> delay_fanout_child() {
    co_await cardio::promises::delay(1);
  }

  static cardio::promise<void> resolved_continuation_child(
      std::size_t remaining,
      std::size_t& completed) {
    if (remaining == 0) {
      ++completed;
      co_return;
    }
    co_await cardio::resolved();
    co_await resolved_continuation_child(remaining - 1, completed);
  }

#if CARDIO_HAS_POSIX_FD
  static void close_fd(int& fd) {
    if (fd >= 0) {
      CHECK_EQ(::close(fd), 0);
      fd = -1;
    }
  }

  static void make_pipe(int (&fds)[2]) {
    CHECK_EQ(::pipe(fds), 0);
  }

  static cardio::promise<void> await_fd_ready(
      int fd,
      cardio::cancellation cancellation,
      std::size_t& completed) {
    (void)co_await cardio::from_fd(
        fd,
        cardio::fd_event::read,
        std::move(cancellation));
    ++completed;
  }
#endif
}  // namespace

//-----------------------------------------------------------------------------------------------

static void delay_fanout_perf() {
  test_dispatcher_host dispatcher;
  const auto iterations = perf_iterations();
  auto children = std::vector<cardio::promise<void>>{};
  children.reserve(iterations);

  const auto started = clock_type::now();
  for (auto index = std::size_t{0}; index < iterations; ++index) {
    children.emplace_back(delay_fanout_child());
  }
  dispatcher.park();
  const auto elapsed = clock_type::now() - started;

  for (const auto& child : children) {
    CHECK(child.is_ready());
  }
  print_metric(metric{
      "delay_fanout",
      iterations,
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)});
}

static void cancellation_register_reset_perf() {
  test_dispatcher_host dispatcher;
  const auto iterations = perf_iterations() * 20;
  cardio::cancellation_source source;
  auto cancellation = source.get_cancellation();
  auto registrations = std::vector<cardio::cancellation_registration>{};
  registrations.reserve(iterations);

  const auto result = measure(
      "cancellation_register_reset",
      iterations,
      [&] {
        registrations.emplace_back(
            cancellation.on_cancellation_requested([] {
            }));
        registrations.back().reset();
      });
  print_metric(result);
}

static void resolved_continuation_chain_perf() {
  test_dispatcher_host dispatcher;
  const auto iterations = perf_iterations() * 20;
  auto completed = std::size_t{0};
  auto child = resolved_continuation_child(iterations, completed);

  const auto started = clock_type::now();
  dispatcher.park();
  const auto elapsed = clock_type::now() - started;

  CHECK(child.is_ready());
  CHECK_EQ(completed, static_cast<std::size_t>(1));
  print_metric(metric{
      "resolved_continuation_chain",
      iterations,
      std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)});
}

#if CARDIO_HAS_POSIX_FD
static void timeout_fd_ready_loop_perf() {
  test_dispatcher_host dispatcher;
  const auto iterations = perf_iterations();
  auto completed = std::size_t{0};

  const auto result = measure(
      "timeout_fd_ready_loop",
      iterations,
      [&] {
        int fds[2]{-1, -1};
        make_pipe(fds);
        const auto value = char{'x'};
        CHECK_EQ(::write(fds[1], &value, 1), static_cast<ssize_t>(1));

        auto timeout_source = cardio::cancellations::timeout(1);
        auto child = await_fd_ready(
            fds[0],
            timeout_source.get_cancellation(),
            completed);
        dispatcher.park();
        CHECK(child.is_ready());

        close_fd(fds[0]);
        close_fd(fds[1]);
      });
  CHECK_EQ(completed, iterations);
  print_metric(result);
}
#else
static void timeout_fd_ready_loop_perf() {
  std::printf(
      "scheduler_perf\ttimeout_fd_ready_loop\titerations=0\telapsed_ns=0\tper_iteration_ns=0\n");
}
#endif

//-----------------------------------------------------------------------------------------------

int main() {
  timeout_fd_ready_loop_perf();
  delay_fanout_perf();
  cancellation_register_reset_perf();
  resolved_continuation_chain_perf();
  return 0;
}
