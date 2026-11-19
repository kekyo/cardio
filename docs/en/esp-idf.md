# Using FreeRTOS tasks on ESP-IDF

On a dual-core ESP32-S3, pin one FreeRTOS task to each core and call
`cardio::set_current_dispatcher(&dispatcher)` and then `dispatcher.park()` in
both tasks.

Independent continuations can execute on both cores.
A coroutine may resume on a different core after suspension.
For a persistent dispatcher, use a group with `exit_condition::exit_by_manual`.
Call `group.shutdown()` and wait for all calls to `park()` to return before destroying the dispatcher.

With `CONFIG_VFS_SUPPORT_SELECT`, `from_fd()` waits for lwIP sockets and `eventfd`.
Register the eventfd VFS once at startup, before constructing a dispatcher.

```cpp
const esp_vfs_eventfd_config_t config{2};
ESP_ERROR_CHECK(esp_vfs_eventfd_register(&config));
cardio::dispatcher_host dispatcher;
```

Allow one eventfd per live dispatcher plus the eventfds opened by
your application. [ESP-IDF VFS and eventfd](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/storage/vfs.html#eventfd)

Use descriptors in the range `0 <= fd < FD_SETSIZE` and close each descriptor after its wait finishes.
Set sockets to nonblocking mode and check for TCP EOF with `recv() == 0` after read readiness.

UART and other devices depend on the readiness directions implemented by their VFS driver.
Define `CARDIO_HAS_POSIX_FD=0` to use shared dispatchers and timers without registering eventfd.
[ESP-IDF VFS drivers](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/storage/vfs.html)

Add the `esp_event` component and define `CARDIO_WITH_ESP_EVENT=1` to await one event.
For example, after creating the default event loop, wait for a station IP address and read the copied payload from `ip.value` when `ip.error == ESP_OK`.

```cpp
auto ip = co_await cardio::from_esp_event<ip_event_got_ip_t>(IP_EVENT, IP_EVENT_STA_GOT_IP);
```

Pass a user event loop as the third argument or a cancellation signal as the fourth.
Keep the loop and dispatcher alive until the wait finishes or the promise is dropped.

Use `from_esp_event<void>()` for notifications without data.
Registration errors are available in `error` with exceptions disabled.

To signal from an ISR, write to an `eventfd` created with `EFD_SUPPORT_ISR` and handle the data on a
dispatcher task.
[ESP event loops](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/system/esp_event.html) and [ISR-capable eventfd](https://github.com/espressif/esp-idf/blob/v6.0/components/vfs/include/esp_vfs_eventfd.h)

## Refer sample code

The PlatformIO sample starts one FreeRTOS task per core to park a shared dispatcher.
A test IP event starts the TCP loopback exchange.
When it finishes, `app_main` calls `shutdown()` and waits for every worker to return from `park()`.
[Sample task setup and shutdown](../../samples/espidf/src/ip_sample.h)

From the repository root, run `make test-espidf` with PlatformIO Core, Node.js and Espressif's `qemu-xtensa`
installed.

For the original ESP32 use `ESPIDF_ENV=esp32`.
For C3 use `ESPIDF_ENV=esp32c3` and install `qemu-riscv32`.
The sample also builds for S2 and C6.
[PlatformIO sample](../../samples/espidf) and [Espressif QEMU installation](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-guides/tools/qemu.html)

To select a physical S3, use `make test-espidf-hw PORT=/dev/ttyACM0`. Linux
`/dev/serial/by-id/…` and `by-path` links also work. The runner uses esptool 5 to
check the chip type before flashing. Set `EXPECT_MAC` to check the factory MAC
and `TEST_PORT` if the console uses a separate port.
[esptool chip selection](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-options.html) and [PlatformIO test ports](https://docs.platformio.org/en/stable/core/userguide/cmd_test.html)

---

[All guides](../../README.md#documentation) · [日本語](../ja/esp-idf.md)
