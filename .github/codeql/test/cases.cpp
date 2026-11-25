// These declarations model the API shapes used by the cross-platform backends.
// The query tests extract this file. The operations are never linked or run.
using size_type = decltype(sizeof(0));
namespace std {
template <class T> struct lock_guard {
  explicit lock_guard(T&);
  ~lock_guard();
};
template <class T, unsigned N> struct array { T* data(); };
template <class T> struct span {
  template <unsigned N> span(T (&)[N]);
  span(T*, size_type);
  size_type size() const;
};
}
namespace cardio {
template <class T> struct promise { ~promise(); };
struct cancellation_registration { ~cancellation_registration(); };
struct cancellation { cancellation_registration on_cancellation_requested(void (*)()); };
struct dispatcher { void shutdown(); };
struct dispatcher_group { void shutdown(); };
namespace internal {
struct mutex {};
struct dispatcher_lifetime { internal::mutex mutex; dispatcher* target; };
struct dispatcher_group_lifetime { internal::mutex mutex; dispatcher_group* group; };
bool is_valid_io_uring_buffer_size(size_type);
}
namespace win32 {
namespace internal { bool is_valid_buffer_size(size_type); }
promise<size_type> read(int, std::span<char>);
}
namespace iocps { promise<size_type> read(int, int, std::span<char>); }
namespace io_urings { promise<size_type> write(int, int, std::span<char>); }
void fire_and_forget(promise<int>);
}

cardio::promise<int> start();
void callback();
void owner_cases(cardio::cancellation token) {
  start(); // alert: DiscardedAsyncOwner
  (void)start(); // alert: DiscardedAsyncOwner
  token.on_cancellation_requested(callback); // alert: DiscardedAsyncOwner
  auto pending = start();
  (void)pending;
  cardio::fire_and_forget(start());
  auto registration = token.on_cancellation_requested(callback);
  (void)registration;
}

void lock_cases(cardio::internal::dispatcher_lifetime& life,
                cardio::internal::dispatcher_lifetime& other) {
  life.target->shutdown(); // alert: UnlockedDispatcher
  cardio::dispatcher* saved;
  {
    std::lock_guard<cardio::internal::mutex> guard(life.mutex);
    saved = life.target;
    if (saved) saved->shutdown();
    if (life.target) life.target->shutdown();
    other.target->shutdown(); // alert: UnlockedDispatcher
  }
  if (saved) saved->shutdown(); // alert: UnlockedDispatcher
}
void group_cases(cardio::internal::dispatcher_group_lifetime& life) {
  if (life.group) life.group->shutdown(); // alert: UnlockedDispatcher
  std::lock_guard<cardio::internal::mutex> guard(life.mutex);
  if (life.group) life.group->shutdown();
}

cardio::promise<size_type> borrowed_stack() {
  char bytes[16];
  return cardio::iocps::read(1, 2, bytes); // alert: EscapingIoBuffer
}
cardio::promise<size_type> borrowed_view() {
  std::array<char, 16> bytes;
  std::span<char> view(bytes.data(), 16);
  auto pending = cardio::io_urings::write(1, 2, view); // alert: EscapingIoBuffer
  return pending;
}
cardio::promise<size_type> borrowed_caller(std::span<char> bytes) {
  return cardio::win32::read(1, bytes);
}
cardio::promise<size_type> borrowed_static() {
  static char bytes[16];
  return cardio::win32::read(1, bytes);
}
void hold_operation() {
  char bytes[16];
  auto pending = cardio::iocps::read(1, 2, bytes);
  (void)pending;
}

void ReadFile(int, void*, unsigned, void*, void*);
void io_uring_prep_write(void*, int, const void*, unsigned, unsigned long long);
void lengths(std::span<char> buffer) {
  auto size = buffer.size();
  ReadFile(1, nullptr, static_cast<unsigned>(size), nullptr, nullptr); // alert: NarrowIoSize
  io_uring_prep_write(nullptr, 0, nullptr, size, 0); // alert: NarrowIoSize
  if (!cardio::win32::internal::is_valid_buffer_size(size)) return;
  ReadFile(1, nullptr, static_cast<unsigned>(size), nullptr, nullptr);
}
void deferred(std::span<char> buffer) {
  const auto size = buffer.size();
  auto unchecked = [size]() {
    io_uring_prep_write(nullptr, 0, nullptr, static_cast<unsigned>(size), 0); // alert: NarrowIoSize
  };
  unchecked();
  if (!cardio::internal::is_valid_io_uring_buffer_size(size)) return;
  auto checked = [size]() {
    io_uring_prep_write(nullptr, 0, nullptr, static_cast<unsigned>(size), 0);
  };
  checked();
}
void bounded(unsigned small) {
  ReadFile(1, nullptr, small, nullptr, nullptr);
  ReadFile(1, nullptr, 16, nullptr, nullptr);
}

// As in the standard library, operator-> may be inherited from a base class.
namespace std {
template <class T> struct pointer_base { T* operator->() const; };
template <class T> struct shared_ptr : pointer_base<T> {};
}
void shared_lifetime(const std::shared_ptr<cardio::internal::dispatcher_lifetime>& life) {
  auto guard = std::lock_guard<cardio::internal::mutex>(life->mutex);
  if (life->target) life->target->shutdown();
}
namespace cardio { namespace internal {
struct promise_state_base {
  dispatcher_group* group;
  std::shared_ptr<dispatcher_group_lifetime> group_lifetime;
};
void finish_promise(promise_state_base& state) {
  state.group->shutdown(); // alert: UnlockedDispatcher
  auto guard = std::lock_guard<mutex>(state.group_lifetime->mutex);
  if (state.group_lifetime->group) state.group_lifetime->group->shutdown();
}
}}
cardio::promise<size_type> rebound_view(std::span<char> caller) {
  char local[16];
  std::span<char> view(local);
  view = caller;
  return cardio::win32::read(1, view);
}
void wrong_length_guard(size_type a, size_type b) {
  if (!cardio::win32::internal::is_valid_buffer_size(a)) return;
  ReadFile(1, nullptr, static_cast<unsigned>(b), nullptr, nullptr); // alert: NarrowIoSize
}

void guarded_cleanup(cardio::internal::dispatcher_lifetime& life) {
  std::lock_guard<cardio::internal::mutex> guard(life.mutex);
  auto* saved = life.target;
  try {
    callback();
  } catch (...) {
    saved->shutdown();
  }
}

void narrowed_local(std::span<char> buffer) {
  unsigned length = static_cast<unsigned>(buffer.size());
  ReadFile(1, nullptr, length, nullptr, nullptr); // alert: NarrowIoSize
}
void checked_local(std::span<char> buffer) {
  const auto size = buffer.size();
  if (!cardio::win32::internal::is_valid_buffer_size(size)) return;
  auto length = static_cast<unsigned>(size);
  ReadFile(1, nullptr, length, nullptr, nullptr);
}
cardio::promise<size_type> replaced_operation(std::span<char> caller) {
  char local[16];
  auto pending = cardio::win32::read(1, local);
  pending = cardio::win32::read(1, caller);
  return pending;
}

namespace cardio {
template <class T> struct promise_source { promise<T> get_promise(); };
promise<int> from_fd(int, int);
promise<int> resolved(int);
promise<int> rejected(int);
}
void pending_or_observing(cardio::promise_source<int>& source, int fd) {
  (void)source.get_promise();
  (void)cardio::resolved(42);
  (void)cardio::rejected(1);
  (void)cardio::from_fd(-1, 1);
  (void)cardio::from_fd(fd, 1); // alert: DiscardedAsyncOwner
}
