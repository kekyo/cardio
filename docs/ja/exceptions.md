# 例外

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

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/exceptions.md)
