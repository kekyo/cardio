# macOS・iOS の dispatcher とイベントループ

macOS・iOS ではアプリがイベントループを動かすか、AppKit や UIKit に任せるかでホストを選びます。いずれのホストでも `from_fd()` でファイルディスクリプタ（fd）の読み書きを待てます。待機中は fd を開いたままにし、使い終わったらアプリで閉じてください。

| 用途 | ホスト | 待機と実行 |
| --- | --- | --- |
| CLI やワーカースレッド | `dispatcher_host` | `park()` を呼びます。標準の POSIX 実装は `poll()` で待機します。 |
| 複数の fd をカーネルに登録して待つ | `dispatcher_host_kqueue` | `park()` を呼びます。タイマーとスレッド間の通知も同じ待機で処理します。 |
| アプリが Core Foundation のイベントループを動かす | `dispatcher_host_cfrunloop` | ループの所有スレッドで `park()` を呼ぶと、アプリのイベントと cardio の継続が実行されます。 |
| AppKit や UIKit が既にイベントループを動かしている | `dispatcher_host_cfrunloop_auto` | ループの所有スレッドでホストを構築すると、継続がそのスレッドで自動的に再開します。 |

Core Foundation の両ホストは、構築・破棄ともにループの所有スレッドで行います。自動ホストは継続の実行が終わるまで保持してください。Core Foundation を使う場合は、アプリの各翻訳単位で `CARDIO_WITH_CFRUNLOOP=1` を指定し、フレームワークをリンクします。登録したソースとタイマーはループの共通モードで動作します。[Apple の Run Loops](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/RunLoopManagement/RunLoopManagement.html)

```sh
xcrun --sdk macosx clang++ -std=c++20 -Iinclude -DCARDIO_WITH_CFRUNLOOP=1 \
  app.cpp -framework CoreFoundation -o app
```

`cardio.h` は使えるホストに対応するマクロを定義します。同じソースを複数の OS でビルドするときは次のように選べます。

```cpp
#include "cardio.h"

#ifdef CARDIO_DISPATCHER_HOST_KQUEUE
using worker_host = cardio::dispatcher_host_kqueue;
#else
using worker_host = cardio::dispatcher_host;
#endif

#ifdef CARDIO_DISPATCHER_HOST_CFRUNLOOP
using native_loop_host = cardio::dispatcher_host_cfrunloop;
using ui_host = cardio::dispatcher_host_cfrunloop_auto;
#elif defined(__ANDROID__)
using native_loop_host = cardio::dispatcher_host_android;
using ui_host = cardio::dispatcher_host_android_auto;
#elif CARDIO_HAS_WIN32_HANDLE
using native_loop_host = cardio::dispatcher_host_win32;
using ui_host = cardio::dispatcher_host_win32_auto;
#endif
```

## 検証環境

macOS 対応は experimental です。表の最低ターゲットはビルド時に指定した OS バージョンです。実行確認には GitHub Actions の macOS ランナーと Xcode 16.4 を使っています。[CI の検証内容](../../.github/workflows/ci.yml)

| CPU | 最低ターゲット | 実行を確認した macOS |
| --- | --- | --- |
| Intel x86_64 | 10.13 | 15.7.9 |
| Apple Silicon arm64 | 11.0 | 15.8.2 |

iOS も experimental です。最低ターゲットを 15.0 に設定し、iOS 18.5 の実機用 SDK と Simulator 用 SDK の両方で各ホストをコンパイル・リンクしました。実行確認には iOS 18.5 の iPhone 16 Simulator を使い、別スレッドからの通知、タイマー、fd の継続が UIKit のメインスレッドで再開することを確認しました。[iOS の検証結果](https://github.com/kekyo/cardio/actions/runs/45802233648/job/139848387233)

---

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/apple-dispatchers.md)
