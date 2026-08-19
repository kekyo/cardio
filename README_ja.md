# cardio

C++20非同期処理のための簡潔な補助を実現する、ヘッダーオンリーライブラリ。

![cardio](./images/cardio-120.png)

[![Project Status: WIP – Initial development is in progress, but there has not yet been a stable, usable release suitable for the public.](https://www.repostatus.org/badges/latest/wip.svg)](https://www.repostatus.org/#wip)
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

- C++20コンパイラ (GNU g++, 恐らくclangでも動作可能)
- POSIX環境 (非POSIX環境でもC++20の主要なライブラリがあれば、コア機能は動作します)
- Windows (optional, Win32 HANDLE and OVERLAPPED wait support)
- Linux (optional, io_uring support)
- Android API 24以降 (optional, Android Looper integration)
- GLib 2/GTK (optional)

---

## インストール

あなたのプロジェクトに [`cardio.h`](./include/cardio.h) をコピーしてインクルードします。以上です。

---

## 基本的な使用方法

すべての公開API定義は、`cardio` 名前空間に配置されています。

cardioで特に重要な機能セットは、 `dispatcher` と `promise` です。
このうち、`promise` については、TypeScript/JavaScriptの `Promise` とほぼ同等と考えて構いません。

`dispatcher` は、非同期処理の「継続」を管理する抽象基底クラスで、ランタイムに相当します。
標準的な具象dispatcher hostとして `dispatcher_host` を使用します。
cardioを使う場合は、`dispatcher_host` の初期化を行う必要があります。

まず、 `dispatcher_host` を配置して初期化し、非同期処理の継続を実行できるように待機させます:

```cpp
// ヘッダオンリーライブラリ
#include "cardio.h"

int main() {
  // dispatcherを初期化
  cardio::dispatcher_host d;

  // メインスレッドを待機させ、非同期継続処理を実行する
  d.park();

  return 0;
}
```

- `park()` 関数は、非同期処理の継続が続く限り、その処理を実行し続けます。

上記のコードでは、非同期処理が何も存在しないため、すぐに関数を抜け、プログラムは終了します。
そこで、最初に実行する非同期処理を挿入します:

```cpp
// 非同期関数
static cardio::promise<int> add_async(int a, int b) {
  co_return a + b;  // 非同期関数の結果は `co_return` で返す
}

// 非同期関数
static cardio::promise<void> main_async() {
  auto r = co_await add_async(1, 2);  // co_await出来る
  printf("1 + 2 = %d\n", r);
}

int main() {
  cardio::dispatcher_host d;

  // 非同期関数を呼び出して、非同期処理を開始
  auto p = main_async();
  (void)p;

  // 非同期継続処理が存在しなくなると、park()関数が抜ける
  d.park();

  return 0;
}
```

非同期関数 `add_async()`  は `promise<int>` を返却するように記述します。
上記の例では、単純な加算処理を行っているためこれが非同期である意味はありませんが、`promise<int>` を返すことによって非同期関数であることが明確になります。

そして、 `add_async()` のような非同期関数を呼び出して処理を行う全体的な関数 `main_async()` も、非同期関数として宣言します。
これは結果を返さないので、 `promise<void>` とします。

`park()` の前に非同期関数 `main_async()` を呼び出すことで、非同期処理が開始されます。
その結果を取得しない場合でも、返された基底promiseは `park()` が終了するまで生存させておきます。

たとえ、上記のように `add_async()` が瞬時に結果を返すとしても、呼び出す側はそれを認識することはできません。
言い換えれば、非同期関数がいつ完了するのかはわかりません。
これを抽象化しているのが `promise<T>` で、その結果を得るのが `co_await` です。

そして、 `park()` によって非同期処理が実行され、完了するまで実行し続けます。
すべての非同期処理が完了して、他に待機する処理がなくなると、 `park()` 関数が終了します。
この例では、計算が完了して `printf()` で値を表示するまでが実行されます。

ところで、このような短いコードであれば、 `main_async()` を用意せず、直接 `main()` にコードを書けるのではないか？ と疑問を持ったかもしれません:

```cpp
static cardio::promise<int> add_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  // ここで直接add_async()を呼び出せるのでは?
  auto r = co_await add_async(1, 2);
  printf("1 + 2 = %d\n", r);

  d.park();

  return 0;
}
```

しかし、これはできません。何故なら、 `main()` 関数はpromiseを返しておらず、従って `main()` 関数内で `co_await` を使用することができないからです。
すべての非同期関数は、 `promise<T>` を返却する必要があります。
従って、 `main_async()` のような「基底非同期関数」を用意して呼び出す手法が、この問題を簡単に解決する方法です（他にも方法はありますが、省略）。

---

## 例外

例外も自然に扱えますが、 `promise<T>` を `co_await` で待機しない場合は、注意が必要です。
以下の例では、非同期関数内で、非同期待機 (`co_await`) を行う前に例外を送出する例です:

```cpp
// 非同期関数(非同期待機前例外)
static cardio::promise<void> exception_async() {
  // 非同期待機を行う前に例外を送出する
  throw std::exception();
  co_return;
}

// 非同期関数
static cardio::promise<void> main_async() {
  try {
    // 非同期処理を開始するが、すぐに例外が発生
    auto p = exception_async();  // (co_awaitで待機していない)
    (void)p;
  } catch (const std::exception& ex) {
    // 呼び出し元で受信できる
    printf("%s\n", ex.what());
  }
}

int main() {
  cardio::dispatcher_host d;

  // 非同期関数を呼び出して、非同期処理を開始
  auto p = main_async();
  (void)p;

  // このスレッドを待機させ、非同期継続処理を実行する
  d.park();

  return 0;
}
```

上記の例は問題なく例外を検出できます。

しかし、非同期待機を行ったあとに例外が送出される場合は、非同期関数を呼び出した箇所の `try` / `catch` では例外を受信できません。
保存された例外は、呼び出し元が `co_await` で `promise` の結果を取り出すときに再送出されます:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  co_return a + b;
}

// 非同期関数(非同期待機後例外)
static cardio::promise<void> exception_async() {
  // 非同期関数を呼び出して待機する
  co_await calc_async(1, 2);
  // その後、例外を送出する
  throw std::exception();
}

// 非同期関数(例外を送出しない)
static cardio::promise<void> main_async() noexcept {
  try {
    // 非同期処理を開始し、co_awaitで待機する
    co_await exception_async();
  } catch (const std::exception& ex) {
    // co_awaitで待機していれば、呼び出し元で受信できる
    printf("%s\n", ex.what());
  }
}

int main() {
  cardio::dispatcher_host d;

  // 非同期関数を呼び出して、非同期処理を開始
  auto p = main_async();
  (void)p;

  // このスレッドを待機させ、非同期継続処理を実行する
  d.park();

  return 0;
}
```

このように、 常に `co_await` で待機を行うように記述すれば、例外を送出する関数が非同期待機前や後のどちらでも、 `catch` ブロックで例外を捕捉できます。

ところで、 `main_async()` が例外を送出してしまった場合は、「未処理の例外」として扱われます。
これは、`dispatcher` の `unhandled_exception()` をフックすることで、検出できます:

```cpp
// 非同期関数
static cardio::promise<void> exception_async() {
  // 非同期待機後に例外を送出する
  co_await cardio::resolved();
  throw std::exception();
}

// 非同期関数(例外が漏れている)
static cardio::promise<void> main_async() {
  // 非同期処理を開始する
  co_await exception_async();
}

int main() {
  cardio::dispatcher_host d;

  // 未処理例外のフック
  d.unhandled_exception([](std::exception_ptr exception) {
    try {
      std::rethrow_exception(exception);
    } catch (const std::exception& ex) {
      printf("%s\n", ex.what());
    }
  });

  // 非同期関数を実行
  // (基底promiseは生存させるが、誰も結果を取り出さない)
  auto p = main_async();
  (void)p;

  // このスレッドを待機させ、非同期継続処理を実行する
  d.park();

  return 0;
}
```

- `unhandled_exception()` フックが設定されていない場合、例外は `std::clog` に記録されます。
- `unhandled_exception()` フック自体が例外を送出した場合、その例外は無視されます。

`unhandled_exception()` フックの実装はやや低レベルであり、しかも `main_async()` からは切り離されているので、複雑な作業は行えません。
従って、`main_async()` のようなエントリポイントを司る非同期関数は、できるだけ非同期関数内ですべての例外を捕捉して処理するようにし、
`unhandled_exception()` フックに頼らないことをお勧めします。

注意: このドキュメント内のコード例は、必要でない限り例外処理を示していないことに注意して下さい。

---

## promiseの操作方法

`promise<T>` は、非同期関数の継続を管理します。また、値を返す結果と例外の保持も行います。
`promise<T>` を返す関数は非同期関数として扱われます。関数内部で `co_await` や `co_return` を使用して、非同期処理を実現できます:

```cpp
static cardio::promise<void> main_async() {
  // 非同期関数を呼び出して待機する
  auto r = co_await calc_async(1, 2);

  // 結果を表示
  printf("%d\n", r);
}
```

但し、すぐに値を返したり例外をスローさせる場合は、 `co_await` などを使用することなく、 `resolved()` や `rejected()` を使用することもできます:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  // 非同期待機せずに直接結果を返す
  return cardio::resolved(a + b);
}

static cardio::promise<int> calc_will_fail_async(int a, int b) {
  // 非同期待機せずに直接失敗を返す
  return cardio::rejected<int>(std::runtime_error("calc failed"));
}
```

外部のコールバックや別スレッドなど、非同期関数の外側から後で `promise<T>` を完了させたい場合は `promise_source<T>` を使用できます。
`promise_source<T>` は完了操作を行う側、`promise<T>` は結果を待機・取得する側として役割を分離します。

これは、C++20非同期処理をネイティブでサポートしないAPIを、promise化することに使用できます:

```cpp
static cardio::promise<int> calc_async(int param) {
  // promise_sourceを生成し、将来の結果返却に備える
  auto source = std::make_shared<cardio::promise_source<int>>();

  // 関連付けられたpromiseを取得
  auto p = source->get_promise();

  // 非同期処理を開始する
  // 例えば、コールバックで完了を受信出来る外部APIを使用する
  foobar::request(param, [source]() {
    // 完了したのでpromiseに通知
    source->resolve(42);
  });

  return p;
}
```

- `promise_source<T>::get_promise()` は1回だけ呼び出せます。
- `resolve()` または `reject()` によって、対応する `promise<T>` は完了し、待機中の継続が再開されます。
- `reject()` は例外有効ビルドでのみ使用できます。
- `try_resolve()` と、例外有効ビルドの `try_reject()` は、すでに完了済みの場合に例外を送出せず `false` を返します。
- 例外有効ビルドでは、`cancel()` または `try_cancel()` によって `canceled_exception` による失敗として完了できます。
- 未完了の `promise_source<T>` が破棄された場合、例外有効ビルドでは `std::runtime_error` による失敗として `promise<T>` が完了します。

より具体的な例を示します。以下の例は、架空のC言語向けリモートAPI呼び出しを、非同期関数でラップする例です:

```cpp
// リモートAPIはC言語エントリポイントなので、ステートを渡すための構造体を定義
struct foobar_request_state {
  std::shared_ptr<cardio::promise_source<std::string>> source;
};

// リモートAPIを呼び出して結果を得る
static cardio::promise<std::string> request_foobar_async() {

  // リモートAPIに渡すステート管理構造体を準備
  auto* state = new foobar_request_state{};
  state->source = std::make_shared<cardio::promise_source<std::string>>();
  auto promise = state->source->get_promise();

  // リモートAPIを呼び出す。結果は関数ポインタのコールバックで返却される。
  foobar_request_text(
    // 成功コールバック
    [](const char* text, void* state_ptr) {
      auto* state = static_cast<foobar_request_state*>(state_ptr);
      auto source = state->source;
      delete state;
      // promiseに通知
      source->resolve(text);
    },
    // 失敗コールバック
    [](int error, void* state_ptr) {
      auto* state = static_cast<foobar_request_state*>(state_ptr);
      auto source = state->source;
      delete state;
      // promiseに通知
      source->reject(std::system_error(
          error, std::generic_category(), "foobar_request_text failed"));
    },
    state);

  return promise;
}
```

---

## 非同期処理におけるC++特有の落とし穴

非同期関数は呼び出した時点で実行を開始しますが、返された `promise` は非同期処理(coroutine)を所有しています。
基底promiseをすぐに破棄してはいけません:

```cpp
// 悪い例: 返されたpromiseは、この完全式の直後に破棄される
(void)main_async();

d.park();
```

結果を取得しない場合でも、`park()` が戻るまでは基底promiseを生存させて下さい:

```cpp
auto p = main_async();
(void)p;

d.park();
```

基底promiseの結果が不要であることが明確な場合は、`fire_and_forget()` によって完了まで生存させることもできます:

```cpp
cardio::fire_and_forget(main_async());

d.park();
```

例外有効ビルドでは、`fire_and_forget()` に渡したpromiseから漏れた失敗は、他の未処理の基底promiseの失敗と同様に `dispatcher::unhandled_exception()` へ通知されます。
これはpromiseの生存期間だけを保持する機能であり、非同期処理が参照するオブジェクトの生存期間を延長するものではありません。

非同期関数が参照する値にも、同じ規則が当てはまります。
非同期処理が参照するすべてのオブジェクトは、非同期処理よりも長く生存している必要があります。
特に、キャプチャを持つ非同期ラムダには注意が必要です。
例えば、次のラムダオブジェクトは呼び出し直後に破棄されますが、非同期処理は後から再開され、そのキャプチャを読み取る可能性があります:

```cpp
auto p = [&]() -> cardio::promise<void> {
  result = co_await work_async();
}();
```

できるだけ、名前付きの非同期関数を使用して下さい:

```cpp
static cardio::promise<void> run_async(int& result) {
  result = co_await work_async();
}

auto p = run_async(result);
(void)p;

d.park();
```

あるいは、ラムダオブジェクトを非同期処理の期間中生存させます:

```cpp
auto run = [&]() -> cardio::promise<void> {
  result = co_await work_async();
};

auto p = run();
(void)p;

d.park();
```

いずれの場合も、`p`、`run`、`result`、その他参照されるオブジェクトは、非同期処理が完了するまで生存している必要があります。

---

## キャンセル処理

`cancellation_source` と `cancellation` を使用すると、非同期処理にキャンセル要求を通知できます。
これはTypeScript/JavaScriptの `AbortController` / `AbortSignal` に近い役割です。

`cancellation_source` はキャンセルを要求する側、`cancellation` はキャンセルを監視する側のハンドルです。
以下の例では、`cancellation` 対応のAPIにキャンセル要求を通知する例です:

```cpp
// 指定されたファイルを読み取る
static cardio::promise<std::string> read_file_async(
  const std::string& path, cardio::cancellation cancellation) {
  // 能動的にキャンセル状態を確認する
  cancellation.throw_if_cancellation_requested();

  // ファイルをオープンする
  auto fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);

  try {
    auto text = std::string{};
    auto buffer = std::array<char, 4096>{};

    while (true) {
      // ファイルが読み取り可能状態になるのを待機する
      // キャンセル通知を待機対象に渡す
      auto events = co_await cardio::from_fd(
          fd, cardio::fd_event::read, cancellation);

      // ファイルを読み取る
      const auto size = read(fd, buffer.data(), buffer.size());
      if (size == 0) {
        break;
      }
      text.append(buffer.data(), static_cast<std::size_t>(size));
    }

    close(fd);
    co_return text;
  } catch (...) {
    close(fd);
    throw;
  }
}

static cardio::promise<void> main_async() noexcept {
  try {
    // cancellation_sourceを準備
    cardio::cancellation_source source;

    // (タイムアウト時間が経過したら非同期処理をキャンセルする)
    my_timeout(10000, [&]() {
      source.cancel();
    });
  
    // cancellationを渡して非同期処理を開始
    auto r = co_await read_file_async(
      "foobar.txt", source.get_cancellation());
  
    // 結果
    std::cout << "result: " << r << '\n';
  } catch (const std::exception& ex) {
    printf("%s\n", ex.what());
  }
}

int main() {
  cardio::dispatcher_host d;

  auto p = main_async();
  (void)p;

  // キャンセルが発生した場合は、main_async()内で処理される
  d.park();

  return 0;
}
```

- `cancellation_source::get_cancellation()` で、読み取り側の `cancellation` を取得できます。
- `cancellation_source::cancel()` は、初回のキャンセル要求で `true`、すでにキャンセル済みの場合は `false` を返します。
- `cancellation::is_cancellation_requested()` は、現在キャンセルが要求済みかどうかを返します。
  これは能動的な確認であり、呼び出し側が明示的に確認した場所でだけキャンセルを検出します。
  または、 `cancellation::throw_if_cancellation_requested()` を使用することもできます。これはキャンセル要求済みの場合に `canceled_exception` を送出します。

`cancellation` に対応していないAPIを使用する場合は、キャンセル要求を自分で処理する必要があります。
`cancellation::on_cancellation_requested()` を使用すると、キャンセル要求が発生したときに、コールバックが実行される機会を得られます。

以下の例は、前章で示した非同期ラップ関数に、完全なキャンセル処理を追加するものです:

```cpp
// リモートAPIはC言語エントリポイントなので、ステートを渡すための構造体を定義
struct foobar_request_state {
  std::shared_ptr<cardio::promise_source<std::string>> source;
  cardio::cancellation_registration registration;
  foobar_request_handle request = nullptr;
};

// リモートAPIを呼び出して結果を得る
static cardio::promise<std::string> request_foobar_async(
    cardio::cancellation cancellation) {

  // リモートAPIに渡すステート管理構造体を準備
  auto* state = new foobar_request_state{};
  state->source = std::make_shared<cardio::promise_source<std::string>>();
  auto promise = state->source->get_promise();

  // リモートAPIを呼び出す。結果は関数ポインタのコールバックで返却される。
  state->request = foobar_request_text(
    // 成功コールバック
    [](const char* text, void* state_ptr) {
      auto* state = static_cast<foobar_request_state*>(state_ptr);
      auto source = state->source;
      // キャンセルコールバック登録を解除
      state->registration.reset();
      delete state;
      // promiseに通知
      (void)source->try_resolve(text);
    },
    // 失敗コールバック
    [](int error, void* state_ptr) {
      auto* state = static_cast<foobar_request_state*>(state_ptr);
      auto source = state->source;
      // キャンセルコールバック登録を解除
      state->registration.reset();
      delete state;
      // promiseに通知
      (void)source->try_reject(std::system_error(
          error, std::generic_category(), "foobar_request_text failed"));
    },
    state);

  // 関数外からキャンセル要求が発生した場合の処理
  state->registration = cancellation.on_cancellation_requested(
    [state] {
      auto source = state->source;
      // API呼び出しをキャンセル:
      // この例では、foobar_cancel() の後に失敗コールバックが呼び出され、stateはそこで解放される
      foobar_cancel(state->request);
      // promiseに通知
      (void)source->try_cancel();
    });

  return promise;
}
```

- `on_cancellation_requested()` は、キャンセル要求時に呼び出されるコールバックを登録します。
  戻り値の `cancellation_registration` を破棄または `reset()` すると登録解除されます。
- キャンセルは、ベストエフォートです。
  実行中の非同期処理を外部から強制破棄するものではなく、キャンセル対応APIや呼び出し側の能動チェックで検出されます。

### 完了競合を扱う

外部APIのコールバックやI/O要求では、正常完了、失敗、キャンセルが競合することがあります。
このような場合は `promise_source<T>` の `try_*` 系APIを使用できます:

```cpp
static cardio::promise<int> request_async(
    cardio::cancellation cancellation) {
  auto source = std::make_shared<cardio::promise_source<int>>();
  auto promise = source->get_promise();
  auto registration =
      std::make_shared<cardio::cancellation_registration>();

  *registration = cancellation.on_cancellation_requested([source] {
    source->try_cancel();
  });

  foobar_request_number(
    [source, registration](int value) {
      registration->reset();
      (void)source->try_resolve(value);
    },
    [source, registration](std::exception_ptr ep) {
      registration->reset();
      (void)source->try_reject(std::move(ep));
    });

  return promise;
}
```

- `resolve()`、`reject()`、`cancel()` は、すでに完了済みの場合に `std::logic_error` を送出します。
- `try_resolve()`、`try_reject()`、`try_cancel()` は、すでに完了済みの場合に `false` を返します。
- 競合が通常の制御フローとして起こる場所では、`try_*` を使用してください。

---

## POSIXファイルディスクリプタの待機

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

## Android Looperとの統合

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

実装方針と検証マトリックスの詳細は [`docs/android.md`](./docs/android.md) を参照してください。

---

## Linux io_uringで非同期I/Oを実装

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

### 追加のio_uring operationを自分で実装する

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

## GLib 2 (GTK) との統合

GLib 2やGTKが利用できる環境では、`CARDIO_WITH_GLIB=1` を指定することで、GLibのスケジューラーを統合できます。
つまり、GTKなどのアプリケーション内で、cardioを使用した非同期処理を統合できます。

この機能を使用するには、GLibの開発ヘッダとライブラリを導入し、GLibをリンクしてください。
例えば、Debian/Ubuntuでは以下のようにAPTパッケージで導入できます:

```bash
sudo apt update
sudo apt install -y libglib2.0-dev pkg-config
```

コンパイル時には、`CARDIO_WITH_GLIB=1` を定義し、`pkg-config` でGLibのコンパイルオプションとリンクオプションを渡します:

```bash
g++ -std=c++20 -Iinclude -DCARDIO_WITH_GLIB=1 app.cpp -o app $(pkg-config --cflags --libs glib-2.0)
```

GLib統合では、 `dispatcher_group_glib`、`dispatcher_host_glib`、`dispatcher_host_glib_auto` を使用します。
`dispatcher_group_glib` は、アプリケーションの終了タイミングを管理します（`dispatcher_group` については後述）:

```cpp
int main() {
  // GLib sourceとcardio workを同時待機するdispatcherを初期化する
  cardio::dispatcher_group_glib group;
  cardio::dispatcher_host_glib d(group);

  // (GLib、GTK、またはGLibベースのmessage pumpを使用するコード。
  //  終了タイミングでshutdown()を呼び出す)
  // {
  //   //  :
  //   //  :
  //   group.shutdown();
  // }

  // GLib sourceとcardio workの両方を待機しながらメインスレッドをparkする。
  d.park();

  return 0;
}
```

- `dispatcher_host_glib` は、`park()` でGLib sourceとcardio workを同時に待機します。
  `park_policy` は、readyなGLib sourceとcardio継続のどちらを先に実行するかを制御します。
- `dispatcher_host_glib_auto` は、グループの `GMainContext` にcardio用の `GSource` をattachします。
  そのため、同じcontextを対象にしたアプリケーション所有のGLib message pumpでも、cardioの継続、timer wait、POSIX fd wait、io_uring completionを実行できます。
  このhostの `park()` は、同じ `GMainContext` を使う互換用の薄いラッパーで、実行順はGLib source priorityで決まります。
- `dispatcher_group_glib::shutdown()` を呼び出すと、 `park()` が終了します。
  `dispatcher` を単独で使用していた場合と異なり、非同期処理の継続が存在しなくなっても、自動的に `park()` は終了しません。

---

## GIOファイルI/Oヘルパー

GLib統合が有効な場合、`CARDIO_WITH_GIO=1` を指定するとGIOファイルI/Oヘルパーを使用できます。
この機能は `CARDIO_WITH_GLIB=1` と `gio-2.0 >= 2.44` を要求します:

```bash
g++ -std=c++20 -Iinclude -DCARDIO_WITH_GLIB=1 -DCARDIO_WITH_GIO=1 app.cpp -o app $(pkg-config --cflags --libs gio-2.0)
```

ヘルパーは `cardio::gio` 名前空間にあります。
stream、file、file enumerator、file streamの `query_info()` など、安定した単発の `*_async()` / `*_finish()` GIO operationを `cardio::promise` に接続します。
これ以外のoperationは、`cardio::gio::submit<T>(start, finish)` で利用側がpromise化できます。

例えば、GIOヘルパーを使ってファイル全体の内容をコピーできます:

```cpp
static cardio::promise<void> copy_file_contents_async(
    GFile* source,
    GFile* destination) {
  auto contents = co_await cardio::gio::load_contents(source);
  co_await cardio::gio::replace_contents(
      destination,
      std::span<const std::byte>(contents.bytes.data(), contents.bytes.size()),
      nullptr,
      false,
      G_FILE_CREATE_NONE);
}
```

operation開始前に、ヘルパーは `get_current_dispatcher().get_feature()` に `dispatcher_feature::gio` が含まれているかを確認します。
GIOサポート付きでコンパイルされた `dispatcher_host_glib` と `dispatcher_host_glib_auto` は、`dispatcher_feature::glib` と `dispatcher_feature::gio` の両方を返します。
通常の `dispatcher_host` は `gio` を返さないため、GIOヘルパーは `std::invalid_argument` をスローします。

所有権と生存期間はGIOの規約に従います:

- 返される `GObject*`、`GBytes*`、`GList*` は呼び出し元が保持する値です。
  `g_object_unref()`、`g_bytes_unref()`、`g_list_free_full(list, g_object_unref)` など、対応するGIO APIで解放してください。
- read、write、replace系ヘルパーに `std::span` で渡すbufferは、返されたpromiseが完了するまで有効である必要があります。
- `GBytes*` を受け取るヘルパーでは、operation中の生存期間はGIOが保持する参照に依存します。
  呼び出し側はasync call開始後に自身の参照を解放できます。
- `GError` は `cardio::gio::gio_error` に変換され、元のdomain、code、messageを保持します。
  `G_IO_ERROR_CANCELLED` は `cardio::canceled_exception` に変換されます。

cancellableな `submit<T>()` overloadはhelper所有の `GCancellable` を作成します。
渡された `cardio::cancellation` が要求されると、helperは `g_cancellable_cancel()` を呼び出し、GIO callbackが呼ばれた時点でpromiseが完了します。

---

## Win32 HANDLEの待機

Windowsでは、`CARDIO_HAS_WIN32_HANDLE=1` が有効な場合に、Win32 HANDLEを `promise` として待機できます。
`_WIN32` ビルドではデフォルトで有効です。

`dispatcher_host_win32::park()` は `MsgWaitForMultipleObjectsEx()` を使用するため、
登録されたHANDLEの待機とWin32メッセージpumpを同じスレッドで行えます。
これは、COMのアパートメントスレッドやマーシャリングの運用にも適しています。
デフォルトでは、キュー済みのWin32メッセージは、キュー済みまたはinlineのcardio継続より先にpumpされます。
cardio継続を先に実行するには、`cardio::park_policy::continuation_first` を `park()` の第2引数に指定します。

`dispatcher_host_win32_auto` は message-only window を作成し、cardio継続、timer、Win32 HANDLE待機をprivateなWin32メッセージ経由で配送します。
`park()` を呼び出さない既存またはモーダルなWin32 message pumpが動作する可能性がある場合に使用します。
このdispatcherは同じスレッドで構築し、`park()` も同じスレッドで呼び出してください。
cardio workの実行タイミングはWin32 message queueの順序で決まります。

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
  `CancelIoEx()` の呼び出しやHANDLEのcloseは行いません。`win32` のoperationでは、
  helperが所有する `OVERLAPPED` operationに対してbest-effortで `CancelIoEx()` を呼び出し、
  native operationの完了後に返されたpromiseを完了させます。
- Win32待機backendは `MAXIMUM_WAIT_OBJECTS` の制約を受けます。通常、この値は64です。
  dispatcherのwakeup eventとmessage queueが待機slotを消費するため、
  1つのdispatcherで同時に待機できるユーザーHANDLEは、最大で `MAXIMUM_WAIT_OBJECTS - 2` 個です。

### Win32 I/O completion portヘルパー

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
  pump threadはIOCP packetを待機し、完了したpromiseをoperation開始時のdispatcherへ戻します。
- `iocps::submit()` を使うと、他の単発 `OVERLAPPED` operationもpromise化できます。
  completion callbackは `win32_iocp_completion` を受け取ります。
- HANDLEとI/O bufferの所有権は取得しません。完了まで呼び出し側で有効に保つ必要があります。
- 同じHANDLEを `from_win32_overlapped()` や別のcompletion portと混在させないでください。
  IOCPヘルパーはHANDLEを自身のcompletion portへ関連付けます。

---

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

---

## マルチスレッド継続ワーカー (高度なトピック)

ここまでの例では、`park()` は常にメインスレッドを用いて実行してきました。
`dispatcher_host` は `park()` を複数のスレッドから呼び出すことができます:

```cpp
#include <thread>

int main() {
  // dispatcherを初期化
  // このdispatcherは自動的に現在のスレッド（メインスレッド）に関連付けられる
  cardio::dispatcher_host d;

  // (何らかの非同期I/Oを開始)

  // 実行スレッドをdispatcherで待機させる
  auto runner = [&]() {
    // 他のスレッドでは、dispatcherが自動的に関連付けられないので
    // 明示的に関連付ける必要がある
    cardio::set_current_dispatcher(&d);

    // このスレッドを待機させ、非同期継続処理を実行する
    d.park();
  };

  // 2つのワーカースレッドを生成
  std::thread t1(runner);
  std::thread t2(runner);

  // 更にメインスレッドも待機させ、同時に3つのスレッドが継続を実行できるようにする
  d.park();

  // ワーカースレッドが終了するのを待機
  t1.join();
  t2.join();

  return 0;
}
```

- 複数のスレッドで同時に継続を実行することができるため、非同期処理の継続を効率よく処理できます。
- 追加のスレッドは、`set_current_dispatcher()` で現在の `dispatcher` を設定してから `park()` を呼び出すことで、継続実行の待機を行わせることができます。
- メインスレッドだけで処理していた時と同様に、すべての非同期処理が完了して、他に待機する処理がなくなると、`park()` 関数が終了します。

継続処理は、異なるスレッドで実行される可能性があることに注意してください:

```cpp
#include <thread>

static cardio::promise<void> main_async(int a, int b) noexcept {
  // 非同期待機前のスレッドID
  auto before_id = std::this_thread::get_id();

  // 非同期関数を待機
  co_await calc_async(a, b);

  // 非同期待機後のスレッドID
  auto after_id = std::this_thread::get_id();

  // 注意: dispatcherをメインスレッドのみで実行する場合は満たされるが、
  // マルチスレッドで実行する場合は満たされない場合がある
  ASSERT(before_id == after_id);
}
```

- 当然、継続処理はマルチスレッドにおける競合条件に配慮する必要があります。

---

## dispatcherの切り替え (高度なトピック)

`switch_to()` を使用すると、現在実行中の非同期関数の継続を、明示的に別の `dispatcher` へ切り替えることができます。
例えば、メインスレッド専用の `dispatcher` と、ワーカースレッド群用の `dispatcher` を分けておくことで、処理の途中だけをワーカー側で実行し、その後メインスレッド側へ戻すことができます。
複数の `dispatcher` を協調させる場合は、同じ `dispatcher_group` に所属させます。

`dispatcher_group` は、非同期処理の継続実行数を管理して、`dispatcher` が完全に終了すべきかどうかを判断します。

通常はすべての継続処理とコールバックが無くなると `park()` が終了します。
空状態では終了させず、明示的な指示で終了させたい場合は `dispatcher_group` を `exit_condition::exit_by_manual` で作成し、終了時に `shutdown()` を呼び出します。

この条件では、継続処理やコールバックが無くなっても `park()` は終了しません。
`park()` はデフォルトで `shutdown_mode::gentle` のshutdown処理を使用し、既にキューに入っている処理と、既に完了通知が到達している待機処理を実行してから終了します。
`shutdown()` 後に、キュー済み処理や未完了の待機処理を残したまま強制的に終了したい場合は、
`park(shutdown_mode::unsafe_immediate)` を指定します。

`switch_to()` は指定された `dispatcher` に継続処理を投入します。
同じ `dispatcher` へ切り替える場合は待機せず、そのまま処理を継続します。
切り替え先の `dispatcher` は、投入された継続が完了するまで生存していて、いずれかのスレッドで `park()` される必要があります。

以下は、メインスレッド用の `dispatcher` と、2つのワーカースレッドで `park()` する `dispatcher` を切り替える例です:

```cpp
#include <iostream>
#include <thread>

static cardio::promise<void> main_async(
    cardio::dispatcher& md, cardio::dispatcher& wd) noexcept {
  // ここではまだメインスレッドで実行
  std::cout << "main thread: " << std::this_thread::get_id() << '\n';

  // 継続をワーカー用dispatcherへ移動
  // (どのワーカースレッドで実行されるかはわからない)
  co_await cardio::switch_to(wd);
  std::cout << "worker thread (arbitrary): " << std::this_thread::get_id() << '\n';

  // 継続をメインスレッド用dispatcherへ戻す
  co_await cardio::switch_to(md);
  std::cout << "main thread again: " << std::this_thread::get_id() << '\n';
}

int main() {
  // dispatcher_groupを使用して、複数のdispatcherの終了を管理する
  cardio::dispatcher_group group;

  // dispatcher_groupに最初に関連付けられたdispatcher (ここではmd)が、
  // 現在のスレッド(メインスレッド)のdispatcherとして自動的に関連付けられる
  cardio::dispatcher_host md(group);
  cardio::dispatcher_host wd(group);

  // ワーカースレッドをwdで待機させる
  auto runner = [&]() {
    // 他のスレッドでは、dispatcherが自動的に関連付けられないので
    // 明示的に関連付ける必要がある
    cardio::set_current_dispatcher(&wd);

    // このスレッドを待機させ、非同期継続処理を実行する
    wd.park();
  };

  // 2つのワーカースレッドを生成
  std::thread worker1(runner);
  std::thread worker2(runner);

  // 非同期関数を実行
  auto p = main_async(md, wd);
  (void)p;

  // メインスレッドを待機させ、非同期継続処理を実行する
  md.park();

  // ワーカースレッドが完了するのを待つ
  worker1.join();
  worker2.join();

  return 0;
}
```

---

## promiseの特殊な使用方法 (高度なトピック)

通常、非同期関数の結果は `co_await` を使用して取得します。
cardioは、必要がない限りは `co_await` を使用することを強く推奨します。

但し、特殊な状況下では `co_await` を使わずに、結果を取得しなければならない場合があるかもしれません。
そのような場合、 `unsafe_result()`, `is_ready()`, `try_result()` を使用すれば、非同期関数のコンテキスト外で値を取得できます。

`is_ready()` は `promise<T>` が完了しているかどうかを直接的に確認できます:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  // 非同期関数を開始し、promiseを得る
  auto p = calc_async(1, 2);

  // (ここではまだpromiseが完了していないかもしれない)
  if (p.is_ready()) {
    printf("promise is done, r=%d\n", p.unsafe_result());
  }

  // このスレッドを待機させ、非同期継続処理を実行する
  d.park();

  // (ここではpromiseが完了しているはず)
  ASSERT(p.is_ready());
  // unsafe_result()の呼び出しは合法
  printf("promise is done, r=%d\n", p.unsafe_result());

  return 0;
}
```

- `unsafe_result()` は、まだ完了していない結果を取得しようとすると、未定義動作となります。
  上記の例では、`park()` が終了するときはすべての非同期処理も完了しているはずなので、問題ありません。

あるいは `try_result()` のほうがスマートかもしれません:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  auto p = calc_async(1, 2);

  // promiseが完了していない場合は、nullptrを返す
  if (auto r = p.try_result(); r != nullptr) {
    printf("promise is done, r=%d\n", *r);
  }

  // このスレッドを待機させ、非同期継続処理を実行する
  d.park();

  // promiseが完了している場合は、値へのポインタを返す
  if (auto r = p.try_result(); r != nullptr) {
    printf("promise is done, r=%d\n", *r);
  }

  return 0;
}
```

- `unsafe_result()` や `try_result()` は、`promise<T>` が失敗している場合は、保存されている例外を再送出することに注意して下さい。
  必要であれば、`catch` ブロックで例外を捕捉する必要があります。

繰り返しますが、これらの関数を使用して結果の取得を試みることは、一般的なコードの記述で避けるべきです。
`co_await` を使用して素直に非同期処理を記述することをお勧めします。

---

## 共有ランタイムライブラリ (高度なトピック)

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

---

## 例外無効ビルド (高度なトピック)

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

## 備考

これは [libbounce](https://github.com/kekyo/libbounce/) の後継プロジェクトです。
(更に遡れば、 [future-promise](https://github.com/kekyo/future-promise/) もあります)

libbounceが複雑化したため、CとC++の役割を逆転させ、C++基準で再構築しました。

C++では、`std::function` を含むインライン展開が行われるので、libbounceで問題だった関数ポインタの最適化が抑制される問題を回避できます。

## License

Under MIT.
