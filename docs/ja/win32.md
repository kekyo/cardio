# Win32 HANDLEの待機

Windowsでは、`CARDIO_HAS_WIN32_HANDLE=1` が有効な場合に、Win32 HANDLEを `promise` として待機できます。
`_WIN32` ビルドではデフォルトで有効です。

Windows XP向けには、i686で`_WIN32_WINNT=0x0501`、amd64で`0x0502`を指定します。
`WINVER`も同じ値に揃えてください。MinGW GCC 12のwin32スレッドモデルでも、
cardioの同期とスレッド機能を利用できます。例外を無効にした構成にも対応しています。

検証したMinGW GCC 12.2では、`if (co_await ...)`のような条件式でコルーチンのアドレスが崩れる問題を確認しました。
`const auto value = co_await operation;`のように結果を変数へ受け取ってから判定してください。
[cardioを使わない再現コード](../../tests/windows/compiler-await.cpp)と
[GCC 12.2のコルーチン実装](https://github.com/gcc-mirror/gcc/blob/releases/gcc-12.2.0/gcc/cp/coroutines.cc)を参照できます。

XP向けバイナリでは、実行時に`GetProcAddress()`で`CancelIoEx()`を検出します。
利用できるOSでは操作ごとに取り消し、利用できない場合は発行元スレッドで`CancelIo()`を呼び出します。
`_WIN32_WINNT >= 0x0600`では`CancelIoEx()`を直接利用する構成です。
これらのマクロはビルド対象を表し、実行中のOSバージョンを表すものではありません。
[Windowsヘッダーの設定](https://learn.microsoft.com/en-us/windows/win32/winprog/using-the-windows-headers)と
[APIの動的取得](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-getprocaddress)を参照してください。

`dispatcher_host_win32::park()` は `MsgWaitForMultipleObjectsEx()` を使用するため、
登録されたHANDLEの待機とWin32メッセージpumpを同じスレッドで行えます。
これは、COMのアパートメントスレッドやマーシャリングの運用にも適しています。
デフォルトでは、キュー済みのWin32メッセージは、キュー済みまたはinlineのcardio継続より先にpumpされます。
cardio継続を先に実行するには、`cardio::park_policy::continuation_first` を `park()` の第2引数に指定します。

`dispatcher_host_win32_auto` は message-only window を作成し、cardio継続、timer、Win32 HANDLE待機をprivateなWin32メッセージ経由で配送します。
`park()` を呼び出さない既存またはモーダルなWin32 message pumpが動作する可能性がある場合に使用します。
このdispatcherは同じスレッドで構築し、`park()` も同じスレッドで呼び出してください。
cardio workの実行タイミングはWin32 message queueの順序で決まります。

手動dispatcherを作成元スレッドに固定する場合は、次のように指定します。
別スレッドからの`park()`はエラーです。引数を省略した場合は、従来どおり複数のスレッドで実行できます。

```cpp
cardio::dispatcher_host_win32 dispatcher(
    cardio::dispatcher_thread_policy::current_thread);
```

```cpp
#include <windows.h>

static cardio::promise<void> wait_async(HANDLE handle) {
  auto result = co_await cardio::from_win32_handle(handle);
  if (result == cardio::win32_handle_event::signaled) {
    printf("The event was signaled.\n");
  }
}
```

外部で投入したOVERLAPPED I/Oは、`OVERLAPPED` operationを待機して転送バイト数を取得できます:

```cpp
OVERLAPPED overlapped{};
overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

ReadFile(file, buffer, size, nullptr, &overlapped);

auto result = co_await cardio::from_win32_overlapped(file, overlapped);
printf("Transferred: %lu\n", result.bytes_transferred);
```

単発I/O operationの `OVERLAPPED` 構造体をcardioに所有させる場合は、
`cardio::win32` の補助関数を使用します:

```cpp
char buffer[4096]{};
auto read_size = co_await cardio::win32::read(
    file,
    std::as_writable_bytes(std::span<char>(buffer)));

auto write_size = co_await cardio::win32::write(
    file,
    std::as_bytes(std::span<const char>(buffer, read_size)),
    0);
```

- `from_win32_handle()` と `from_win32_overlapped()` は、HANDLE、`OVERLAPPED` 構造体、
  `OVERLAPPED::hEvent` の所有権を取得しません。
- `win32::submit()`、`win32::read()`、`win32::write()` は、
  native operationが完了するまで `OVERLAPPED` 構造体とmanual-reset eventを所有します。
  HANDLEとI/O bufferの所有権は取得しません。
- `OVERLAPPED::hEvent` が `nullptr` の場合は、ファイルHANDLE自体を待機します。
  同じHANDLEで複数のoperationを同時に実行する場合は、完了を区別できるように各 `OVERLAPPED` に
  個別のmanual-reset eventを指定してください。
- `from_win32_overlapped()` のキャンセルはpromiseの待機だけをキャンセルします。
  ネイティブI/Oの取消やHANDLEのcloseは行いません。`win32`のoperationでは、
  利用可能な取消APIを呼び出し、
  native operationの完了後に返されたpromiseを完了させます。
- Win32待機backendは `MAXIMUM_WAIT_OBJECTS` の制約を受けます。通常、この値は64です。
  dispatcherのwakeup eventとmessage queueが待機slotを消費するため、
  1つのdispatcherで同時に待機できるユーザーHANDLEは、最大で `MAXIMUM_WAIT_OBJECTS - 2` 個です。

`CancelIoEx()`を利用できないOSでは、`win32::submit()`、`read()`、`write()`を
スレッドに固定したdispatcherの所有スレッドから呼び出してください。
`dispatcher_host_win32_auto`、または上記の`current_thread`指定を使用します。
固定されていないdispatcherからの投入は、I/Oを開始せず`ERROR_NOT_SUPPORTED`で失敗します。
未完了のpromiseを破棄する場合も所有スレッドで行い、dispatcherを操作より長く保持してください。
モーダルなGUIループで取消を処理する場合はauto版が必要です。

同じOS条件では、同じHANDLEで同時に実行できるcardioの操作は1つです。
未完了の操作がある間の追加投入は`ERROR_BUSY`で失敗します。
同じHANDLEに外部コードからI/Oを発行しないでください。
これは、[CancelIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelio)が
同じスレッド・HANDLEの未完了I/Oをまとめて取り消すための制約です。
動的に`CancelIoEx()`を取得できるOSには、この同時実行制限はありません。

取消要求が成功しても、完了までの時間はドライバーに依存します。
HANDLE、バッファ、`OVERLAPPED`を完了前に解放・再利用しないでください。
取消と正常完了が競合した場合は、正常完了することもあります。
詳細は[CancelIoExの契約](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)を参照してください。

## Win32 I/O completion portヘルパー

Windowsのfileやpipe I/OでI/O completion portを使用する場合は、
`cardio::io_completion_port` を作成し、`cardio::iocps` からoperationを投入します:

```cpp
cardio::io_completion_port port;
char buffer[4096]{};

auto read_size = co_await cardio::iocps::read(
    port,
    file,
    std::as_writable_bytes(std::span<char>(buffer)));

auto write_size = co_await cardio::iocps::write(
    port,
    file,
    std::as_bytes(std::span<const char>(buffer, read_size)),
    0);
```

- `io_completion_port` はcompletion portと1つのpump threadを所有します。
  pump threadでI/Oの開始、取消、完了を処理し、promiseの継続を投入元dispatcherへ戻します。
- `iocps::submit()` を使うと、他の単発 `OVERLAPPED` operationもpromise化できます。
  startとcompletionのコールバックはpump threadで実行されます。
  startは非同期I/Oの投入後すぐに戻り、completionは`win32_iocp_completion`を受け取ります。
- HANDLEとI/O bufferの所有権は取得しません。完了まで呼び出し側で有効に保つ必要があります。
- 同じHANDLEを `from_win32_overlapped()` や別のcompletion portと混在させないでください。
  IOCPヘルパーはHANDLEを自身のcompletion portへ関連付けます。
  関連付けを保持するため、このportを利用する間は関連付けたHANDLEを開いたままにしてください。

IOCPでは、`CancelIo()`へのフォールバックもpump threadで実行するため、
投入元dispatcherをスレッドに固定する必要はありません。
同じHANDLEの未完了操作を1つに制限する条件は、イベント方式と共通です。
取消通知は登録元dispatcherを経由するため、そのdispatcherも実行を続けてください。
開始コールバックをpump threadへ配送するので、開始失敗も非同期に通知される場合があります。

受付上限は、待機中と実行中を合わせて既定で256操作です。
`cardio::io_completion_port port(64);`のように構築時に変更できます。
上限を超えた操作は開始せず、`ERROR_NOT_ENOUGH_QUOTA`で失敗します。
startとcompletionのコールバックはpump threadを止める処理を避け、I/O投入後に例外を投げないでください。
portの破棄はこれらのコールバックの外で行います。
破棄時には未完了I/Oを取り消し、完了を回収してからpump threadを終了します。
ドライバーが取消を完了しない場合は、portの破棄も完了しません。

---

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/win32.md)
