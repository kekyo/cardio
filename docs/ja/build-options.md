# 共有ランタイムと例外無効ビルド

## 共有ランタイムライブラリ

cardioは既定ではヘッダーオンリーです。
アプリケーションがC++プラグインや他の動的モジュールをロードし、それらもcardioを使用する場合、
各モジュールがヘッダー定義のスレッドローカルランタイム状態を個別に持つ可能性があります。
その場合は、オプションの共有ランタイムライブラリをビルドし、
すべてのモジュールを `CARDIO_SHARED_LIB=1` でコンパイルしてください:

```bash
make -C samples/libcardio -f Makefile.posix
g++ -std=c++20 -Iinclude -DCARDIO_SHARED_LIB=1 app.cpp \
  -Lbuild/libcardio -lcardio -Wl,-rpath,'$ORIGIN/build/libcardio'
```

共有ランタイム自身をビルドする翻訳単位では、`CARDIO_SHARED_LIB=1` と `CARDIO_BUILD_SHARED_LIB=1` の両方を定義します。
このリポジトリに含まれている `samples/libcardio/Makefile.posix` は `libcardio.so` 用にこれを行い、
`samples/libcardio/Makefile.win32` ではMinGWのimport library付きで `libcardio.dll` をビルドします。

共有ランタイムを使うすべてのモジュールは、`CARDIO_WITH_GLIB`、`CARDIO_WITH_LINUX_IO_URING`、`CARDIO_HAS_POSIX_FD`、`CARDIO_HAS_EXCEPTIONS` などのcardio設定マクロを同一にしてビルドする必要があります。
また、互換性のあるC++ ABIを使用する必要があります。
このモードが共有するのはcardioのスレッドローカルランタイム状態だけであり、
C++ APIを安定したプラグインABIにするものではありません。

プラグインをアンロードする前に、新規投入を止め、プラグインが所有するoperationをキャンセルまたは完了し、保留中のpromiseと継続をdrainしてください。
アンロード済みモジュール内のコードを指すコルーチンやコールバックは、安全に再開できません。

## 例外無効ビルド

gccの `-fno-exceptions` オプションのように C++ 例外が無効化されている環境では、cardioは例外関連機能をコンパイル対象から除外します。
この場合、`CARDIO_HAS_EXCEPTIONS` は `0` になります:

```bash
g++ -std=c++20 -fno-exceptions -Iinclude app.cpp
```

例外無効ビルドでは、失敗したpromiseを作る機能は使用できません。
具体的には、`rejected()`、`promise_source<T>::reject()`、`promise_source<T>::try_reject()`、`promise_source<T>::cancel()`、`promise_source<T>::try_cancel()`、`cancellation::throw_if_cancellation_requested()`、`dispatcher::unhandled_exception()` は宣言されません。
また、`try_result()` や `unsafe_result()` は失敗例外を再送出しません。
未完了の `promise_source<T>` を破棄した場合も失敗を値として表現できないため、 `std::terminate()` が呼び出されます。

ライブラリ内部の初期化失敗、不正引数、I/O submit失敗など、例外でしか表現できなかった失敗は `std::terminate()` に集約されます。
エラーを値として扱いたい場合は、呼び出し側で `promise<expected-like-type>` のような値型に包んでください。

`cancellation_source` と `cancellation` によるキャンセル通知そのものは、例外無効ビルドでも使用できます。
ただし、キャンセルを `promise<T>` の失敗として表現するAPIは例外有効ビルドでのみ使用できます。

---

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/build-options.md)
