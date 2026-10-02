// Standalone diagnostic for the MinGW GCC 12 coroutine lowering issue.
// It intentionally has no cardio dependency and is not part of the test suite.
// Define CARDIO_PROBE_SEPARATE_AWAIT to check the supported expression form.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
#include <coroutine>
#include <cstdio>
#include <cstdlib>
static void* expected;
struct task {
  struct promise_type {
    void* marker = nullptr;
    task get_return_object() {
      expected = this;
      return {std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_always final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::abort(); }
  };
  std::coroutine_handle<promise_type> handle;
  ~task() { handle.destroy(); }
};
struct pause {
  bool await_ready() { return false; }
  template<typename T> void await_suspend(std::coroutine_handle<T> handle) {
    if (&handle.promise() != expected) {
      std::printf("Wrong promise address: %p != %p\n",
          static_cast<void*>(&handle.promise()), expected);
      std::exit(1);
    }
  }
  int await_resume() { return 42; }
};
static task run() {
#if defined(CARDIO_PROBE_SEPARATE_AWAIT)
  const auto value = co_await pause{};
  if (value != 42) { std::abort(); }
#else
  if ((co_await pause{}) != 42) { std::abort(); }
#endif
}
int main() { auto t = run(); t.handle.resume(); std::puts("PASS"); }
