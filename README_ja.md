# cardio

C++20非同期処理のための簡潔な補助を実現する、ヘッダーオンリーライブラリ。

![cardio](./images/cardio-120.png)

[![Project Status: Active – The project has reached a stable, usable state and is being actively developed.](https://www.repostatus.org/badges/latest/active.svg)](https://www.repostatus.org/#active)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

---

[(English language is here)](./README.md)

## これは何?

C++20で、新たに追加された `co_await` などの非同期処理を実現する拡張構文を、気楽に使用したいと考えたことはありますか？
コンパイラはこれらの構文をサポートしていますが、現在のところ、標準ライブラリとしてはほとんど提供されていないため、
低レベル実装から用意する必要があります。

cardioはこれを補助して、汎用の非同期処理を容易に記述可能にするライブラリです。
ヘッダーオンリーライブラリなので、あなたの環境に容易に取り込むことができます。

主に、非同期API設計者と、それを使用する開発者向けに設計されています。

C++20の非同期処理は、TypeScript/JavaScriptのように、簡単に記述できる構文をサポートしています。
このライブラリを使用すれば、以下のように簡単に非同期処理を記述できるようになります:

```cpp
#include <stdio.h>
#include "cardio.h"

// 非同期関数(promiseを返す)
static cardio::promise<int> add_async(int a, int b) {
  co_return a + b;  // co_return出来る
}

// 非同期関数(promiseを返す)
static cardio::promise<void> main_async() {
  auto r = co_await add_async(1, 2);  // co_await出来る
  printf("1 + 2 = %d\n", r);
}

int main() {
  // cardio dispatcherを初期化
  // dispatcherはこのスレッド（メインスレッド）を使用して、promiseの継続を代理実行する
  cardio::dispatcher_host d;

  // park()が終了するまで、基底promiseを生存させる
  auto p = main_async();
  (void)p;

  // このスレッドを待機させ、非同期継続処理を実行する
  d.park();

  return 0;
}
```

### 特徴

- ヘッダーオンリーライブラリです。容易に組み込むことができます。
- 基本的な挙動は、TypeScript/JavaScriptにおける非同期処理を踏襲しています。
  従って、高度な非同期処理を、TypeScriptと同じように簡潔に記述することができます。
- キャンセル通知をサポートしています。
- POSIXファイルディスクリプタを待機して、ready状態の非同期処理を実現できます。
- Win32 HANDLEと、外部で投入したOVERLAPPED I/Oの完了を待機できます。
- Linux io_uringを使用して、真の非同期I/Oを実現できます。
- Androidのnative LooperとJava UI Looperとの統合をサポートしています。
- GLib 2 (GTK) のスケジューラーとの統合をサポートしています。
- マルチスレッドによる継続実行をサポートしています。
- 同期プリミティブに対応する非同期プリミティブ (mutex, semaphore, conditional variables, reader-writer-lock) をサポートしています。

### 環境

- C++20 コンパイラ（GNU g++、Clang、Apple Clang）
- POSIX環境 (非POSIX環境でもC++20の主要なライブラリがあれば、コア機能は動作します)
- Linux (optional, io_uring support)
- GLib 2/GTK (optional)
- Android API 24以降 (optional, Android Looper integration)
- ESP-IDF 6.0（optional, 主対象はESP32-S3）
- Windows (optional, Win32 HANDLE and OVERLAPPED wait support)
- macOS（experimental。最低ターゲットは Intel 10.13、Apple Silicon 11.0）
- iOS（experimental。最低ターゲット 15.0、UIKit との統合は Simulator で実行確認）

---

## インストール

あなたのプロジェクトに [`cardio.h`](./include/cardio.h) をコピーしてインクルードします。以上です。

---

## ドキュメント

### 共通の使い方

- [基本的な使用方法](./docs/ja/getting-started.md)
- [promiseの操作と寿命](./docs/ja/promises.md)
- [例外](./docs/ja/exceptions.md)
- [キャンセル処理](./docs/ja/cancellation.md)
- [補助関数と非同期プリミティブ](./docs/ja/helpers-and-primitives.md)

### 環境別ガイド

- [POSIXファイルディスクリプタの待機](./docs/ja/posix-fd.md)
- [Linux io_uringで非同期I/Oを実装](./docs/ja/linux-io-uring.md)
- [GLib・GTKとの統合とGIOファイルI/O](./docs/ja/glib-gio.md)
- [Android Looperとの統合](./docs/ja/android-looper.md)
- [ESP-IDFでFreeRTOSタスクを使う](./docs/ja/esp-idf.md)
- [macOS・iOS の dispatcher とイベントループ](./docs/ja/apple-dispatchers.md)
- [Win32 HANDLEの待機とI/O完了ポート](./docs/ja/win32.md)

### 応用

- [複数のスレッドでdispatcherを使う](./docs/ja/dispatchers.md)
- [co_awaitを使わずにpromiseの結果を取得する](./docs/ja/promise-results.md)
- [共有ランタイムと例外無効ビルド](./docs/ja/build-options.md)

---

## 備考

これは [libbounce](https://github.com/kekyo/libbounce/) の後継プロジェクトです。
(更に遡れば、 [future-promise](https://github.com/kekyo/future-promise/) もあります)

libbounceが複雑化したため、CとC++の役割を逆転させ、C++基準で再構築しました。

C++では、`std::function` を含むインライン展開が行われるので、libbounceで問題だった関数ポインタの最適化が抑制される問題を回避できます。

## License

Under MIT.
