# ESP-IDFでFreeRTOSタスクを使う

ESP32-S3では、各コアに1つずつFreeRTOSタスクを作り、同じ`dispatcher_host`を両方から`park()`できます。

各タスクで必要な準備は、`park()`の前に`cardio::set_current_dispatcher(&dispatcher)`を呼ぶことです。独立した継続は両コアで実行でき、同じコルーチンでも中断後に別のコアで再開することがあります。
常駐させる場合は`exit_condition::exit_by_manual`のグループを使い、`group.shutdown()`の後、すべての`park()`が戻ってからdispatcherを破棄してください。

`CONFIG_VFS_SUPPORT_SELECT`を有効にすると、`from_fd()`でlwIPソケットや`eventfd`の準備を待てます。
dispatcherを作る前に、アプリケーションの起動時に一度、次のように`eventfd`を登録してください。

```cpp
const esp_vfs_eventfd_config_t config{2};
ESP_ERROR_CHECK(esp_vfs_eventfd_register(&config));
cardio::dispatcher_host dispatcher;
```

`max_fds`には同時に使うdispatcherごとに1個と、アプリが開く`eventfd`の個数を含めます。[ESP-IDFのVFSとeventfd](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/storage/vfs.html#eventfd)

待機するfdは`0 <= fd < FD_SETSIZE`の範囲です。
ソケットをノンブロッキングに設定し、読み取り通知後の`recv() == 0`でTCPの正常切断を判定します。

UARTなどのデバイスで待てるのは、VFSドライバーが`select()`で通知できる方向だけです。
fd待機が不要なアプリでは`CARDIO_HAS_POSIX_FD=0`を指定すれば、タイマーや共有dispatcherを`eventfd`の登録なしで使えます。
[ESP-IDFのVFSドライバー](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/storage/vfs.html)

ESPイベントを待つ場合は`esp_event`コンポーネントを追加し、`CARDIO_WITH_ESP_EVENT=1`でビルドします。
次の例で待つのは、作成済みの既定のイベントループに届く最初のIP取得イベントです。
結果の`error`が`ESP_OK`なら、`value`からコピー済みのデータを読めます。

```cpp
auto ip = co_await cardio::from_esp_event<ip_event_got_ip_t>(IP_EVENT, IP_EVENT_STA_GOT_IP);
```

第3引数で独自のイベントループ、第4引数でキャンセルを指定できます。
待機を終えるかpromiseを破棄してから、ループとdispatcherを破棄してください。

データのない通知を待つ形式は`from_esp_event<void>()`です。
イベントの登録に失敗すると、例外無効でも`error`で理由を受け取れます。

割り込みから通知するには`EFD_SUPPORT_ISR`付きの`eventfd`に書き込み、データの読み取りと継続の実行をタスクに任せてください。
[ESPイベント](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/system/esp_event.html)・[割り込みから使うeventfd](https://github.com/espressif/esp-idf/blob/v6.0/components/vfs/include/esp_vfs_eventfd.h)

## サンプルコードを参照する

PlatformIOのサンプルは、各コアに1つのタスクを起動してdispatcherを共有します。
テスト用のIP取得イベントでTCPループバック通信を始め、通信が終わると`app_main`がすべてのタスクの`park()`を解除します。
[サンプルのタスク起動と停止](../../samples/espidf/src/ip_sample.h)

リポジトリのルートで`make test-espidf`を実行すると、S3向けの共通テストがEspressif版QEMUで動きます。
PlatformIO Core、Node.js、QEMUの`qemu-xtensa`が必要です。

初代ESP32は`ESPIDF_ENV=esp32`、C3は`ESPIDF_ENV=esp32c3`と`qemu-riscv32`を使います。
S2とC6はビルドまで確認しています。
[PlatformIOサンプル](../../samples/espidf)・[Espressif版QEMUの導入](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-guides/tools/qemu.html)

S3実機の実行例は`make test-espidf-hw PORT=/dev/ttyACM0`です。
`PORT`にはLinuxの`/dev/serial/by-id/…`や`by-path`のパスも指定できます。
`esptool` 5で書き込み前に機種を確認し、`EXPECT_MAC`を指定した場合は工場出荷時のMACアドレスも照合します。
受信用のポートを分けるときは`TEST_PORT`を指定してください。
[esptoolの機種指定](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-options.html)・[PlatformIOのポート指定](https://docs.platformio.org/en/stable/core/userguide/cmd_test.html)

---

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/esp-idf.md)
