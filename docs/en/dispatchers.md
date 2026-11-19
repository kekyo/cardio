# Using Dispatchers Across Threads

## Multithreaded Continuation Workers

In the examples so far, `park()` has always run on the main thread.
`dispatcher_host` can call `park()` from multiple threads:

```cpp
#include <thread>

int main() {
  // Initialize the dispatcher.
  // This dispatcher is automatically associated with the current thread (the main thread).
  cardio::dispatcher_host d;

  // (Start some asynchronous I/O.)

  // Let an execution thread wait on the dispatcher.
  auto runner = [&]() {
    // In other threads, the dispatcher is not automatically associated,
    // so it must be associated explicitly.
    cardio::set_current_dispatcher(&d);

    // Park this thread and execute asynchronous continuations.
    d.park();
  };

  // Create two worker threads.
  std::thread t1(runner);
  std::thread t2(runner);

  // Also park the main thread, so three threads can execute continuations concurrently.
  d.park();

  // Wait for the worker threads to finish.
  t1.join();
  t2.join();

  return 0;
}
```

- Because continuations can be executed concurrently by multiple threads,
  asynchronous continuations can be processed efficiently.
- Additional threads can wait for continuation execution by calling `park()` after
  setting the current `dispatcher` with `set_current_dispatcher()`.
- As with processing on only the main thread, the `park()` function exits once all
  asynchronous processing is complete and there is nothing else to wait for.

Note that continuation processing may run on different threads:

```cpp
#include <thread>

static cardio::promise<void> main_async(int a, int b) noexcept {
  // Thread ID before asynchronous wait.
  auto before_id = std::this_thread::get_id();

  // Await an asynchronous function.
  co_await calc_async(a, b);

  // Thread ID after asynchronous wait.
  auto after_id = std::this_thread::get_id();

  // Note: this is satisfied when the dispatcher runs only on the main thread,
  // but it may not be satisfied when running on multiple threads.
  ASSERT(before_id == after_id);
}
```

- Naturally, continuation processing must take race conditions in multithreaded
  execution into account.

## Switching Dispatchers

Using `switch_to()`, you can explicitly switch the continuation of the currently
running asynchronous function to another `dispatcher`.
For example, by separating a `dispatcher` dedicated to the main thread from a
`dispatcher` for worker threads, you can run only part of the processing on the
worker side and then return to the main thread side.
When coordinating multiple `dispatcher` instances, make them belong to the same
`dispatcher_group`.

`dispatcher_group` manages the number of asynchronous continuations in execution
and determines whether the `dispatcher` should fully exit.

Normally, `park()` exits when all continuation processing and callbacks are gone.
If you do not want it to exit when empty and instead want it to exit only on an
explicit instruction, create the `dispatcher_group` with
`exit_condition::exit_by_manual` and call `shutdown()` at exit.

Under this condition, `park()` does not exit even when no continuations or
callbacks remain.
By default, `park()` uses `shutdown_mode::gentle` shutdown handling and
exits after executing work that is already queued and wait operations whose
completion notifications have already arrived.
Specify `park(shutdown_mode::unsafe_immediate)` if you want to force
termination after `shutdown()` while leaving queued work or incomplete waits
behind.

`switch_to()` posts the continuation to the specified `dispatcher`.
When switching to the same `dispatcher`, it does not wait and continues
processing as-is.
The destination `dispatcher` must remain alive until the posted continuation is
complete and must be `park()`ed by some thread.

The following example switches between a `dispatcher` for the main thread and a
`dispatcher` that is `park()`ed by two worker threads:

```cpp
#include <iostream>
#include <thread>

static cardio::promise<void> main_async(
    cardio::dispatcher& md, cardio::dispatcher& wd) noexcept {
  // Still running on the main thread here.
  std::cout << "main thread: " << std::this_thread::get_id() << '\n';

  // Move the continuation to the worker dispatcher.
  // (Which worker thread runs it is unspecified.)
  co_await cardio::switch_to(wd);
  std::cout << "worker thread (arbitrary): " << std::this_thread::get_id() << '\n';

  // Move the continuation back to the main-thread dispatcher.
  co_await cardio::switch_to(md);
  std::cout << "main thread again: " << std::this_thread::get_id() << '\n';
}

int main() {
  // Use dispatcher_group to manage shutdown of multiple dispatchers.
  cardio::dispatcher_group group;

  // The first dispatcher associated with the dispatcher_group (md here)
  // is automatically associated as the current thread's (main thread's) dispatcher.
  cardio::dispatcher_host md(group);
  cardio::dispatcher_host wd(group);

  // Let worker threads wait on wd.
  auto runner = [&]() {
    // In other threads, the dispatcher is not automatically associated,
    // so it must be associated explicitly.
    cardio::set_current_dispatcher(&wd);

    // Park this thread and execute asynchronous continuations.
    wd.park();
  };

  // Create two worker threads.
  std::thread worker1(runner);
  std::thread worker2(runner);

  // Run the asynchronous function.
  auto p = main_async(md, wd);
  (void)p;

  // Park the main thread and execute asynchronous continuations.
  md.park();

  // Wait for the worker threads to complete.
  worker1.join();
  worker2.join();

  return 0;
}
```

---

[All guides](../../README.md#documentation) · [日本語](../ja/dispatchers.md)
