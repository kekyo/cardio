// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>

#if CARDIO_WITH_LINUX_IO_URING
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>
#endif

//-----------------------------------------------------------------------------------------------

#if CARDIO_WITH_LINUX_IO_URING
struct temp_file {
  std::string path;
  int fd = -1;

  temp_file() = default;

  temp_file(const temp_file&) = delete;
  temp_file& operator=(const temp_file&) = delete;

  temp_file(temp_file&& other) noexcept
      : path(std::move(other.path)), fd(std::exchange(other.fd, -1)) {}

  temp_file& operator=(temp_file&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    cleanup();
    path = std::move(other.path);
    fd = std::exchange(other.fd, -1);
    return *this;
  }

  ~temp_file() {
    cleanup();
  }

  void cleanup() noexcept {
    if (fd >= 0) {
      (void)::close(fd);
      fd = -1;
    }
    if (!path.empty()) {
      (void)::unlink(path.c_str());
      path.clear();
    }
  }
};

struct temp_dir {
  std::string path;

  temp_dir() = default;

  temp_dir(const temp_dir&) = delete;
  temp_dir& operator=(const temp_dir&) = delete;

  temp_dir(temp_dir&& other) noexcept
      : path(std::move(other.path)) {}

  temp_dir& operator=(temp_dir&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    cleanup();
    path = std::move(other.path);
    return *this;
  }

  ~temp_dir() {
    cleanup();
  }

  void cleanup() noexcept {
    if (!path.empty()) {
      (void)::rmdir(path.c_str());
      path.clear();
    }
  }
};

static bool is_io_uring_unavailable(const std::system_error& error) {
  const auto value = error.code().value();
  return value == ENOSYS || value == EPERM || value == EACCES ||
         value == EOPNOTSUPP;
}

static void close_fd(int& fd) {
  if (fd >= 0) {
    CHECK_EQ(::close(fd), 0);
    fd = -1;
  }
}

static void write_all(int fd, const char* data, std::size_t size) {
  auto offset = std::size_t{0};
  while (offset < size) {
    const auto written = ::write(fd, data + offset, size - offset);
    CHECK(written > 0);
    offset += static_cast<std::size_t>(written);
  }
}

static std::string read_file_contents(int fd) {
  CHECK_EQ(::lseek(fd, 0, SEEK_SET), static_cast<off_t>(0));

  auto result = std::string{};
  auto buffer = std::array<char, 64>{};
  while (true) {
    const auto read_size = ::read(fd, buffer.data(), buffer.size());
    CHECK(read_size >= 0);
    if (read_size == 0) {
      return result;
    }
    result.append(buffer.data(), static_cast<std::size_t>(read_size));
  }
}

static temp_file make_temp_file(const char* content) {
  auto file = temp_file{};
  auto path_template = std::array<char, 36>{};
  const auto prefix = std::string{"/tmp/cardio_io_uring_test_XXXXXX"};
  std::memcpy(path_template.data(), prefix.c_str(), prefix.size() + 1);

  file.fd = ::mkstemp(path_template.data());
  CHECK(file.fd >= 0);
  file.path = path_template.data();

  if (content != nullptr) {
    write_all(file.fd, content, std::strlen(content));
    CHECK_EQ(::lseek(file.fd, 0, SEEK_SET), static_cast<off_t>(0));
  }

  return file;
}

static temp_dir make_temp_dir() {
  auto dir = temp_dir{};
  auto path_template = std::array<char, 35>{};
  const auto prefix = std::string{"/tmp/cardio_io_uring_dir_XXXXXX"};
  std::memcpy(path_template.data(), prefix.c_str(), prefix.size() + 1);

  auto* path = ::mkdtemp(path_template.data());
  CHECK(path != nullptr);
  dir.path = path;
  return dir;
}

template <typename Promise>
static void check_canceled(Promise& promise) {
  CHECK(promise.is_ready());

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }
  CHECK(caught);
}

