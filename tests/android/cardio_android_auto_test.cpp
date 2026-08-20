// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"

#include <jni.h>

#include <cerrno>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <unistd.h>

static_assert(
    std::is_base_of_v<cardio::dispatcher, cardio::dispatcher_host_android_auto>);
static_assert(
    std::is_default_constructible_v<cardio::dispatcher_host_android_auto>);
static_assert(std::is_constructible_v<
    cardio::dispatcher_host_android_auto,
    cardio::dispatcher_group&>);

template <typename T>
concept has_park = requires(T& target) {
  target.park();
};

static_assert(!has_park<cardio::dispatcher_host_android_auto>);

namespace {
  constexpr unsigned direct_bit = 1U << 0;
  constexpr unsigned timer_bit = 1U << 1;
  constexpr unsigned fd_bit = 1U << 2;
  constexpr unsigned worker_bit = 1U << 3;
  constexpr unsigned handler_bit = 1U << 4;
  constexpr unsigned all_bits =
      direct_bit | timer_bit | fd_bit | worker_bit | handler_bit;

  struct auto_test_state {
    std::unique_ptr<cardio::dispatcher_host_android_auto> dispatcher;
    std::thread::id owner_thread;
    JavaVM* vm = nullptr;
    jobject activity = nullptr;
    jmethodID completion_method = nullptr;
    int pipe_fds[2]{-1, -1};
    std::optional<cardio::promise<void>> timer_task;
    std::optional<cardio::promise<void>> fd_task;
    std::thread worker;
    unsigned completed_bits = 0;
    bool reported = false;

    explicit auto_test_state(JNIEnv* environment, jobject target)
        : dispatcher(
              std::make_unique<cardio::dispatcher_host_android_auto>()),
          owner_thread(std::this_thread::get_id()) {
      if (environment->GetJavaVM(&vm) != JNI_OK) {
        throw std::runtime_error("GetJavaVM failed");
      }

      auto* target_class = environment->GetObjectClass(target);
      if (target_class == nullptr) {
        throw std::runtime_error("GetObjectClass failed");
      }
      completion_method = environment->GetMethodID(
          target_class, "onNativeComplete", "(Ljava/lang/String;)V");
      environment->DeleteLocalRef(target_class);
      if (completion_method == nullptr) {
        throw std::runtime_error("GetMethodID failed");
      }

      activity = environment->NewGlobalRef(target);
      if (activity == nullptr) {
        throw std::runtime_error("NewGlobalRef failed");
      }
    }

    void report(const std::string& failure) {
      if (reported) {
        return;
      }
      reported = true;

      auto* environment = static_cast<JNIEnv*>(nullptr);
      if (vm->GetEnv(
              reinterpret_cast<void**>(&environment),
              JNI_VERSION_1_6) != JNI_OK) {
        return;
      }

      auto* message = failure.empty()
          ? static_cast<jstring>(nullptr)
          : environment->NewStringUTF(failure.c_str());
      environment->CallVoidMethod(activity, completion_method, message);
      if (message != nullptr) {
        environment->DeleteLocalRef(message);
      }
    }

    void fail(const std::string& message) {
      report(message);
    }

    void complete(unsigned bit) {
      if (std::this_thread::get_id() != owner_thread) {
        fail("cardio callback ran outside the Java UI thread");
        return;
      }
      if ((completed_bits & bit) != 0) {
        fail("cardio callback ran more than once");
        return;
      }

      completed_bits |= bit;
      if (completed_bits == all_bits) {
        report({});
      }
    }
  };

  auto_test_state* state = nullptr;

  void report_direct(
      JNIEnv* environment,
      jobject activity,
      const std::string& failure) {
    auto* target_class = environment->GetObjectClass(activity);
    if (target_class == nullptr) {
      return;
    }
    const auto completion_method = environment->GetMethodID(
        target_class, "onNativeComplete", "(Ljava/lang/String;)V");
    environment->DeleteLocalRef(target_class);
    if (completion_method == nullptr) {
      return;
    }

    auto* message = environment->NewStringUTF(failure.c_str());
    environment->CallVoidMethod(activity, completion_method, message);
    environment->DeleteLocalRef(message);
  }

  cardio::promise<void> wait_for_timer(auto_test_state* target) {
    co_await cardio::promises::delay(5);
    target->complete(timer_bit);
  }

  cardio::promise<void> wait_for_fd(auto_test_state* target) {
    const auto events = co_await cardio::from_fd(
        target->pipe_fds[0], cardio::fd_event::read);
    if ((events & cardio::fd_event::read) == cardio::fd_event::none) {
      target->fail("fd wait did not report read readiness");
      co_return;
    }
    target->complete(fd_bit);
  }

  void close_fd(int& fd) noexcept {
    if (fd >= 0) {
      (void)::close(fd);
      fd = -1;
    }
  }
}  // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_example_cardio_CardioActivity_nativeStart(
    JNIEnv* environment,
    jobject activity) {
  if (state != nullptr) {
    report_direct(environment, activity, "native test is already running");
    return;
  }

  try {
    state = new auto_test_state(environment, activity);
    if ((state->dispatcher->get_feature() &
         cardio::dispatcher_feature::android) ==
        cardio::dispatcher_feature::none) {
      state->fail("Android dispatcher feature is missing");
      return;
    }

    if (::pipe(state->pipe_fds) == -1) {
      state->fail("pipe failed");
      return;
    }

    state->timer_task.emplace(wait_for_timer(state));
    state->fd_task.emplace(wait_for_fd(state));
    auto* const target = state;
    cardio::internal::dangerous_schedule_later__(
        target->dispatcher.get(), [target] { target->complete(direct_bit); });

    target->worker = std::thread([target] {
      const auto value = char{'x'};
      auto result = ssize_t{};
      do {
        result = ::write(target->pipe_fds[1], &value, 1);
      } while (result == -1 && errno == EINTR);
      const auto error = errno;

      cardio::internal::dangerous_schedule_later__(
          target->dispatcher.get(), [target, result, error] {
            if (result != 1) {
              target->fail(
                  "worker pipe write failed with errno " +
                  std::to_string(error));
              return;
            }
            target->complete(worker_bit);
          });
    });
  } catch (const std::exception& exception) {
    if (state != nullptr) {
      state->fail(exception.what());
    } else {
      report_direct(environment, activity, exception.what());
    }
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_cardio_CardioActivity_nativeHandlerRan(
    JNIEnv* environment,
    jobject activity) {
  if (state == nullptr) {
    report_direct(environment, activity, "native test is not running");
    return;
  }
  state->complete(handler_bit);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_cardio_CardioActivity_nativeStop(
    JNIEnv* environment,
    jobject) {
  auto* target = state;
  if (target == nullptr) {
    return;
  }
  state = nullptr;

  if (target->worker.joinable()) {
    target->worker.join();
  }
  target->timer_task.reset();
  target->fd_task.reset();
  close_fd(target->pipe_fds[0]);
  close_fd(target->pipe_fds[1]);
  if (target->activity != nullptr) {
    environment->DeleteGlobalRef(target->activity);
    target->activity = nullptr;
  }
  target->dispatcher.reset();
  delete target;
}
