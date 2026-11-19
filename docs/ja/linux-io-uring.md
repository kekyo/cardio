# Linux io_uringで非同期I/Oを実装

Linuxでは、io_uringを使用した、完全非同期I/Oを実装できます。
この機能はデフォルトでは無効です。使用するには `CARDIO_WITH_LINUX_IO_URING=1` を定義し、liburingの開発ヘッダとライブラリを導入して、`liburing` をリンクしてください。
例えば、Debian/Ubuntuでは以下のようにAPTパッケージで導入できます:

```bash
sudo apt update
sudo apt install -y liburing-dev pkg-config
```

コンパイル時には、`CARDIO_WITH_LINUX_IO_URING=1` を定義し、`pkg-config` でliburingのコンパイルオプションとリンクオプションを渡します:

```bash
g++ -std=c++20 -Iinclude -DCARDIO_WITH_LINUX_IO_URING=1 app.cpp -o app $(pkg-config --cflags --libs liburing)
```

以下に、io_uringを使用して非同期でファイルのコピーを行う例を示します。
io_uringを使用するために `cardio::io_uring` 構造体を初期化し、 `cardio::io_urings` 名前空間の補助関数を使用します:

```cpp
#include <array>
#include <cstddef>
#include <fcntl.h>
#include <span>
#include <stdexcept>
#include <unistd.h>

// io_uringを使用する非同期ファイルコピー
static cardio::promise<void> main_async() noexcept {
  // バッファを準備
  auto buffer = std::array<std::byte, 4096>{};
  auto bytes = std::as_writable_bytes(std::span(buffer));

  // io_uringの準備
  cardio::io_uring io;

  // コピー元とコピー先fdをオープン
  auto source_fd = open("input.bin", O_RDONLY);
  auto destination_fd = open("output.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);

  while (true) {
    // source_fdから非同期に読み取る
    auto read_size = co_await cardio::io_urings::read(io, source_fd, bytes);
    if (read_size == 0) {
      break;
    }

    // 読み取った範囲だけをdestination_fdへ非同期に書き込む
    auto remaining = std::span<const std::byte>(bytes.data(), read_size);
    while (!remaining.empty()) {
      auto write_size =
          co_await cardio::io_urings::write(io, destination_fd, remaining);
      if (write_size == 0) {
        throw std::runtime_error("write made no progress");
      }
      remaining = remaining.subspan(write_size);
    }
  }

  close(source_fd);
  close(destination_fd);
}

int main() {
  cardio::dispatcher_host d;

  // 非同期ファイルコピーを開始
  auto p = main_async();
  (void)p;

  // promiseの継続に加えて、io_uringの完了イベントも待機する
  d.park();

  return 0;
}
```

- `cardio::io_uring` はキューを所有し、dispatcherとの連携を行います。
- `io.submit<T>()` と `cardio::io_urings::submit()` を使用すると、liburingのSQE準備関数を直接使って単発のoperationを投入できます。
- `cardio::io_urings::read()` と `cardio::io_urings::write()` は、io_uringへI/O要求を投入し、完了時に実際の読み書きサイズを返す `promise<std::size_t>` を返します。
  その他に、fsync、open/close/statx、パス変更operationを扱う関数が定義されています。補助関数の章を参照して下さい。
- パスを受け取る補助関数は、path文字列をコピーして完了まで保持します。
- 例外有効ビルドでは、すべてのio_uring補助関数に `cancellation` を渡せます。
  キャンセル時は返されたpromiseを `canceled_exception` で失敗完了させ、可能であればio_uringへcancel要求を投入します。
  ただし、ネイティブI/Oの副作用がすでに発生している可能性があるため、キャンセルはbest-effortです。
- I/Oエラー発生時は、`promise` から例外が送出されます。
  実行中のカーネルが投入したoperationをサポートせず、io_uringが負のCQE結果を返した場合も同様に扱われます。
  上記のコード例では例外処理が省略されていることに注意してください。

## 追加のio_uring operationを自分で実装する

使用したいio_uring operationに対する専用ヘルパーがcardioに存在しない場合でも、公開APIである
`cardio::io_uring::submit<T>()` を使って、小さなヘルパーを利用側で定義できます。
例えば、以下は `IORING_OP_MADVISE` を投入するユーザー定義ヘルパーです:

```cpp
#include <cstddef>
#include <system_error>
#include <sys/mman.h>

static cardio::promise<void> madvise_async(
    cardio::io_uring& io,
    void* address,
    std::size_t length,
    int advice) {
  return io.submit<void>(
    [address, length, advice](::io_uring_sqe* sqe) {
      io_uring_prep_madvise(
          sqe,
          address,
          static_cast<off_t>(length),
          advice);
    },
    [](cardio::io_uring_completion completion) {
      if (completion.result < 0) {
        throw std::system_error(
            -completion.result,
            std::generic_category(),
            "io_uring madvise failed");
      }
    });
}
```

この方法は、1つのCQEで完了し、追加の `io_uring_register_*()` 設定を必要としない単発operationに適しています。
SQEから参照されるメモリ、path文字列、その他のポインタは、返されたpromiseが完了するまで呼び出し側で有効に保つ必要があります。

注意: 複数CQE、multishot stream、linked SQE、registered buffer/fileを必要とするoperationは、現在の単発promise APIでは自然には表現できません。

---

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/linux-io-uring.md)
