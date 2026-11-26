# macOS 対応と iOS 向けの検証（plan1）

次のリリースでは macOS のコマンドラインアプリやワーカースレッドで、継続を実行する `dispatcher_host` と、ファイルディスクリプタ（fd）の読み書きが可能になるまで待つ `from_fd()` を使えるようにする。対応状況は experimental とする。
古い Mac で使えるよう、ビルド時に指定する実行先 OS の下限（最低ターゲット）の第一候補を Intel では macOS 10.13、Apple Silicon では macOS 11.0 とする。
日常のビルドには Linux を使い、Apple のコンパイラと OS を使った実行テストには GitHub Actions を使う。
成果物はリポジトリ内で再現できるテストとその実行結果とし、バイナリの配布方法は将来配布が必要になった時点で検討する。

最初に `poll()` 版を Linux でクロスビルドし、作業ブランチを push して GitHub Actions の Intel と Apple Silicon の両方で動作を確認する。両方のテストが通ったら、`kqueue` を使う手動ホストと、Core Foundation のイベントループ（`CFRunLoop`）に接続するホストを順に実装する。アプリが待機ループを所有するときは `park()` を呼び、AppKit などが既にループを動かしているときは、そのループで継続を自動実行できるようにする。

macOS の experimental 対応は段階1〜5で完了する。iOS 向けのビルド確認は追加作業が小さければ同じリリースに含める。

## 現行実装と調査結果

調査時点は 2026年11月26日、対象コミットは `a132482`。
標準の `dispatcher_host` を使うアプリは自身のスレッドから `park()` を呼んで継続やコールバックを実行する。
POSIX 版は fd の読み書きを `poll()` で待ち、別スレッドからの通知には非ブロッキングのパイプを使う。タイマーとキャンセルにも既存の実装を使える。[dispatcher の使い方](../dispatchers.md)・[POSIX fd の仕様](../posix-fd.md)・[待機の実装](../../../include/cardio.h)

