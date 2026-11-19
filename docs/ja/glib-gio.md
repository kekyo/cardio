# GLib・GTKとの統合とGIOファイルI/O

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
`dispatcher_group_glib` は、アプリケーションの終了タイミングを管理します。`dispatcher_group` の役割は[dispatcherの切り替え](./dispatchers.md#dispatcherの切り替え)を参照してください。

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

[ドキュメント一覧](../../README_ja.md#ドキュメント) · [English](../en/glib-gio.md)
