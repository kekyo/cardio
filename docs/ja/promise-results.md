# promiseの特殊な使用方法

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

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/promise-results.md)
