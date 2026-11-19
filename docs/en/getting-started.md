# Basic Usage

All public API definitions are placed in the `cardio` namespace.

The most important feature set in cardio is `dispatcher` and `promise`.
You can think of `promise` as roughly equivalent to TypeScript/JavaScript's
`Promise`.

`dispatcher` is the abstract base class that manages asynchronous
"continuations", and corresponds to the runtime. Use `dispatcher_host` as the
standard concrete dispatcher host.
When using cardio, you need to initialize a `dispatcher_host`.

First, place and initialize a `dispatcher_host`, then let it wait so that it can
execute asynchronous continuations:

```cpp
// Header-only library
#include "cardio.h"

int main() {
  // Initialize the dispatcher.
  cardio::dispatcher_host d;

  // Park the main thread and execute asynchronous continuations.
  d.park();

  return 0;
}
```

- The `park()` function continues executing asynchronous continuations as long as
  they exist.

In the code above, no asynchronous processing exists, so the function returns
immediately and the program exits.
Next, insert the first asynchronous process to run:

```cpp
// Async function
static cardio::promise<int> add_async(int a, int b) {
  co_return a + b;  // Return the result of an async function with `co_return`.
}

// Async function
static cardio::promise<void> main_async() {
  auto r = co_await add_async(1, 2);  // Can use `co_await`.
  printf("1 + 2 = %d\n", r);
}

int main() {
  cardio::dispatcher_host d;

  // Call the async function and start asynchronous processing.
  auto p = main_async();
  (void)p;

  // park() returns when there are no more asynchronous continuations.
  d.park();

  return 0;
}
```

The asynchronous function `add_async()` is written to return `promise<int>`.
In the example above, it only performs simple addition, so there is no practical
reason for it to be asynchronous, but returning `promise<int>` makes it clear
that this is an asynchronous function.

Also declare the overall function `main_async()`, which calls and performs work
with asynchronous functions such as `add_async()`, as an asynchronous function.
Because it does not return a result, it uses `promise<void>`.

Calling the asynchronous function `main_async()` before `park()` starts
asynchronous processing.
Keep the returned root promise alive until `park()` finishes, even when you do
not need to retrieve its result.

Even if `add_async()` returns a result instantly as shown above, the caller
cannot know that.
In other words, you do not know when the asynchronous function will complete.
`promise<T>` abstracts this behavior, and `co_await` retrieves its result.

Then `park()` runs the asynchronous processing and continues until it completes.
Once all asynchronous processing is complete and there is nothing else to wait
for, the `park()` function exits.
In this example, execution continues until the calculation completes and the
value is printed with `printf()`.

At this point, you may wonder whether such short code could be written directly
in `main()` without preparing `main_async()`:

```cpp
static cardio::promise<int> add_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  // Could add_async() be called directly here?
  auto r = co_await add_async(1, 2);
  printf("1 + 2 = %d\n", r);

  d.park();

  return 0;
}
```

However, this cannot be done. `main()` does not return a promise, so `co_await`
cannot be used inside `main()`.
Every asynchronous function must return `promise<T>`.
Therefore, preparing and calling a "root asynchronous function" such as
`main_async()` is a simple way to solve this problem. There are other ways, but
they are omitted here.

---

[All guides](../../README.md#documentation) · [日本語](../ja/getting-started.md)
