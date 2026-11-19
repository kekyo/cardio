# GLib/GTK Integration and GIO File I/O

## Integration With GLib 2 (GTK)

In environments where GLib 2 or GTK is available, you can integrate GLib's
scheduler by specifying `CARDIO_WITH_GLIB=1`.
In other words, you can integrate asynchronous processing using cardio within
applications such as GTK.

To use this feature, install the GLib development headers and libraries, and
link GLib.
For example, on Debian/Ubuntu, you can install them with APT packages as follows:

```bash
sudo apt update
sudo apt install -y libglib2.0-dev pkg-config
```

At compile time, define `CARDIO_WITH_GLIB=1` and pass GLib compile and link
options with `pkg-config`:

```bash
g++ -std=c++20 -Iinclude -DCARDIO_WITH_GLIB=1 app.cpp -o app $(pkg-config --cflags --libs glib-2.0)
```

GLib integration uses `dispatcher_group_glib`, `dispatcher_host_glib`, and
`dispatcher_host_glib_auto`.
`dispatcher_group_glib` manages the application's shutdown timing. For the role of
`dispatcher_group`, see [Switching Dispatchers](./dispatchers.md#switching-dispatchers).

```cpp
int main() {
  // Initialize the dispatcher with GLib co-waiting enabled.
  cardio::dispatcher_group_glib group;
  cardio::dispatcher_host_glib d(group);

  // (Code that uses GLib, GTK, or another GLib-based message pump.
  //  Call shutdown() at the time of application exit.)
  // {
  //   //  :
  //   //  :
  //   group.shutdown();
  // }

  // Park the main thread while waiting for both GLib sources and cardio work.
  d.park();

  return 0;
}
```

- `dispatcher_host_glib` waits for GLib sources and cardio work together from
  `park()`. `park_policy` controls whether ready GLib sources or cardio
  continuations run first.
- `dispatcher_host_glib_auto` attaches a cardio `GSource` to the group's
  `GMainContext`. Any application-owned GLib message pump for that context can
  execute cardio continuations, timer waits, POSIX fd waits, and io_uring
  completions. Its `park()` is a compatibility wrapper around the same
  `GMainContext`, and GLib source priority determines ordering.
- Calling `dispatcher_group_glib::shutdown()` causes `park()` to exit.
  Unlike when using `dispatcher` alone, `park()` does not automatically exit when
  asynchronous continuations no longer exist.

## GIO File I/O Helpers

When GLib integration is enabled, GIO file I/O helpers can be enabled with
`CARDIO_WITH_GIO=1`. This also requires `CARDIO_WITH_GLIB=1` and
`gio-2.0 >= 2.44`:

```bash
g++ -std=c++20 -Iinclude -DCARDIO_WITH_GLIB=1 -DCARDIO_WITH_GIO=1 app.cpp -o app $(pkg-config --cflags --libs gio-2.0)
```

The helpers are available in `cardio::gio`. They wrap stable, single-shot
`*_async()` / `*_finish()` GIO operations for streams, files, file enumerators,
and file stream `query_info()` calls. Operations outside that scope can still be
adapted with `cardio::gio::submit<T>(start, finish)`.

For example, can copy full file contents through GIO helpers:

```cpp
static cardio::promise<void> copy_file_contents_async(
    GFile* source,
    GFile* destination) {
  auto contents = co_await cardio::gio::load_contents(source);
  co_await cardio::gio::replace_contents(
      destination,
      std::span<const std::byte>(contents.bytes.data(), contents.bytes.size()),
      nullptr,
      false,
      G_FILE_CREATE_NONE);
}
```

Before starting an operation, the helper checks
`get_current_dispatcher().get_feature()` for `dispatcher_feature::gio`.
`dispatcher_host_glib` and `dispatcher_host_glib_auto` report both
`dispatcher_feature::glib` and `dispatcher_feature::gio` when GIO support is
compiled in. A regular `dispatcher_host` does not report `gio`, so GIO helpers
return a rejected promise with `std::invalid_argument`.

Ownership and lifetime follow GIO conventions:

- Returned `GObject*`, `GBytes*`, and `GList*` values are raw caller-owned values.
  Release them with the matching GIO API such as `g_object_unref()`,
  `g_bytes_unref()`, or `g_list_free_full(list, g_object_unref)`.
- Buffers passed as `std::span` to read, write, and replace helpers must remain
  valid until the returned promise completes.
- Helpers that accept `GBytes*` rely on GIO keeping the bytes alive for the
  operation; the caller may release its own reference after the async call has
  been started.
- `GError` failures are converted to `cardio::gio::gio_error`, which stores the
  original domain, code, and message. `G_IO_ERROR_CANCELLED` is converted to
  `cardio::canceled_exception`.

The cancellable `submit<T>()` overload creates a helper-owned `GCancellable`.
When the supplied `cardio::cancellation` is requested, the helper calls
`g_cancellable_cancel()` and the promise completes when GIO invokes the callback.

---

[All guides](../../README.md#documentation) · [日本語](../ja/glib-gio.md)
