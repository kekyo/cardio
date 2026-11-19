# Android Looperとの統合

Android NDKが `__ANDROID__` を定義すると、cardioはPOSIXファイルディスクリプタ待機を自動的に有効化し、2種類のAndroid dispatcher hostを公開します。最終的なnativeバイナリは `libandroid` とリンクしてください。サポートする最低バージョンはAndroid API 24です。Androidでは `CARDIO_HAS_POSIX_FD=0` の指定と、`CARDIO_WITH_LINUX_IO_URING` の有効化はサポートされません。

nativeアプリケーションがループを所有するnativeスレッドでは、`dispatcher_host_android` を使用します:

```cpp
static cardio::promise<void> main_async();

static void run_native_loop() {
  cardio::dispatcher_host_android dispatcher;
  auto root = main_async();
  (void)root;
  dispatcher.park();
}
```

構築、`park()`、破棄は同じnativeスレッドで行う必要があります。このhostはスレッドに既存の `ALooper` があれば接続し、存在しなければ新たに準備します。cardioの継続、timer、POSIX fd readiness、既存のcallback形式のLooper登録を同じループで配送します。Java UIスレッドでは `park()` を呼び出さないでください。

ActivityのUIスレッドのように、Javaがmessage loopを所有している場合は `dispatcher_host_android_auto` を使用します。そのスレッドから呼び出されるJNI関数で構築・破棄します。このhostは意図的に `park()` を公開しません:

```cpp
#include <jni.h>
#include <memory>
#include <optional>

static std::unique_ptr<cardio::dispatcher_host_android_auto> ui_dispatcher;
static std::optional<cardio::promise<void>> ui_root;

extern "C" JNIEXPORT void JNICALL
Java_com_example_App_nativeStart(JNIEnv*, jobject) {
  ui_dispatcher =
      std::make_unique<cardio::dispatcher_host_android_auto>();
  ui_root.emplace(main_async());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_App_nativeStop(JNIEnv*, jobject) {
  ui_root.reset();
  ui_dispatcher.reset();
}
```

現在のスレッドには既存の `ALooper` が必要です。AndroidのJava UIスレッドはこの条件を満たします。Java Looperの所有権はアプリケーションに残り、任意のスレッドから投入されたcardio workはLooperを所有するスレッドで再開されます。

cardioはヘッダーオンリーなので、配布するnative成果物にAPIレベルやページサイズの次元を追加しません。通常、アプリケーションは `x86_64` や `arm64-v8a` などのABIごとに、API 24を最低ターゲットとした最終nativeバイナリを1つずつ生成します。同じバイナリを4 KiBと16 KiBの両方のページサイズでロード可能にするには、例えば `-Wl,-z,max-page-size=16384` を指定し、すべてのnative依存関係を含めて16 KiB互換でリンクしてください。非圧縮nativeライブラリを含むパッケージも16 KiB境界にZIP alignmentする必要があります。宣言した `minSdkVersion` に対して、使用するAPIとすべてのnative依存関係が互換であることはアプリケーション側の責任です。

Android用dispatcherの実装は[公開ヘッダー](../../include/cardio.h)にあります。検証に使うAPIレベルとページサイズは[AndroidのCI設定](../../.github/workflows/ci.yml)を参照してください。

---

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/android-looper.md)
