# Integration With Android Looper

When the Android NDK defines `__ANDROID__`, cardio automatically enables POSIX
file descriptor waits and exposes two Android dispatcher hosts. Link the final
native binary with `libandroid`. The supported baseline is Android API 24 or
newer; defining `CARDIO_HAS_POSIX_FD=0` or enabling
`CARDIO_WITH_LINUX_IO_URING` is not supported on Android.

Use `dispatcher_host_android` on a native thread whose loop is owned by the
native application:

```cpp
static cardio::promise<void> main_async();

static void run_native_loop() {
  cardio::dispatcher_host_android dispatcher;
  auto root = main_async();
  (void)root;
  dispatcher.park();
}
```

Construction, `park()`, and destruction must occur on the same native thread.
The host uses that thread's existing `ALooper`, or prepares one if necessary,
and dispatches cardio continuations, timers, POSIX fd readiness, and existing
callback-based Looper registrations. Do not call `park()` on a Java UI thread.

Use `dispatcher_host_android_auto` when Java already owns the message loop,
such as an Activity's UI thread. Construct and destroy it from JNI calls made
on that thread. It intentionally has no `park()` method:

```cpp
#include <jni.h>
#include <memory>
#include <optional>

static std::unique_ptr<cardio::dispatcher_host_android_auto> ui_dispatcher;
static std::optional<cardio::promise<void>> ui_root;

extern "C" JNIEXPORT void JNICALL
Java_com_example_App_nativeStart(JNIEnv*, jobject) {
  ui_dispatcher =
      std::make_unique<cardio::dispatcher_host_android_auto>();
  ui_root.emplace(main_async());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_App_nativeStop(JNIEnv*, jobject) {
  ui_root.reset();
  ui_dispatcher.reset();
}
```

The current thread must already have an `ALooper`; Android Java UI threads meet
this requirement. The Java Looper remains application-owned, while cardio work
posted from any thread is resumed on the Looper-owning thread.

cardio is header-only and does not add an API-level or page-size dimension to
the distributed native artifacts. An application normally builds one final
native binary for each ABI, such as `x86_64` and `arm64-v8a`, with API 24 as its
minimum target. To make the same binary loadable on both 4 KiB and 16 KiB page
size devices, link it and every native dependency for 16 KiB compatibility,
for example with `-Wl,-z,max-page-size=16384`, and package uncompressed native
libraries with 16 KiB ZIP alignment. The application remains responsible for
ensuring that all of its native dependencies and APIs are compatible with its
declared `minSdkVersion`.

The [public header](../../include/cardio.h) contains the Android dispatcher implementation.
The [Android CI configuration](../../.github/workflows/ci.yml) lists the API levels and page sizes used for testing.

---

[All guides](../../README.md#documentation) · [日本語](../ja/android-looper.md)
