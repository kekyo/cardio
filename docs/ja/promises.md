# promiseの操作と寿命

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

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/promises.md)
