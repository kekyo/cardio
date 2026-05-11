// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#if CARDIO_WITH_GIO

#include <gio/gio.h>
#include <glib/gstdio.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

//-----------------------------------------------------------------------------------------------

static bool has_feature(
    cardio::dispatcher_feature features,
    cardio::dispatcher_feature feature) noexcept {
  return (features & feature) != cardio::dispatcher_feature::none;
}

static std::vector<std::byte> to_bytes(std::string_view value) {
  auto bytes = std::vector<std::byte>(value.size());
  if (!value.empty()) {
    std::memcpy(bytes.data(), value.data(), value.size());
  }
  return bytes;
}

static std::string from_bytes(std::span<const std::byte> bytes) {
  if (bytes.empty()) {
    return {};
  }
  return std::string(
      reinterpret_cast<const char*>(bytes.data()),
      bytes.size());
}

static std::string contents_to_string(
    const cardio::gio::file_contents& contents) {
  return from_bytes(contents.bytes);
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

template <typename Operation>
static void run_glib_operation(Operation operation) {
  auto* context = g_main_context_new();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib_auto dispatcher(group);
  auto* loop = g_main_loop_new(group.context(), false);
  auto promise = std::optional<cardio::promise<void>>{};

  struct loop_state {
    std::optional<cardio::promise<void>>* promise;
    GMainLoop* loop;
  } state{&promise, loop};

  auto* source = g_timeout_source_new(1);
  g_source_set_callback(
      source,
      [](gpointer data) -> gboolean {
        auto* current = static_cast<loop_state*>(data);
        if (current->promise->has_value() && (*current->promise)->is_ready()) {
          g_main_loop_quit(current->loop);
          return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
      },
      &state,
      nullptr);
  g_source_attach(source, group.context());
  g_source_unref(source);

  cardio::set_current_dispatcher(nullptr);
  cardio::internal::dangerous_schedule_later__(&dispatcher, [&] {
    promise.emplace(operation(group));
  });
  g_main_loop_run(loop);

  CHECK(promise.has_value());
  CHECK(promise->is_ready());
  promise->unsafe_result();
  CHECK(cardio::unsafe_get_current_dispatcher() == nullptr);

  g_main_loop_unref(loop);
}

struct temp_dir {
  char* path = nullptr;

  inline temp_dir() {
    path = g_dir_make_tmp("cardio-gio-test-XXXXXX", nullptr);
    CHECK(path != nullptr);
  }

  inline ~temp_dir() {
    if (path != nullptr) {
      (void)g_rmdir(path);
      g_free(path);
    }
  }

  temp_dir(const temp_dir&) = delete;
  temp_dir& operator=(const temp_dir&) = delete;
};

struct temp_path {
  char* path = nullptr;

  inline temp_path(const temp_dir& dir, const char* name)
      : path(g_build_filename(dir.path, name, nullptr)) {
    CHECK(path != nullptr);
  }

  inline ~temp_path() {
    if (path != nullptr) {
      (void)g_remove(path);
      g_free(path);
    }
  }

  temp_path(const temp_path&) = delete;
  temp_path& operator=(const temp_path&) = delete;
};

//-----------------------------------------------------------------------------------------------

static void dispatcher_features_are_reported() {
  auto combined = cardio::dispatcher_feature::glib;
  combined |= cardio::dispatcher_feature::gio;
  CHECK(has_feature(combined, cardio::dispatcher_feature::glib));
  CHECK(has_feature(combined, cardio::dispatcher_feature::gio));
  combined &= cardio::dispatcher_feature::gio;
  CHECK(!has_feature(combined, cardio::dispatcher_feature::glib));
  CHECK(has_feature(combined, cardio::dispatcher_feature::gio));

  test_dispatcher_host dispatcher;
  const auto base_features = dispatcher.get_feature();
  CHECK(!has_feature(base_features, cardio::dispatcher_feature::glib));
  CHECK(!has_feature(base_features, cardio::dispatcher_feature::gio));
#if CARDIO_HAS_POSIX_FD
  CHECK(has_feature(base_features, cardio::dispatcher_feature::posix));
#endif
#if CARDIO_HAS_EXCEPTIONS
  CHECK(has_feature(base_features, cardio::dispatcher_feature::exceptions));
#endif

  auto* context = g_main_context_new();
  cardio::dispatcher_group_glib group(context);
  g_main_context_unref(context);
  cardio::dispatcher_host_glib glib_dispatcher(group);
  const auto glib_features = glib_dispatcher.get_feature();
  CHECK(has_feature(glib_features, cardio::dispatcher_feature::glib));
  CHECK(has_feature(glib_features, cardio::dispatcher_feature::gio));

  cardio::dispatcher_host_glib_auto glib_auto_dispatcher(group);
  const auto glib_auto_features = glib_auto_dispatcher.get_feature();
  CHECK(has_feature(glib_auto_features, cardio::dispatcher_feature::glib));
  CHECK(has_feature(glib_auto_features, cardio::dispatcher_feature::gio));
}

//-----------------------------------------------------------------------------------------------

static void gio_helpers_reject_non_gio_dispatcher() {
  test_dispatcher_host dispatcher;
  const auto text = std::string_view{"x"};
  auto* stream = G_INPUT_STREAM(
      g_memory_input_stream_new_from_data(text.data(), text.size(), nullptr));
  auto buffer = std::array<char, 1>{};
  auto promise = cardio::gio::read(
      stream,
      std::as_writable_bytes(std::span(buffer)));

  check_invalid_argument(promise);
  g_object_unref(stream);
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> stream_helpers_async(
    cardio::dispatcher_group_glib& group) {
  try {
    const auto input_text = std::string_view{"abcdef"};
    auto* input = G_INPUT_STREAM(g_memory_input_stream_new_from_data(
        input_text.data(), input_text.size(), nullptr));
    auto first = std::array<char, 3>{};
    const auto read_count = co_await cardio::gio::read(
        input,
        std::as_writable_bytes(std::span(first)));
    CHECK_EQ(read_count, std::size_t{3});
    CHECK_EQ(std::string(first.data(), first.size()), std::string("abc"));

    const auto skipped = co_await cardio::gio::skip(input, 1);
    CHECK_EQ(skipped, std::size_t{1});

    auto rest = std::array<char, 2>{};
    const auto rest_count = co_await cardio::gio::read_all(
        input,
        std::as_writable_bytes(std::span(rest)));
    CHECK_EQ(rest_count, std::size_t{2});
    CHECK_EQ(std::string(rest.data(), rest.size()), std::string("ef"));

    co_await cardio::gio::close(input);
    g_object_unref(input);

    auto* byte_input = G_INPUT_STREAM(g_memory_input_stream_new_from_data(
        input_text.data(), input_text.size(), nullptr));
    auto* bytes = co_await cardio::gio::read_bytes(byte_input, 4);
    auto byte_size = gsize{};
    const auto* byte_data = static_cast<const char*>(
        g_bytes_get_data(bytes, &byte_size));
    CHECK_EQ(byte_size, static_cast<gsize>(4));
    CHECK_EQ(std::string(byte_data, byte_size), std::string("abcd"));
    g_bytes_unref(bytes);
    co_await cardio::gio::close(byte_input);
    g_object_unref(byte_input);

    auto* output = G_OUTPUT_STREAM(g_memory_output_stream_new_resizable());
    auto hello = to_bytes("hello");
    auto space = to_bytes(" ");
    auto world = to_bytes("world");
    auto* world_bytes = g_bytes_new(world.data(), world.size());

    CHECK_EQ(co_await cardio::gio::write(output, hello), hello.size());
    CHECK_EQ(co_await cardio::gio::write_all(output, space), space.size());
    CHECK_EQ(
        co_await cardio::gio::write_bytes(output, world_bytes),
        world.size());
    g_bytes_unref(world_bytes);

    co_await cardio::gio::flush(output);
    co_await cardio::gio::close(output);

    auto* memory_output = G_MEMORY_OUTPUT_STREAM(output);
    const auto output_size = g_memory_output_stream_get_data_size(memory_output);
    const auto* output_data = static_cast<const char*>(
        g_memory_output_stream_get_data(memory_output));
    CHECK_EQ(output_size, static_cast<gsize>(11));
    CHECK_EQ(std::string(output_data, output_size), std::string("hello world"));
    g_object_unref(output);
  } catch (...) {
    group.shutdown();
    throw;
  }

  group.shutdown();
  co_return;
}

static void stream_helpers_complete() {
  run_glib_operation([](cardio::dispatcher_group_glib& group) {
    return stream_helpers_async(group);
  });
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> file_helpers_async(
    cardio::dispatcher_group_glib& group,
    const temp_dir& dir,
    const temp_path& file_path,
    const temp_path& copy_path,
    const temp_path& subdir_path,
    const temp_path& rw_path) {
  try {
    auto* file = g_file_new_for_path(file_path.path);
    auto* copy_file = g_file_new_for_path(copy_path.path);
    auto* subdir = g_file_new_for_path(subdir_path.path);
    auto* dir_file = g_file_new_for_path(dir.path);
    auto* rw_file = g_file_new_for_path(rw_path.path);

    auto original = to_bytes("alpha");
    auto* output = co_await cardio::gio::create(file, G_FILE_CREATE_NONE);
    auto* output_info = co_await cardio::gio::query_info(
        output,
        G_FILE_ATTRIBUTE_STANDARD_TYPE);
    CHECK(
        g_file_info_get_file_type(output_info) ==
        G_FILE_TYPE_REGULAR);
    g_object_unref(output_info);

    CHECK_EQ(
        co_await cardio::gio::write_all(G_OUTPUT_STREAM(output), original),
        original.size());
    co_await cardio::gio::close(G_OUTPUT_STREAM(output));
    g_object_unref(output);

    auto* info = co_await cardio::gio::query_info(
        file,
        G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NONE);
    CHECK(
        g_file_info_get_file_type(info) ==
        G_FILE_TYPE_REGULAR);
    CHECK_EQ(
        static_cast<std::size_t>(g_file_info_get_size(info)),
        original.size());
    g_object_unref(info);

    auto loaded = co_await cardio::gio::load_contents(file);
    CHECK_EQ(contents_to_string(loaded), std::string("alpha"));

    auto replacement = to_bytes("beta");
    (void)co_await cardio::gio::replace_contents(
        file,
        replacement,
        nullptr,
        false,
        G_FILE_CREATE_NONE);
    loaded = co_await cardio::gio::load_contents(file);
    CHECK_EQ(contents_to_string(loaded), std::string("beta"));

    auto* input = co_await cardio::gio::read(file);
    auto* input_info = co_await cardio::gio::query_info(
        input,
        G_FILE_ATTRIBUTE_STANDARD_SIZE);
    CHECK_EQ(
        static_cast<std::size_t>(g_file_info_get_size(input_info)),
        replacement.size());
    g_object_unref(input_info);

    auto read_buffer = std::array<char, 4>{};
    CHECK_EQ(
        co_await cardio::gio::read_all(
            G_INPUT_STREAM(input),
            std::as_writable_bytes(std::span(read_buffer))),
        read_buffer.size());
    CHECK_EQ(
        std::string(read_buffer.data(), read_buffer.size()),
        std::string("beta"));
    co_await cardio::gio::close(G_INPUT_STREAM(input));
    g_object_unref(input);

    co_await cardio::gio::make_directory(subdir);
    auto* subdir_info = co_await cardio::gio::query_info(
        subdir,
        G_FILE_ATTRIBUTE_STANDARD_TYPE,
        G_FILE_QUERY_INFO_NONE);
    CHECK(
        g_file_info_get_file_type(subdir_info) ==
        G_FILE_TYPE_DIRECTORY);
    g_object_unref(subdir_info);

    auto* enumerator = co_await cardio::gio::enumerate_children(
        dir_file,
        G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NONE);
    auto* list = co_await cardio::gio::next_files(enumerator, 10);
    auto saw_file = false;
    auto saw_subdir = false;
    for (auto* item = list; item != nullptr; item = item->next) {
      auto* child_info = G_FILE_INFO(item->data);
      const auto* name = g_file_info_get_name(child_info);
      if (name != nullptr && std::string(name) == "file.txt") {
        saw_file = true;
      }
      if (name != nullptr && std::string(name) == "subdir") {
        saw_subdir = true;
      }
    }
    CHECK(saw_file);
    CHECK(saw_subdir);
    g_list_free_full(list, reinterpret_cast<GDestroyNotify>(g_object_unref));
    co_await cardio::gio::close(enumerator);
    g_object_unref(enumerator);

    co_await cardio::gio::copy(file, copy_file, G_FILE_COPY_NONE);
    auto copied = co_await cardio::gio::load_contents(copy_file);
    CHECK_EQ(contents_to_string(copied), std::string("beta"));

    auto from_bytes_value = to_bytes("bytes");
    auto* bytes = g_bytes_new(from_bytes_value.data(), from_bytes_value.size());
    (void)co_await cardio::gio::replace_contents_bytes(
        file,
        bytes,
        nullptr,
        false,
        G_FILE_CREATE_NONE);
    g_bytes_unref(bytes);
    loaded = co_await cardio::gio::load_contents(file);
    CHECK_EQ(contents_to_string(loaded), std::string("bytes"));

    auto* rw_stream = co_await cardio::gio::create_readwrite(
        rw_file,
        G_FILE_CREATE_NONE);
    auto* rw_info = co_await cardio::gio::query_info(
        rw_stream,
        G_FILE_ATTRIBUTE_STANDARD_TYPE);
    CHECK(
        g_file_info_get_file_type(rw_info) ==
        G_FILE_TYPE_REGULAR);
    g_object_unref(rw_info);
    co_await cardio::gio::close(G_IO_STREAM(rw_stream));
    g_object_unref(rw_stream);

    co_await cardio::gio::delete_file(copy_file);
    co_await cardio::gio::delete_file(file);
    co_await cardio::gio::delete_file(subdir);
    co_await cardio::gio::delete_file(rw_file);

    g_object_unref(rw_file);
    g_object_unref(dir_file);
    g_object_unref(subdir);
    g_object_unref(copy_file);
    g_object_unref(file);
  } catch (...) {
    group.shutdown();
    throw;
  }

  group.shutdown();
  co_return;
}

static void file_helpers_complete() {
  auto dir = temp_dir{};
  auto file_path = temp_path(dir, "file.txt");
  auto copy_path = temp_path(dir, "copy.txt");
  auto subdir_path = temp_path(dir, "subdir");
  auto rw_path = temp_path(dir, "rw.txt");

  run_glib_operation([&](cardio::dispatcher_group_glib& group) {
    return file_helpers_async(
        group,
        dir,
        file_path,
        copy_path,
        subdir_path,
        rw_path);
  });
}

//-----------------------------------------------------------------------------------------------

static cardio::promise<void> generic_submit_async(
    cardio::dispatcher_group_glib& group) {
  try {
    auto* source_object = G_OBJECT(g_memory_input_stream_new());

    const auto value = co_await cardio::gio::submit<int>(
        [source_object](
            GCancellable* cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data) {
          auto* task = g_task_new(source_object, cancellable, callback, user_data);
          g_task_return_boolean(task, TRUE);
          g_object_unref(task);
        },
        [](GObject*, GAsyncResult* result, GError** error) {
          const auto succeeded = g_task_propagate_boolean(
              G_TASK(result),
              error);
          return succeeded ? 73 : 0;
        });
    CHECK_EQ(value, 73);

    cardio::cancellation_source already_canceled;
    CHECK(already_canceled.cancel());
    auto started = false;
    auto canceled_promise = cardio::gio::submit<int>(
        [&started](
            GCancellable*,
            GAsyncReadyCallback,
            gpointer) {
          started = true;
        },
        [](GObject*, GAsyncResult*, GError**) {
          return 1;
        },
        already_canceled.get_cancellation());
    CHECK(!started);
    check_canceled(canceled_promise);

    auto gio_cancelled = cardio::gio::submit<void>(
        [source_object](
            GCancellable* cancellable,
            GAsyncReadyCallback callback,
            gpointer user_data) {
          auto* task = g_task_new(source_object, cancellable, callback, user_data);
          g_task_return_new_error(
              task,
              G_IO_ERROR,
              G_IO_ERROR_CANCELLED,
              "cancelled");
          g_object_unref(task);
        },
        [](GObject*, GAsyncResult* result, GError** error) {
          (void)g_task_propagate_boolean(G_TASK(result), error);
        });
    auto caught_cancelled = false;
    try {
      co_await gio_cancelled;
    } catch (const cardio::canceled_exception&) {
      caught_cancelled = true;
    }
    CHECK(caught_cancelled);

    auto* missing = g_file_new_for_path("/definitely/not/cardio/gio/missing");
    auto missing_promise = cardio::gio::read(missing);
    auto caught_gio_error = false;
    try {
      co_await missing_promise;
    } catch (const cardio::gio::gio_error& error) {
      caught_gio_error =
          error.domain() == G_IO_ERROR &&
          error.code() == G_IO_ERROR_NOT_FOUND;
    }
    CHECK(caught_gio_error);
    g_object_unref(missing);
    g_object_unref(source_object);
  } catch (...) {
    group.shutdown();
    throw;
  }

  group.shutdown();
  co_return;
}

static void generic_submit_and_errors_complete() {
  run_glib_operation([](cardio::dispatcher_group_glib& group) {
    return generic_submit_async(group);
  });
}

//-----------------------------------------------------------------------------------------------

int main() {
  dispatcher_features_are_reported();
  gio_helpers_reject_non_gio_dispatcher();
  stream_helpers_complete();
  file_helpers_complete();
  generic_submit_and_errors_complete();

  std::puts("cardio_gio_test: PASS");
  return 0;
}

#else

int main() {
  std::puts("cardio_gio_test: SKIP");
  return 0;
}

#endif
