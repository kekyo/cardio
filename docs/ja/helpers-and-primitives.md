# 補助関数と非同期プリミティブ

## 補助関数

cardioは、補助関数を提供しています。これらは既定で有効となっています。
`CARDIO_WITH_SUPPLEMENTAL=0` で無効化出来ます。
io_uringの補助関数は `CARDIO_WITH_LINUX_IO_URING=1` が定義されている場合にのみ使用できます。
Win32の補助関数は `CARDIO_HAS_WIN32_HANDLE=1` が定義されている場合にのみ使用できます。

|関数|詳細|
|:----|:----|
|`promises::delay()`|指定された時間(msec)待機するpromiseを返します|
|`promises::all()`|JavaScriptの`Promise.all()`と同様に、すべての入力promiseが完了したときに完了するpromiseを返します|
|`promises::start_new()`|新しいワーカースレッドでcallableを実行し、その結果を表すpromiseを返します|
|`cancellations::timeout()`|JavaScriptの`AbortSignal.timeout()`と同様に、指定された時間(msec)後にキャンセル要求する`cancellation_source`を返します|
|`cancellations::any()`|JavaScriptの`AbortSignal.any()`と同様に、入力`cancellation`のいずれかがキャンセルされたときにキャンセル要求する`cancellation_source`を返します|
|`io_urings::submit()`|io_uringの単発operationを直接投入し、完了フィールドを返します|
|`io_urings::read()`|io_uringの非同期read operationを投入し、読み取ったバイト数を返します|
|`io_urings::write()`|io_uringの非同期write operationを投入し、書き込んだバイト数を返します|
|`io_urings::fsync()`|io_uringの非同期fsync operationを投入します|
|`io_urings::open()`、`io_urings::close()`、`io_urings::statx()`|fdとメタデータに関するoperationを投入します|
|`io_urings::rename()`、`io_urings::unlink()`、`io_urings::mkdir()`|パス変更operationを投入します|
|`win32::submit()`|helperが所有するWin32 `OVERLAPPED` の単発operationを投入し、完了フィールドを返します|
|`win32::read()`|Win32 `ReadFile()` の非同期operationを投入し、読み取ったバイト数を返します|
|`win32::write()`|Win32 `WriteFile()` の非同期operationを投入し、書き込んだバイト数を返します|
|`iocps::submit()`|helperが所有するWin32 IOCP単発operationを投入し、完了フィールドを返します|
|`iocps::read()`|Win32 IOCP `ReadFile()` operationを投入し、読み取ったバイト数を返します|
|`iocps::write()`|Win32 IOCP `WriteFile()` operationを投入し、書き込んだバイト数を返します|

### delayとtimeoutについて

`promises::delay()` と `cancellations::timeout()` は、現在の dispatcher が持つ内部 timer queue で時間経過を監視します。
タイムアウトごとの timer fd やワーカースレッドは生成しません。

時間経過は dispatcher が `park()` されている間に検出されます。
`cancellations::timeout()` が返す `cancellation_source` は、dispatcher が期限切れ timer を処理した時点でキャンセル状態になり、
登録済みのコールバックは登録時の dispatcher にキューイングされます。

### ワーカースレッドで処理を開始する

`promises::start_new()` は、1つのcallableに対して新しいワーカースレッドを生成し、
呼び出し元の現在のdispatcherに属するpromiseを返します。
ブロッキング処理やCPU負荷の高い処理を、cardioのcore dispatcher実装に含めずに実行したい場合に使用できます。

```cpp
static cardio::promise<int> worker_async() {
  co_await cardio::promises::delay(10);
  co_return 42;
}

int main() {
  cardio::dispatcher_host d;

  auto result = cardio::promises::start_new([] {
    return worker_async();
  });

  d.park();

  printf("result = %d\n", result.unsafe_result());
  return 0;
}
```

callableは、値、`void`、`promise<T>`、`promise<void>` を返すことができます。
promiseを返した場合、`start_new()` はワーカースレッド上に独立したdispatcherを設定し、
そのpromiseが完了するまでworker dispatcherを `park()` します。
`start_new()` は呼び出しごとに1つのスレッドを生成し、スレッドプールは使用しません。

キャンセルは協調的に処理します。callableに `cardio::cancellation` をcaptureし、
キャンセル対応operationへ渡すか、自分の処理の中で能動的に確認してください。
結果が不要な場合は、返されたpromiseを `fire_and_forget()` で完了まで生存させることもできます:

```cpp
cardio::fire_and_forget(cardio::promises::start_new([] {
  return worker_async();
}));
```

## 非同期プリミティブ

cardioは `cardio::primitives` 名前空間にpromiseベースのプリミティブも提供します。
これらは既定で有効となっています。`CARDIO_WITH_PRIMITIVES=0` で無効化できます。

|型|詳細|
|:----|:----|
|`primitives::mutex`|非同期排他制御を行います|
|`primitives::semaphore`|同時に保持できる数を制限します|
|`primitives::reader_writer_lock`|複数readerと単一writerを制御します|
|`primitives::conditional`|`trigger()` ごとにwaiterを1件解放します|
|`primitives::manually_conditional`|手動でraise/dropできるconditional stateです|

`lock_handle` はスコープを抜けると自動的に解放されます。必要なら `release()` で明示解放することもできます:

```cpp
static cardio::promise<void> update_async(
    cardio::primitives::mutex& locker) {
  auto handle = co_await locker.lock();
  // Critical section.
}
```

例外有効ビルドでは、lockやwait operationに `cardio::cancellation` を渡せます。
待機中にキャンセルされた場合は `canceled_exception` による失敗として完了します。

---

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/helpers-and-primitives.md)
