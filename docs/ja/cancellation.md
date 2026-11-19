# キャンセル処理

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

## 完了競合を扱う

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

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/cancellation.md)