template <typename Promise>
static void check_invalid_argument(Promise& promise) {
  CHECK(promise.is_ready());

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const std::invalid_argument&) {
    caught = true;
  }
  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void read_returns_file_contents() {
  auto file = make_temp_file("hello");
  auto buffer = std::array<char, 5>{};
  auto bytes = std::as_writable_bytes(std::span(buffer));

  test_dispatcher_host dispatcher;
  cardio::io_uring io;
  auto p = cardio::io_urings::read(io, file.fd, bytes);
  dispatcher.park();

  CHECK(p.is_ready());
  CHECK_EQ(p.unsafe_result(), std::size_t{5});
  CHECK_EQ(std::string(buffer.data(), buffer.size()), std::string("hello"));
}

//-----------------------------------------------------------------------------------------------

static void write_persists_file_contents() {
  auto file = make_temp_file("");
  const auto content = std::string{"written"};
  const auto bytes =
      std::as_bytes(std::span(content.data(), content.size()));

  test_dispatcher_host dispatcher;
  cardio::io_uring io;
  auto p = cardio::io_urings::write(io, file.fd, bytes);
  dispatcher.park();

  CHECK(p.is_ready());
  CHECK_EQ(p.unsafe_result(), content.size());
  CHECK_EQ(read_file_contents(file.fd), content);
}

//-----------------------------------------------------------------------------------------------

static void empty_spans_resolve_immediately() {
  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  auto read_p = cardio::io_urings::read(
      io, STDIN_FILENO, std::span<std::byte>{});
  CHECK(read_p.is_ready());
  CHECK_EQ(read_p.unsafe_result(), std::size_t{0});

  auto write_p = cardio::io_urings::write(
      io, STDOUT_FILENO, std::span<const std::byte>{});
  CHECK(write_p.is_ready());
  CHECK_EQ(write_p.unsafe_result(), std::size_t{0});
}

//-----------------------------------------------------------------------------------------------

