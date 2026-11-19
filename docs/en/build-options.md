# Shared Runtime and Exception-Disabled Builds

## Shared Runtime Library

cardio is header-only by default. If an application loads C++ plugins or other
dynamic modules that also use cardio, each module may otherwise get its own
header-defined thread-local runtime state. In that case, build the optional
shared runtime library and compile every module with `CARDIO_SHARED_LIB=1`:

```bash
make -C samples/libcardio -f Makefile.posix
g++ -std=c++20 -Iinclude -DCARDIO_SHARED_LIB=1 app.cpp \
  -Lbuild/libcardio -lcardio -Wl,-rpath,'$ORIGIN/build/libcardio'
```

When building the shared runtime itself, define both `CARDIO_SHARED_LIB=1` and
`CARDIO_BUILD_SHARED_LIB=1`. The bundled `samples/libcardio/Makefile.posix`
does this for `libcardio.so`, and `samples/libcardio/Makefile.win32` builds
`libcardio.dll` with a MinGW import library.

All modules that use the shared runtime must be built with the same cardio configuration macros,
such as `CARDIO_WITH_GLIB`,
`CARDIO_WITH_LINUX_IO_URING`, `CARDIO_HAS_POSIX_FD`, and
`CARDIO_HAS_EXCEPTIONS`. They must also use a compatible C++ ABI. This mode only
shares cardio's runtime thread-local state; it does not make the C++ API a stable plugin ABI.

Before unloading a plugin, stop new work, cancel or complete plugin-owned operations,
and drain pending promises and continuations. A queued coroutine or
callback whose code lives in an unloaded module can no longer be resumed safely.

## Exception-Disabled Builds

In environments where C++ exceptions are disabled, such as with gcc's
`-fno-exceptions` option, cardio excludes exception-related features from
compilation.
In this case, `CARDIO_HAS_EXCEPTIONS` becomes `0`:

```bash
g++ -std=c++20 -fno-exceptions -Iinclude app.cpp
```

In exception-disabled builds, features that create failed promises cannot be
used.
Specifically, `rejected()`, `promise_source<T>::reject()`,
`promise_source<T>::try_reject()`, `promise_source<T>::cancel()`,
`promise_source<T>::try_cancel()`,
`cancellation::throw_if_cancellation_requested()`, and
`dispatcher::unhandled_exception()` are not declared.
Also, `try_result()` and `unsafe_result()` do not rethrow failure exceptions.
If an incomplete `promise_source<T>` is destroyed, failure cannot be represented
as a value, so `std::terminate()` is called.

Failures that could previously only be represented as exceptions, such as
library internal initialization failures, invalid arguments, and I/O submit
failures, are consolidated into `std::terminate()`.
If you want to handle errors as values, wrap them in a value type on the caller
side, such as `promise<expected-like-type>`.

Cancellation notifications themselves through `cancellation_source` and
`cancellation` can still be used in exception-disabled builds.
However, APIs that represent cancellation as a `promise<T>` failure are available
only in exception-enabled builds.

---

[All guides](../../README.md#documentation) · [日本語](../ja/build-options.md)
