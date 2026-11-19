# POSIXファイルディスクリプタの待機

POSIX環境では、ファイルディスクリプタのreadinessを `promise` として待機できます。
この機能は `_POSIX_C_SOURCE` または `CARDIO_HAS_POSIX_FD=1` が有効なビルドでのみ公開されます。
非POSIXビルドでは、`fd_event` と `from_fd()` は公開されません。
`cardio::from_fd()` は、指定したファイルディスクリプタが読み取り可能、または書き込み可能になるまで待機し、実際に発生した状態を `fd_event` で返します:

```cpp
#include <unistd.h>

// POSIX fdを使用する非同期処理
static cardio::promise<void> main_async() noexcept {
  // パイプを生成する
  int fds[2];
  pipe(fds);

  // パイプにデータを送出
  write(fds[1], "x", 1);

  // パイプの端点が読み取り可能になるまで待機する
  auto events = co_await cardio::from_fd(
      fds[0], cardio::fd_event::read);

  if ((events & cardio::fd_event::read) != cardio::fd_event::none) {
    // fdは読み取り可能
    printf("Ready for read.\n");
  }

  if ((events & cardio::fd_event::hangup) != cardio::fd_event::none) {
    // fdはhangupした
    printf("Hanged up.\n");
  }

  if ((events & cardio::fd_event::error) != cardio::fd_event::none) {
    // fdでエラーが発生した
    printf("Error occurred.\n");
  }

  close(fds[0]);
  close(fds[1]);
}

int main() {
  cardio::dispatcher_host d;

  // 非同期パイプ処理を開始
  auto p = main_async();
  (void)p;

  // promiseの継続に加えて、登録されたfdのreadinessも待機する
  d.park();

  return 0;
}
```

待機対象は `fd_event::read` と `fd_event::write` で指定します。
複数の条件を同時に待機する場合は、ビット演算で組み合わせます:

```cpp
// fdの読み取りと書き込み両方を待機する
auto events = co_await cardio::from_fd(
    fd, cardio::fd_event::read | cardio::fd_event::write);

if ((events & cardio::fd_event::write) != cardio::fd_event::none) {
  // fdは書き込み可能
}
```

- `from_fd()` はファイルディスクリプタの所有権を取得しません。
  そのため、待機中にpromiseを破棄してもfdはcloseされず、fdのcloseは呼び出し元が行う必要があります。
- 例外有効ビルドでは、`from_fd(fd, interests, cancellation)` を使用して待機をキャンセルできます。
  readinessより先にキャンセルが要求された場合、返されたpromiseは `canceled_exception` による失敗として完了します。
- `fd < 0` や、`fd_event::none` のように読み取り・書き込みのどちらも指定していない場合、`from_fd()` は失敗したpromiseを返します。
  この失敗は、通常のpromiseと同様に `co_await`、`try_result()`、`unsafe_result()` で結果を取り出すときに例外として再送出されます。
- `fd_event::error` と `fd_event::hangup` は、POSIX `poll()` の結果から返される状態です。
  これらは待機条件として指定するものではなく、readiness完了後の結果として確認します。

---

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/posix-fd.md)
