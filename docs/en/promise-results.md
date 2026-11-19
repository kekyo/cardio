# Special Promise Usage

Usually, results from asynchronous functions are obtained with `co_await`.
cardio strongly recommends using `co_await` unless there is a specific reason
not to.

However, in special situations, you may need to obtain a result without using
`co_await`.
In those cases, `unsafe_result()`, `is_ready()`, and `try_result()` can be used
to obtain values outside an asynchronous function context.

`is_ready()` can directly check whether `promise<T>` has completed:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  // Start the asynchronous function and get a promise.
  auto p = calc_async(1, 2);

  // (The promise may not be complete yet here.)
  if (p.is_ready()) {
    printf("promise is done, r=%d\n", p.unsafe_result());
  }

  // Park this thread and execute asynchronous continuations.
  d.park();

  // (The promise should be complete here.)
  ASSERT(p.is_ready());
  // Calling unsafe_result() is valid.
  printf("promise is done, r=%d\n", p.unsafe_result());

  return 0;
}
```

- `unsafe_result()` has undefined behavior if you try to retrieve a result that
  has not completed yet.
  In the example above, this is fine because all asynchronous processing should
  be complete when `park()` returns.

Alternatively, `try_result()` may be smarter:

```cpp
static cardio::promise<int> calc_async(int a, int b) {
  co_return a + b;
}

int main() {
  cardio::dispatcher_host d;

  auto p = calc_async(1, 2);

  // If the promise is not complete, nullptr is returned.
  if (auto r = p.try_result(); r != nullptr) {
    printf("promise is done, r=%d\n", *r);
  }

  // Park this thread and execute asynchronous continuations.
  d.park();

  // If the promise is complete, a pointer to the value is returned.
  if (auto r = p.try_result(); r != nullptr) {
    printf("promise is done, r=%d\n", *r);
  }

  return 0;
}
```

- Note that `unsafe_result()` and `try_result()` rethrow the stored exception if
  `promise<T>` has failed.
  Catch the exception with a `catch` block if necessary.

Again, trying to retrieve results with these functions should be avoided in
ordinary code.
Writing asynchronous processing straightforwardly with `co_await` is
recommended.

---

[All guides](../../README.md#documentation) · [日本語](../ja/promise-results.md)
