# 基本的な使用方法

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

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/getting-started.md)