static void submit_requires_current_dispatcher() {
  cardio::io_uring io;

  auto caught = false;
  try {
    auto promise =
        cardio::io_urings::read(io, STDIN_FILENO, std::span<std::byte>{});
    (void)promise;
  } catch (const std::runtime_error& exception) {
    caught = exception.what() == std::string("cardio: no active dispatcher");
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void closed_fd_fails_with_system_error() {
  auto file = make_temp_file("");
  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  const auto closed_fd = file.fd;
  close_fd(file.fd);

  auto buffer = std::array<char, 1>{};
  auto bytes = std::as_writable_bytes(std::span(buffer));

  auto p = cardio::io_urings::read(io, closed_fd, bytes);
  dispatcher.park();

  CHECK(p.is_ready());

  auto caught = false;
  try {
    (void)p.unsafe_result();
  } catch (const std::system_error&) {
    caught = true;
  }
  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void validation_failures_reject_promises() {
  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  auto buffer = std::array<char, 1>{};
  auto bytes = std::as_writable_bytes(std::span(buffer));
  auto negative_fd_p = cardio::io_urings::read(io, -1, bytes);
  check_invalid_argument(negative_fd_p);

  struct statx stat_buffer {};
  auto invalid_dfd_p = cardio::io_urings::statx(
      io, -2, std::string{"unused"}, 0, STATX_SIZE, stat_buffer);
  check_invalid_argument(invalid_dfd_p);

  const auto too_large =
      static_cast<std::size_t>(std::numeric_limits<unsigned>::max()) + 1;
  auto too_large_span =
      std::span<std::byte>(reinterpret_cast<std::byte*>(1), too_large);
  auto too_large_p =
      cardio::io_urings::read(io, STDIN_FILENO, too_large_span);
  check_invalid_argument(too_large_p);
}

//-----------------------------------------------------------------------------------------------

static void cancellation_overloads_fail_when_already_canceled() {
  auto file = make_temp_file("cancel");
  test_dispatcher_host dispatcher;
  cardio::io_uring io;
  cardio::cancellation_source source;
  CHECK(source.cancel());

  auto buffer = std::array<char, 1>{};
  auto bytes = std::as_writable_bytes(std::span(buffer));
  auto read_p =
      cardio::io_urings::read(
          io, file.fd, bytes, source.get_cancellation());
  check_canceled(read_p);

  auto open_p =
      cardio::io_urings::open(
          io, file.path, O_RDONLY, 0, source.get_cancellation());
  check_canceled(open_p);
}

//-----------------------------------------------------------------------------------------------

static void multiple_operations_complete_on_one_dispatcher() {
  auto source = make_temp_file("abc");
  auto destination = make_temp_file("");
  auto read_buffer = std::array<char, 3>{};
  auto read_bytes = std::as_writable_bytes(std::span(read_buffer));
  const auto write_content = std::string{"xyz"};
  const auto write_bytes =
      std::as_bytes(std::span(write_content.data(), write_content.size()));

  test_dispatcher_host dispatcher;
  cardio::io_uring io;
  auto read_p = cardio::io_urings::read(io, source.fd, read_bytes);
  auto write_p = cardio::io_urings::write(io, destination.fd, write_bytes);
  dispatcher.park();

  CHECK(read_p.is_ready());
  CHECK(write_p.is_ready());
  CHECK_EQ(read_p.unsafe_result(), std::size_t{3});
  CHECK_EQ(write_p.unsafe_result(), write_content.size());
  CHECK_EQ(std::string(read_buffer.data(), read_buffer.size()), std::string("abc"));
  CHECK_EQ(read_file_contents(destination.fd), write_content);
}

//-----------------------------------------------------------------------------------------------

static void fsync_resolves_without_value() {
  auto file = make_temp_file("sync");
  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  auto p = cardio::io_urings::fsync(io, file.fd);
  auto flags_p = cardio::io_urings::fsync(io, file.fd, 0);
  dispatcher.park();

  CHECK(p.is_ready());
  p.unsafe_result();
  CHECK(flags_p.is_ready());
  flags_p.unsafe_result();
}

//-----------------------------------------------------------------------------------------------

static void statx_helper_reads_file_metadata() {
  auto file = make_temp_file("data");
  struct statx stat_buffer {};
  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  auto statx_p = cardio::io_urings::statx(
      io, AT_FDCWD, file.path, 0, STATX_SIZE, stat_buffer);
  dispatcher.park();

  CHECK(statx_p.is_ready());
  statx_p.unsafe_result();
  CHECK_EQ(stat_buffer.stx_size, std::uint64_t{4});
}

//-----------------------------------------------------------------------------------------------

static void open_and_close_helpers_manage_file_descriptors() {
  auto file = make_temp_file("opened");
  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  auto open_p = cardio::io_urings::open(io, file.path, O_RDONLY);
  dispatcher.park();

  CHECK(open_p.is_ready());
  auto opened_fd = open_p.unsafe_result();
  CHECK(opened_fd >= 0);

  auto buffer = std::array<char, 6>{};
  CHECK_EQ(
      ::read(opened_fd, buffer.data(), buffer.size()),
      static_cast<ssize_t>(buffer.size()));
  CHECK_EQ(std::string(buffer.data(), buffer.size()), std::string("opened"));

  auto close_p = cardio::io_urings::close(io, opened_fd);
  dispatcher.park();
  opened_fd = -1;

  CHECK(close_p.is_ready());
  close_p.unsafe_result();
}

//-----------------------------------------------------------------------------------------------

static void path_mutation_helpers_update_filesystem() {
  auto dir = make_temp_dir();
  const auto first_path = dir.path + "/first";
  const auto renamed_path = dir.path + "/renamed";
  const auto subdir_path = dir.path + "/subdir";

  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  auto open_p = cardio::io_urings::open(
      io, first_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  dispatcher.park();
  CHECK(open_p.is_ready());
  auto fd = open_p.unsafe_result();
  write_all(fd, "x", 1);
  close_fd(fd);

  auto rename_p = cardio::io_urings::rename(io, first_path, renamed_path);
  dispatcher.park();
  CHECK(rename_p.is_ready());
  rename_p.unsafe_result();
  CHECK(::access(first_path.c_str(), F_OK) == -1);
  CHECK_EQ(::access(renamed_path.c_str(), F_OK), 0);

  auto mkdir_p = cardio::io_urings::mkdir(io, subdir_path, 0755);
  dispatcher.park();
  CHECK(mkdir_p.is_ready());
  mkdir_p.unsafe_result();
  CHECK_EQ(::access(subdir_path.c_str(), F_OK), 0);

  auto unlink_file_p = cardio::io_urings::unlink(io, renamed_path);
  dispatcher.park();
  CHECK(unlink_file_p.is_ready());
  unlink_file_p.unsafe_result();
  CHECK_EQ(::rmdir(subdir_path.c_str()), 0);
}

//-----------------------------------------------------------------------------------------------

static void submit_io_uring_returns_completion_fields() {
  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  auto p = cardio::io_urings::submit(io, [](::io_uring_sqe* sqe) {
    ::io_uring_prep_nop(sqe);
  });
  dispatcher.park();

  CHECK(p.is_ready());
  const auto completion = p.unsafe_result();
  CHECK_EQ(completion.result, 0);
}

//-----------------------------------------------------------------------------------------------

static void submit_converts_completion_result() {
  test_dispatcher_host dispatcher;
  cardio::io_uring io;

  auto p = io.submit<int>(
      [](::io_uring_sqe* sqe) {
        ::io_uring_prep_nop(sqe);
      },
      [](cardio::io_uring_completion completion) {
        CHECK_EQ(completion.result, 0);
        return completion.result + 42;
      });
  dispatcher.park();

  CHECK(p.is_ready());
  CHECK_EQ(p.unsafe_result(), 42);
}

//-----------------------------------------------------------------------------------------------

static void submit_io_uring_with_already_canceled_signal_fails() {
  test_dispatcher_host dispatcher;
  cardio::io_uring io;
  cardio::cancellation_source source;

  CHECK(source.cancel());
  auto p = cardio::io_urings::submit(
      io,
      [](::io_uring_sqe* sqe) {
        ::io_uring_prep_nop(sqe);
      },
      source.get_cancellation());

  CHECK(p.is_ready());

  auto caught = false;
  try {
    (void)p.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void pending_operation_cancellation_fails_promise() {
  test_dispatcher_host dispatcher;
  cardio::io_uring io;
  cardio::cancellation_source source;
  auto timeout = __kernel_timespec{};
  timeout.tv_sec = 0;
  timeout.tv_nsec = 100000000;

  auto p = cardio::io_urings::submit(
      io,
      [&timeout](::io_uring_sqe* sqe) {
        ::io_uring_prep_timeout(sqe, &timeout, 0, 0);
      },
      source.get_cancellation());

  CHECK(source.cancel());
  dispatcher.park();

  CHECK(p.is_ready());

  auto caught = false;
  try {
    (void)p.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }

  CHECK(caught);
}

//-----------------------------------------------------------------------------------------------

static void completed_operation_wins_over_late_cancellation() {
  test_dispatcher_host dispatcher;
  cardio::io_uring io;
  cardio::cancellation_source source;

  auto p = cardio::io_urings::submit(
      io,
      [](::io_uring_sqe* sqe) {
        ::io_uring_prep_nop(sqe);
      },
      source.get_cancellation());
  dispatcher.park();

  CHECK(p.is_ready());
  CHECK_EQ(p.unsafe_result().result, 0);
  CHECK(source.cancel());
  CHECK_EQ(p.unsafe_result().result, 0);
}
#endif

//-----------------------------------------------------------------------------------------------

int main() {
#if !CARDIO_WITH_LINUX_IO_URING
  std::puts("cardio_io_uring_test: SKIP");
  return 0;
#else
  try {
    auto probe = cardio::io_uring(8);
    (void)probe;
  } catch (const std::system_error& error) {
    if (is_io_uring_unavailable(error)) {
      std::puts("cardio_io_uring_test: SKIP");
      return 0;
    }
    throw;
  }

  read_returns_file_contents();
  write_persists_file_contents();
  empty_spans_resolve_immediately();
  submit_requires_current_dispatcher();
  closed_fd_fails_with_system_error();
  validation_failures_reject_promises();
  cancellation_overloads_fail_when_already_canceled();
  multiple_operations_complete_on_one_dispatcher();
  fsync_resolves_without_value();
  statx_helper_reads_file_metadata();
  open_and_close_helpers_manage_file_descriptors();
  path_mutation_helpers_update_filesystem();
  submit_io_uring_returns_completion_fields();
  submit_converts_completion_result();
  submit_io_uring_with_already_canceled_signal_fails();
  pending_operation_cancellation_fails_promise();
  completed_operation_wins_over_late_cancellation();

  std::puts("cardio_io_uring_test: PASS");
  return 0;
#endif
}
