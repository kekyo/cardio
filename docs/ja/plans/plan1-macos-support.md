# macOS 対応と iOS 向けの検証（plan1）

次のリリースでは macOS のコマンドラインアプリやワーカースレッドで、継続を実行する `dispatcher_host` と、ファイルディスクリプタ（fd）の読み書きが可能になるまで待つ `from_fd()` を使えるようにする。対応状況は experimental とする。
古い Mac で使えるよう、ビルド時に指定する実行先 OS の下限（最低ターゲット）の第一候補を Intel では macOS 10.13、Apple Silicon では macOS 11.0 とする。
日常のビルドには Linux を使い、Apple のコンパイラと OS を使った実行テストには GitHub Actions を使う。
成果物はリポジトリ内で再現できるテストとその実行結果とし、バイナリの配布方法は将来配布が必要になった時点で検討する。

macOS のリリース条件は後述の段階1と2で満たす。iOS 向けのビルド確認は追加作業が小さければ同じリリースに含める。macOS の AppKit と iOS の UIKit が動かす GUI のイベントループとの統合は、その後に扱う。

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
| 既存の `poll()` とパイプ | CLI・サービス・ワーカースレッド向けの初期案。Apple 向けの fd 判定を整えれば、タイマー、キャンセル、複数スレッドからの `park()` を再利用できる。変更が最も小さく、Linux 上で同じ実装をテストできる。[Apple の poll(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/poll.2.html) |
| `kqueue` / `kevent` | 多数の fd を監視するサービス向け。読み書きの監視をカーネルに登録したままにし、`EVFILT_USER` で別スレッドから起床させる。同じ fd を待つ複数の promise に通知を届け、EOF やエラーを既存の `fd_event` に変換する必要がある。fd 数を増やした計測で `poll()` の配列を組み直す費用が問題になった時点で採用を判断する。[Apple の kqueue(2)](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html)・[XNU のイベント定義](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/event.h) |
| Core Foundation の `CFRunLoop` | macOS の AppKit と iOS の UIKit が所有するイベントループに接続する第一候補。Android の自動ホストと同様に、アプリのメインスレッドで `park()` を呼ばずに使える。別スレッドからの `post()` には `CFRunLoopSourceSignal` と `CFRunLoopWakeUp`、期限には `CFRunLoopTimer`、fd には `CFFileDescriptor` を使う。fd の通知は一度限りで、監視解除時に呼び出し元の fd を閉じない設定を選べる。ユーザー入力の追跡中にも配送するなら、ソースとタイマーを共通の実行モード（common modes）に登録する。[Android の自動ホスト](../android-looper.md)・[Apple の Run Loops](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/RunLoopManagement/RunLoopManagement.html)・[CFFileDescriptor](https://developer.apple.com/documentation/corefoundation/cffiledescriptor) |
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

調査時のビルドはリポジトリのルートから次のコマンドで再現できる。`-DCARDIO_HAS_POSIX_FD=1` は当時の既定値を補うための指定で、段階1の完了確認ではこの指定を外す。

```sh
zig c++ -target x86_64-macos.10.13 -std=c++20 -O2 -pthread \
  -DCARDIO_HAS_POSIX_FD=1 -Iinclude tests/cardio_fd_test.cpp -o /tmp/cardio-fd-mac-intel
zig c++ -target aarch64-macos.11.0 -std=c++20 -O2 -pthread \
  -DCARDIO_HAS_POSIX_FD=1 -Iinclude tests/cardio_fd_test.cpp -o /tmp/cardio-fd-mac-arm64
```

Wine に近い選択肢として、Linux 上で macOS の実行ファイルを動かす Darling がある。cardio の Darling 上での動作は未確認のため、採用を検討する場合は Intel 用の CLI テストで fd、タイマー、スレッド間通知を試す。Darling は GUI 対応を experimental としている。[Darling](https://www.darlinghq.org/)

OSXCross も Linux から Mach-O を生成できるが、macOS SDK が必要になる。Apple は SDK のインストール・利用を Apple ブランドの機器に限定しているため、一般の Linux PC で繰り返すビルドには Zig を使う。[OSXCross](https://github.com/tpoechtrager/osxcross)・[Xcode and Apple SDKs Agreement §2.2・§2.7](https://www.apple.com/legal/sla/docs/xcode.pdf)

## 実装の段階と完了条件

### 1. 標準 dispatcher で POSIX fd を待機する

Apple 向けの既定構成で `from_fd()` を使えることを確かめるテストを先に追加する。現行ヘッダーでそのテストが失敗することを確認してから、macOS・iOS で POSIX fd を既定で有効にする。利用者が明示的に `CARDIO_HAS_POSIX_FD=0` を指定する構成もテストする。[Android の既定構成テスト](../../../tests/android/cardio_android_config_test.cpp)・[POSIX fd を無効にする既存テスト](../../../tests/cardio_no_posix_test.cpp)

この段階は、Linux 上で共通テストが通り、追加のマクロ指定なしで既存の fd テスト本体を Intel 10.13 と arm64 11.0 の両方にクロスリンクできれば完了とする。

### 2. macOS で共通テストを実行する

macOS 用の Make ターゲットを設ける。現行の `build.sh` は Win32 テストも起動し、`make test` は Android の診断テストにも依存する。共通の C++ テストと各構成のテストを新しいターゲットから実行できるようにする。既存の Makefile を共用する場合は、複数の生成物をまとめて扱う `&:` 規則に対応した GNU Make 4.3 以降を使う。[build.sh](../../../build.sh)・[Makefile](../../../Makefile)・[GNU Make 4.3](https://lists.gnu.org/archive/html/info-gnu/2020-01/msg00004.html)

GitHub Actions では Intel 用の `macos-15-intel` と arm64 用の `macos-15` で Xcode 16.4 を選ぶ。それぞれの最低 OS を指定して Apple Clang でビルドし、同じランナーで実行する。[GitHub のランナー一覧](https://github.com/actions/runner-images#available-images)・[Intel イメージの Xcode](https://github.com/actions/runner-images/blob/main/images/macos/macos-15-Readme.md)・[arm64 イメージの Xcode](https://github.com/actions/runner-images/blob/main/images/macos/macos-15-arm64-Readme.md)

両ランナーで対象テスト一式が通り、既定構成で `cardio_fd_test: PASS` が出ることを experimental リリースの完了条件とする。パイプの切断、閉じた fd、読み書きの同時通知、別スレッドからの起床、キャンセル、shutdown の挙動を含める。結果にはアーキテクチャ、最低ターゲット、実行した macOS のバージョンを残す。[fd テストの検証項目](../../../tests/cardio_fd_test.cpp#L546)

任意機能の共有ランタイムまで対象に含める場合は、Linux 用の `.so` と `$ORIGIN` を使っているビルド設定を見直し、検証用の `.dylib` と `@loader_path` で既存のプラグインテストを動かす。現行の `test-shared` は Darwin では空で、共有ライブラリのサンプルは Linux の io_uring を既定で有効にしている。[共有ランタイムの Makefile](../../../samples/libcardio/Makefile.posix)・[プラグインをロードするテスト](../../../tests/cardio_shared_host_test.cpp)

### 3. iOS 向けに標準 dispatcher をビルドする（同時対応の候補）

iOS 15 を最低ターゲットの候補とする。Xcode の実機用 SDK（`iphoneos`）と Simulator 用 SDK（`iphonesimulator`）で、`dispatcher_host` と `from_fd()` を使う小さな C++ プログラムをビルドする。両方でコンパイル・リンクできればこの段階は完了とし、結果には使用した SDK と「ビルド確認」を記録する。[Xcode と iOS の対応表](https://developer.apple.com/xcode/system-requirements)

### 4. GUI のメインスレッドで継続を再開する（将来）

AppKit・UIKit 用に、前述の `CFRunLoop` に接続する自動ホストを追加する。macOS のアプリと iOS Simulator 上の UIKit アプリで、イベントループを動かしたまま別スレッドから `post()` する。投稿したコールバック、タイマーの継続、fd の継続がメインスレッドで実行され、監視を解除しても呼び出し元の fd が使えることを完了条件とする。[Apple の Run Loops](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/RunLoopManagement/RunLoopManagement.html)・[Simulator でのテスト](https://developer.apple.com/library/archive/technotes/tn2339/_index.html)
