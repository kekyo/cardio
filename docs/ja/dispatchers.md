# 複数のスレッドでdispatcherを使う

## マルチスレッド継続ワーカー

[基本的な使用方法](./getting-started.md)の例では、メインスレッドから `park()` を呼び出しています。
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

## dispatcherの切り替え

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

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/dispatchers.md)