| 確認した環境 | 結果 |
| --- | --- |
| Linux x86_64、Clang 21、libc++ 18 | dispatcher、fd、キャンセル、例外無効の4つの既存テストをコンパイルして実行し、いずれも `PASS`。 |
| Linux から Zig 0.15.2 で macOS 向けにコンパイル | 既定では `CARDIO_HAS_POSIX_FD=0`。ヘッダーの判定は、ESP-IDF の分岐以外では `_POSIX_C_SOURCE` または `__ANDROID__` を見ている。この構成の fd テストは `SKIP` と表示して正常終了する。[判定箇所](../../../include/cardio.h#L94)・[fd テスト](../../../tests/cardio_fd_test.cpp#L546) |
| 同じ環境で `CARDIO_HAS_POSIX_FD=1` を指定 | 既存の fd テストを Intel と arm64 の両方でリンクできた。macOS の実行形式である Mach-O に記録された最低 OS は、それぞれ 10.13 と 11.0。 |

macOS 10.13・11.0 上での実行、Apple 付属の libc++ との組み合わせ、iOS の動作は未検証である。

## dispatcher の選択肢

| 方式 | 適する用途と実装上の判断 |
| --- | --- |
| 既存の `poll()` とパイプ | 最初に対応する方式。標準の `dispatcher_host` で CLI・サービス・ワーカースレッドから使う。Apple 向けの fd 判定を整えれば、タイマー、キャンセル、複数スレッドからの `park()` を再利用できる。Linux 上でも同じ実装をテストできる。[Apple の poll(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/poll.2.html) |
| `kqueue` / `kevent` | `poll()` 版を macOS で実行確認した後に追加する。CLI やワーカースレッドが `park()` を呼ぶと、カーネルに登録した fd の読み書きと次のタイマー期限を待つ。別スレッドからは `EVFILT_USER` で起床させる。同じ fd を待つ複数の promise に通知を届け、EOF やエラーを既存の `fd_event` に変換する。[Apple の kqueue(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html)・[XNU のイベント定義](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/event.h) |
| Core Foundation の `CFRunLoop` | `kqueue` 版に続いて実装する。アプリがループを所有する場合は手動で、AppKit や UIKit が所有する場合は自動で、cardio の継続とアプリのイベントを同じスレッドで処理する。別スレッドからの `post()` には `CFRunLoopSourceSignal` と `CFRunLoopWakeUp`、期限には `CFRunLoopTimer` を使う。`CFFileDescriptor` で `kqueue` 自体の読み出し通知を受けると、個々の fd の監視を手動版と共用できる。ソースとタイマーは共通の実行モード（common modes）に登録する。[Apple の Run Loops](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/RunLoopManagement/RunLoopManagement.html)・[CFFileDescriptor と kqueue](https://developer.apple.com/documentation/corefoundation/cffiledescriptor) |
| Grand Central Dispatch（GCD） | メインキューや既存の dispatch queue に継続を返したいアプリ向け。dispatch source で fd とタイマーを監視できる。通常の直列キューは処理ごとに実行スレッドが変わり得るので、コールバックの間だけ cardio のスレッドローカルな dispatcher を設定し、終了時に復元する。source のキャンセルは非同期のため、解除が完了するまで fd が再利用されないようにする設計も必要になる。[Apple の Dispatch Queues](https://developer.apple.com/documentation/dispatch/dispatchqueue)・[Dispatch Sources](https://developer.apple.com/library/archive/documentation/General/Conceptual/ConcurrencyProgrammingGuide/GCDWorkQueues/GCDWorkQueues.html) |

## macOS の最低ターゲット

どの候補でも、ヘッダーを組み込む側には C++20 の `<coroutine>` に対応したコンパイラと標準ライブラリが必要になる。

| Intel（x86_64） | Apple Silicon（arm64） | 選択の理由 |
| --- | --- | --- |
| 10.13 | 11.0 | 第一候補。Xcode 16.4 は Intel 向けに 10.13 を指定でき、11.0 は Apple Silicon で動作する最初の macOS。CI でアーキテクチャごとに最低版を設定する。 |
| 11.0 | 11.0 | 2つのアーキテクチャで同じ設定を使える。Xcode 26 系でもこの下限を指定できる。Intel の下限は 10.13 から 11.0 に上がる。 |
| 12.0 | 12.0 | Xcode 27 系で最低版のビルドも続ける場合の候補。macOS 27 SDK を使って Intel・Apple Silicon の両方に対応できるデプロイ先の下限は 12。 |

利用者向けの対応表にはビルド時に指定した最低 OS とテストを実行した OS をそれぞれ記録する。例えば、10.13 を指定して macOS 15 でテストした場合は「最低ターゲット 10.13、実行確認 macOS 15」と書く。[Xcode とデプロイ先の対応表](https://developer.apple.com/xcode/system-requirements)・[Apple Silicon の最低版](https://developer.apple.com/documentation/xcode/embedding-a-helper-tool-in-a-sandboxed-app)・[Apple の C++ 対応表](https://developer.apple.com/xcode/cpp/)

## Linux での開発と検証

日常の開発では Linux 上の Clang と libc++ で共通テストを実行し、`zig c++` で macOS 向けのコンパイルとリンクを確認する。
`zig c++` は Zig に同梱された Clang で既存の `.cpp` と `cardio.h` を C++20 としてコンパイルするコマンドである。
Linux 上の MinGW が Windows 用の実行ファイルを作るのと同じように、`-target` で生成先の CPU と OS を指定できる。
`poll()` の宣言と、macOS のシステムライブラリ `libSystem` がその関数を提供することを示すリンク用定義が同梱されており、今回のテストは Apple SDK を導入せずにリンクできた。
このクロスビルドでは Zig 側の libc++ を使う。OS 付属の libc++ との互換性や実行時の fd 通知は、Apple Clang でビルドしたテストを macOS 上で実行して確認する。[Zig の C++ コンパイラ](https://ziglang.org/download/0.11.0/release-notes.html#zig-c)・[同梱の poll.h](https://github.com/ziglang/zig/blob/0.15.2/lib/libc/include/any-macos-any/sys/poll.h)・[libSystem のリンク用定義](https://github.com/ziglang/zig/blob/0.15.2/lib/libc/darwin/libSystem.tbd)

日常のビルドでは `build.sh` から macOS 向けの Make ターゲット（案は `make macos-cross`）を呼ぶ。このターゲットが Zig を取得・配置するので、開発者は Zig を事前にインストールしたり `PATH` を設定したりせずに済む。macOS 向けだけを確認するときはこのターゲットを直接使う。

次のコマンドは調査時に実行したコンパイルとリンクを示す。`-DCARDIO_HAS_POSIX_FD=1` は当時の既定値を補うための指定であり、段階2の完了確認では外す。

```sh
zig c++ -target x86_64-macos.10.13 -std=c++20 -O2 -pthread \
  -DCARDIO_HAS_POSIX_FD=1 -Iinclude tests/cardio_fd_test.cpp -o /tmp/cardio-fd-mac-intel
zig c++ -target aarch64-macos.11.0 -std=c++20 -O2 -pthread \
  -DCARDIO_HAS_POSIX_FD=1 -Iinclude tests/cardio_fd_test.cpp -o /tmp/cardio-fd-mac-arm64
```

Wine に近い選択肢として、Linux 上で macOS の実行ファイルを動かす Darling がある。cardio の Darling 上での動作は未確認のため、採用を検討する場合は Intel 用の CLI テストで fd、タイマー、スレッド間通知を試す。Darling は GUI 対応を experimental としている。[Darling](https://www.darlinghq.org/)

OSXCross も Linux から Mach-O を生成できるが、macOS SDK が必要になる。Apple は SDK のインストール・利用を Apple ブランドの機器に限定しているため、一般の Linux PC で繰り返すビルドには Zig を使う。[OSXCross](https://github.com/tpoechtrager/osxcross)・[Xcode and Apple SDKs Agreement §2.2・§2.7](https://www.apple.com/legal/sla/docs/xcode.pdf)

## 実装の段階と完了条件

### 1. Linux のビルドで Zig を自動取得し、macOS 用の実行ファイルを作る

調査で使った Zig 0.15.2 を固定する。Linux x86_64 用の公式アーカイブの URL と SHA-256 をビルド設定に記録し、初回のビルドでダウンロードして照合する。Ubuntu では取得に `curl` と `ca-certificates`、照合に `sha256sum`（`coreutils`）、展開に `tar` と `xz-utils` を使う。一時ディレクトリで検証と展開を終えてから `build/tools/zig/` のバージョン・ホスト別ディレクトリに配置する。2つの CPU 向けターゲットから同じ Zig を Make の依存先として参照する。[Zig の公式配布一覧と SHA-256](https://ziglang.org/download/index.json)・[build.sh](../../../build.sh)・[Makefile](../../../Makefile)

完了条件は、Zig が `PATH` になく配置先も空の Linux 環境で通常のビルドを起動すると、既存の fd テストを Intel・arm64 両方の Mach-O にリンクできることとする。この段階では調査時の fd 有効化指定を使える。並列ビルドで取得は1回に収まり、2回目はネットワークを切っても配置済みの Zig で両方を再ビルドできることを確かめる。ダウンロードの中断やハッシュ不一致の後は、次のビルドで取得し直せることも確認する。

### 2. 標準 dispatcher の `poll()` 版を実装し、ローカルでビルドを確認する

Apple 向けの既定構成で `from_fd()` を使えることを確かめるテストを先に追加する。現行ヘッダーでそのテストが失敗することを確認してから、macOS・iOS で POSIX fd を既定で有効にする。利用者が明示的に `CARDIO_HAS_POSIX_FD=0` を指定する構成もテストする。[Android の既定構成テスト](../../../tests/android/cardio_android_config_test.cpp)・[POSIX fd を無効にする既存テスト](../../../tests/cardio_no_posix_test.cpp)

完了条件は、Linux の既存テストが通り、Zig のターゲットから fd の強制指定を外しても、Apple の既定構成テストと既存の fd テスト本体を両方の CPU 向けにクロスリンクできることとする。Mach-O に記録された最低 OS が Intel では 10.13、arm64 では 11.0 であることも確かめる。

### 3. `poll()` 版を push し、GitHub Actions の macOS で動作を確認する

macOS 上でビルドして実行する Make ターゲット（案は `make test-macos`）を設ける。現行の `build.sh` は Win32 テストも起動し、`make test` は Android の診断テストにも依存する。共通の C++ テストと macOS に適用できる各構成のテストを新しいターゲットから実行する。既存の Makefile を共用する場合は、複数の生成物をまとめて扱う `&:` 規則に対応した GNU Make 4.3 以降を使う。[build.sh](../../../build.sh)・[Makefile](../../../Makefile)・[GNU Make 4.3](https://lists.gnu.org/archive/html/info-gnu/2020-01/msg00004.html)

GitHub Actions では Intel 用の `macos-15-intel` と arm64 用の `macos-15` で Xcode 16.4 を選ぶ。それぞれの最低 OS を指定して Apple Clang でビルドし、同じランナーで実行する。[GitHub のランナー一覧](https://github.com/actions/runner-images#available-images)・[Intel イメージの Xcode](https://github.com/actions/runner-images/blob/main/images/macos/macos-15-Readme.md)・[arm64 イメージの Xcode](https://github.com/actions/runner-images/blob/main/images/macos/macos-15-arm64-Readme.md)

段階2のローカル確認後に、`poll()` 版と macOS 用 CI を含む作業ブランチ `feature/macos-support` を `origin` に push する。対象コミットの Actions が完了するまで確認する。両ランナーで対象テスト一式が通り、既定構成で `cardio_fd_test: PASS` が出ることが段階4の着手条件となる。fd テストが `SKIP` で正常終了しても CI は失敗とする。失敗時は修正を push し、新しいコミットで両方の結果を確認する。[現在の push ワークフロー](../../../.github/workflows/ci.yml)・[fd テストの結果表示](../../../tests/cardio_fd_test.cpp#L546)

実行テストにはパイプの切断、閉じた fd、読み書きの同時通知、別スレッドからの起床、キャンセル、shutdown を含める。結果にはコミット、Actions の実行 URL、アーキテクチャ、最低ターゲット、実行した macOS のバージョンを残す。

任意機能の共有ランタイムまで対象に含める場合は、Linux 用の `.so` と `$ORIGIN` を使っているビルド設定を見直し、検証用の `.dylib` と `@loader_path` で既存のプラグインテストを動かす。現行の `test-shared` は Darwin では空で、共有ライブラリのサンプルは Linux の io_uring を既定で有効にしている。[共有ランタイムの Makefile](../../../samples/libcardio/Makefile.posix)・[プラグインをロードするテスト](../../../tests/cardio_shared_host_test.cpp)

### 4. `kqueue` / `kevent` で手動の `park()` を実装する

標準の `poll()` 版に加えて、`dispatcher_host_kqueue`（仮称）を公開する。読み書きは `EVFILT_READ` と `EVFILT_WRITE` に登録し、`park()` は次のタイマー期限まで `kevent()` で待機する。別スレッドからの投稿や待機の取り消しで起床できるようにする。標準版と同じ fd・タイマー・キャンセルの振る舞いをテストで先に表し、同じ fd を待つ複数の処理への通知と、解除後も呼び出し元の fd が使えることを確認する。[POSIX fd の仕様](../posix-fd.md)・[Apple の kqueue(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html)

完了条件は、CLI テストがこのホストを選んで `park()` を呼び、fd の読み書き、タイマー、別スレッドからの投稿を処理し、shutdown で待機から戻ることとする。Linux から両 CPU 向けにクロスビルドした後に push し、macOS の両ランナーで標準版と `kqueue` 版のテストが通ることを確認する。[Zig に同梱される sys/event.h](https://github.com/ziglang/zig/blob/0.15.2/lib/libc/include/any-macos-any/sys/event.h)

### 5. `CFRunLoop` で手動と自動の配送を選べるようにする

アプリがループを所有する場合は `dispatcher_host_cfrunloop` の `park()` で cardio の継続とアプリのイベントを処理する。AppKit などが既にループを動かしている場合は、所有スレッドで `dispatcher_host_cfrunloop_auto` を構築し、アプリから `park()` を呼ばずに継続を実行する。いずれも仮称で、同じ fd 監視、タイマー、起床用ソースを使う。この使い分けは Win32 と Android の手動ホストと自動ホストに合わせる。[Win32 のメッセージ配送](../win32.md)・[Android の Looper](../android-looper.md)

fd 通知のコールバックでは `kevent()` の待ち時間をゼロにして到着済みのイベントを取り出し、次の通知を受けるために `CFFileDescriptor` を再度有効にする。ループの所有スレッドでホストを破棄するときはソースとタイマーを解除し、呼び出し元が所有する fd を引き続き使えるようにする。Core Foundation を使うアプリはビルド時に有効化し（案は `CARDIO_WITH_CFRUNLOOP=1`）、Apple SDK のヘッダーと `-framework CoreFoundation` を使ってビルドする。[CFFileDescriptor のコールバック](https://developer.apple.com/documentation/corefoundation/cffiledescriptor)・[Apple の Run Loops](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/RunLoopManagement/RunLoopManagement.html)

公開ヘッダーは、各ホストが使える構成でだけ次のマクロを定義する。開発者は `#include "cardio.h"` の後で `#ifdef` を使ってホストを選べる。マクロ名の案は `kqueue` 手動版が `CARDIO_DISPATCHER_HOST_KQUEUE`、Core Foundation の手動・自動の組が `CARDIO_DISPATCHER_HOST_CFRUNLOOP`。使えない構成ではそれぞれ未定義にする。既存の `CARDIO_HAS_POSIX_FD` は 0 または 1 で fd 待機の有無を表すため、引き続き `#if` で判定する。

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

macOS の両ランナーで手動・自動のテストを実行する。別スレッドからの投稿、タイマー、fd の継続がループの所有スレッドで実行されることを確かめる。手動版では `park()` 中にアプリ独自のイベントも処理し、自動版では既存のループを動かすだけで両方を処理できることを確認する。待機を取り消した後の二重通知や、ホストを破棄した後のコールバックも検出する。

公開例は Core Foundation 有効・無効の macOS、Linux、Win32、Android 向けにビルドする。Linux の既存テスト一式が通ったら作業ブランチを push し、そのコミットに対する Actions の対象テスト一式が通れば macOS 対応を完了とする。

### 6. iOS 向けにビルドする（同時対応の候補）

iOS 15 を最低ターゲットの候補とする。Xcode の実機用 SDK（`iphoneos`）と Simulator 用 SDK（`iphonesimulator`）で、標準ホストと `from_fd()` を使う小さな C++ プログラムをビルドする。両 SDK でコンパイル・リンクできれば iOS のビルド確認は完了とし、結果には使用した SDK を記録する。[Xcode と iOS の対応表](https://developer.apple.com/xcode/system-requirements)

iOS でも追加ホストを有効にする場合は、同じ公開マクロで選んだホストを両 SDK でリンクする。UIKit との動作確認は Simulator 上で行う。メインループを動かしたまま別スレッドから投稿し、投稿したコールバック、タイマー、fd の継続がメインスレッドで実行され、監視解除後も fd が使えることを確かめる。ビルドのみ確認した構成と Simulator で実行した構成は、対応表にそれぞれ記録する。[Simulator でのテスト](https://developer.apple.com/library/archive/technotes/tn2339/_index.html)

## 実施結果

| 段階 | 確認した内容 | 記録 |
| --- | --- | --- |
| 1. Zig の取得 | Zig をインストールしていない Linux x86_64 で通常のビルドを起動し、1回の取得で両 CPU の実行ファイルを生成した。配置後のオフライン再ビルド、取得中断とハッシュ不一致の後の再取得も確認した。 | [自動取得の実装 afc848b](https://github.com/kekyo/cardio/commit/afc848b) |
| 2. 標準ホスト | Apple の既定値を確認するテストは変更前に両 CPU で失敗し、変更後は fd の強制指定を外しても両方でリンクできた。Linux の標準テスト一式と、Clang・libc++ での共通テストも通過した。 | [既定値とテスト 81445aa](https://github.com/kekyo/cardio/commit/81445aa) |
| 3. `poll()` | Intel は最低ターゲット 10.13、実行 OS 15.7.9。arm64 は最低ターゲット 11.0、実行 OS 15.8.2。両方で `cardio_fd_test: PASS` と Mach-O の最低 OS を確認してから段階4に進んだ。 | [2bf18d8 の macOS テスト](https://github.com/kekyo/cardio/actions/runs/45799219751) |
| 4. `kqueue` | 両 CPU で標準版と同じ fd テストを実行した。重複した待機のキャンセル、40組のソケットの同時通知、タイマー、他スレッドからの起床、shutdown が通過した。 | [01f17aa の macOS テスト](https://github.com/kekyo/cardio/actions/runs/45800337931) |
| 5. `CFRunLoop` | 両 CPU で手動・自動ホストを検証した。アプリ独自のイベント、別スレッドからの通知、タイマー、fd の継続が所有スレッドで動作した。自動ホストは既定以外の共通モードでも動作し、待機を取り消してホストを破棄した後も元の fd は使えた。 | [31c71d0 の Intel テスト](https://github.com/kekyo/cardio/actions/runs/45802233648/job/139848387320)・[arm64 テスト](https://github.com/kekyo/cardio/actions/runs/45802233648/job/139848387340) |
| 6. iOS | 最低ターゲットを 15.0 とし、実機用・Simulator 用の両 SDK 18.5 で標準版、`kqueue` 版、Core Foundation 版とホスト選択例をリンクした。iPhone 16 Simulator（iOS 18.5）で UIKit のイベントループを動かし、`cardio_ios_cfrunloop_test: PASS` を確認した。 | [31c71d0 の iOS テスト](https://github.com/kekyo/cardio/actions/runs/45802233648/job/139848387233) |

段階5では公開 API の `promise_source::resolve()` を別スレッドで呼び、待機中のコルーチンがループの所有スレッドで再開することを確認した。手動・自動のホストを選ぶ公開例は Core Foundation を有効・無効にした macOS、Linux、Win32、Android 向けにビルドした。[ホスト選択のテスト](../../../tests/cardio_host_selection_test.cpp)
