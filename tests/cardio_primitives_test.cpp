// cardio - A header-only library for asynchronous processing in C++20.
// Copyright (c) Kouji Matsui (@kekyo@mi.kekyo.net)
// Under MIT.
// https://github.com/kekyo/cardio/

#include "cardio.h"
#include "test_helpers.h"

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

//-----------------------------------------------------------------------------------------------

#if CARDIO_HAS_EXCEPTIONS
template <typename T>
static void check_canceled(cardio::promise<T>& promise) {
  CHECK(promise.is_ready());

  auto caught = false;
  try {
    (void)promise.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }
  CHECK(caught);
}

static void check_canceled(cardio::promise<void>& promise) {
  CHECK(promise.is_ready());

  auto caught = false;
  try {
    promise.unsafe_result();
  } catch (const cardio::canceled_exception&) {
    caught = true;
  }
  CHECK(caught);
}
#endif

//-----------------------------------------------------------------------------------------------

static void mutex_acquires_and_releases_lock() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;

  CHECK(!locker.is_locked());
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(0));

  auto promise = locker.lock();
  CHECK(promise.is_ready());
  CHECK(locker.is_locked());

  auto handle = std::move(promise).unsafe_result();
  CHECK(handle.is_active());
  handle.release();

  CHECK(!handle.is_active());
  CHECK(!locker.is_locked());
}

static void mutex_tracks_pending_count() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;

  auto first = locker.lock();
  auto first_handle = std::move(first).unsafe_result();
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(0));

  auto second = locker.lock();
  CHECK(!second.is_ready());
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(1));

  first_handle.release();
  CHECK(second.is_ready());
  auto second_handle = std::move(second).unsafe_result();
  second_handle.release();
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(0));
}

static void mutex_grants_waiters_in_fifo_order() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;

  auto first = locker.lock();
  auto first_handle = std::move(first).unsafe_result();
  auto second = locker.lock();
  auto third = locker.lock();

  first_handle.release();
  CHECK(second.is_ready());
  CHECK(!third.is_ready());

  auto second_handle = std::move(second).unsafe_result();
  second_handle.release();
  CHECK(third.is_ready());

  auto third_handle = std::move(third).unsafe_result();
  third_handle.release();
  CHECK(!locker.is_locked());
}

static void mutex_raii_release_unlocks_on_scope_exit() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;

  {
    auto promise = locker.lock();
    auto handle = std::move(promise).unsafe_result();
    CHECK(handle.is_active());
    CHECK(locker.is_locked());
  }

  CHECK(!locker.is_locked());
}

static void mutex_release_is_idempotent() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;

  auto promise = locker.lock();
  auto handle = std::move(promise).unsafe_result();
  handle.release();
  handle.release();
  handle.release();

  CHECK(!handle.is_active());
  CHECK(!locker.is_locked());

  auto next = locker.lock();
  CHECK(next.is_ready());
  auto next_handle = std::move(next).unsafe_result();
  next_handle.release();
}

static void mutex_independent_instances_do_not_block_each_other() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex first_locker;
  cardio::primitives::mutex second_locker;

  auto first = first_locker.lock();
  auto second = second_locker.lock();
  CHECK(first.is_ready());
  CHECK(second.is_ready());

  auto first_handle = std::move(first).unsafe_result();
  auto second_handle = std::move(second).unsafe_result();
  CHECK(first_locker.is_locked());
  CHECK(second_locker.is_locked());

  first_handle.release();
  CHECK(!first_locker.is_locked());
  CHECK(second_locker.is_locked());

  second_handle.release();
  CHECK(!second_locker.is_locked());
}

static void mutex_nested_independent_locks_release_in_reverse_order() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex first_locker;
  cardio::primitives::mutex second_locker;

  auto first = first_locker.lock();
  auto first_handle = std::move(first).unsafe_result();
  auto second = second_locker.lock();
  auto second_handle = std::move(second).unsafe_result();

  CHECK(first_locker.is_locked());
  CHECK(second_locker.is_locked());

  second_handle.release();
  CHECK(first_locker.is_locked());
  CHECK(!second_locker.is_locked());

  first_handle.release();
  CHECK(!first_locker.is_locked());
}

static void mutex_handles_rapid_queue_cycles() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;
  constexpr auto iterations = std::size_t{100};

  auto first = locker.lock();
  auto current = std::move(first).unsafe_result();

  auto waiters =
      std::vector<cardio::promise<cardio::primitives::lock_handle>>{};
  waiters.reserve(iterations);
  for (auto index = std::size_t{}; index < iterations; ++index) {
    waiters.push_back(locker.lock());
  }

  for (auto& waiter : waiters) {
    CHECK(!waiter.is_ready());
  }

  for (auto& waiter : waiters) {
    current.release();
    CHECK(waiter.is_ready());
    current = std::move(waiter).unsafe_result();
  }

  current.release();
  CHECK(!locker.is_locked());
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(0));
}

#if CARDIO_HAS_EXCEPTIONS
static void mutex_wait_can_be_canceled() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;
  cardio::cancellation_source source;

  auto first = locker.lock();
  auto first_handle = std::move(first).unsafe_result();
  auto pending = locker.lock(source.get_cancellation());
  CHECK(!pending.is_ready());
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(1));

  CHECK(source.cancel());
  dispatcher.park();

  check_canceled(pending);
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(0));

  first_handle.release();
  CHECK(!locker.is_locked());
}

static void mutex_rejects_immediately_when_canceled_before_lock() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;
  cardio::cancellation_source source;

  CHECK(source.cancel());
  auto pending = locker.lock(source.get_cancellation());

  check_canceled(pending);
  CHECK(!locker.is_locked());
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(0));
}

static void mutex_handles_multiple_simultaneous_cancellations() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;
  auto sources = std::vector<cardio::cancellation_source>(10);

  auto first = locker.lock();
  auto first_handle = std::move(first).unsafe_result();
  auto waiters =
      std::vector<cardio::promise<cardio::primitives::lock_handle>>{};
  waiters.reserve(sources.size());
  for (auto& source : sources) {
    waiters.push_back(locker.lock(source.get_cancellation()));
  }

  CHECK_EQ(locker.pending_count(), sources.size());
  for (auto& source : sources) {
    CHECK(source.cancel());
  }

  dispatcher.park();

  for (auto& waiter : waiters) {
    check_canceled(waiter);
  }
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(0));

  first_handle.release();
  CHECK(!locker.is_locked());
}

static void mutex_keeps_queue_integrity_when_canceled_items_are_removed() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;
  cardio::cancellation_source first_source;
  cardio::cancellation_source second_source;

  auto first = locker.lock();
  auto first_handle = std::move(first).unsafe_result();
  auto canceled_first = locker.lock(first_source.get_cancellation());
  auto canceled_second = locker.lock(second_source.get_cancellation());

  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(2));
  CHECK(first_source.cancel());
  CHECK(second_source.cancel());
  dispatcher.park();

  check_canceled(canceled_first);
  check_canceled(canceled_second);
  CHECK_EQ(locker.pending_count(), static_cast<std::size_t>(0));

  auto surviving = locker.lock();
  CHECK(!surviving.is_ready());
  first_handle.release();
  CHECK(surviving.is_ready());
  auto surviving_handle = std::move(surviving).unsafe_result();
  surviving_handle.release();
  CHECK(!locker.is_locked());
}

static void mutex_release_before_cancellation_dispatch_completes_once() {
  test_dispatcher_host dispatcher;
  cardio::primitives::mutex locker;
  cardio::cancellation_source source;

  auto first = locker.lock();
  auto first_handle = std::move(first).unsafe_result();
  auto pending = locker.lock(source.get_cancellation());

  CHECK(source.cancel());
  first_handle.release();
  dispatcher.park();

  CHECK(pending.is_ready());
  auto pending_handle = std::move(pending).unsafe_result();
  pending_handle.release();
  CHECK(!locker.is_locked());
}
#endif

//-----------------------------------------------------------------------------------------------

static void semaphore_allows_acquisitions_up_to_count() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(3);

  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(3));
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(0));

  auto first = semaphore.acquire();
  auto second = semaphore.acquire();
  auto third = semaphore.acquire();
  CHECK(first.is_ready());
  CHECK(second.is_ready());
  CHECK(third.is_ready());
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(0));

  auto first_handle = std::move(first).unsafe_result();
  auto second_handle = std::move(second).unsafe_result();
  auto third_handle = std::move(third).unsafe_result();
  first_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(1));
  second_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(2));
  third_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(3));
}

static void semaphore_rejects_zero_count() {
  auto caught = false;
  try {
    cardio::primitives::semaphore semaphore(0);
    (void)semaphore;
  } catch (const std::invalid_argument& exception) {
    caught = std::string(exception.what()).find("semaphore count") !=
             std::string::npos;
  }

  CHECK(caught);
}

static void semaphore_release_is_idempotent() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(2);

  auto promise = semaphore.acquire();
  auto handle = std::move(promise).unsafe_result();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(1));

  handle.release();
  handle.release();
  handle.release();

  CHECK(!handle.is_active());
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(2));
}

static void semaphore_tracks_pending_count() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(1);

  auto first = semaphore.acquire();
  auto first_handle = std::move(first).unsafe_result();
  auto second = semaphore.acquire();
  auto third = semaphore.acquire();

  CHECK(!second.is_ready());
  CHECK(!third.is_ready());
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(2));

  first_handle.release();
  CHECK(second.is_ready());
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(1));

  auto second_handle = std::move(second).unsafe_result();
  second_handle.release();
  CHECK(third.is_ready());

  auto third_handle = std::move(third).unsafe_result();
  third_handle.release();
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(0));
}

static void semaphore_blocks_when_all_resources_are_acquired() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(2);

  auto first = semaphore.acquire();
  auto second = semaphore.acquire();
  auto third = semaphore.acquire();
  CHECK(!third.is_ready());

  auto first_handle = std::move(first).unsafe_result();
  auto second_handle = std::move(second).unsafe_result();
  first_handle.release();
  CHECK(third.is_ready());

  auto third_handle = std::move(third).unsafe_result();
  second_handle.release();
  third_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(2));
}

static void semaphore_preserves_fifo_order() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(1);

  auto first = semaphore.acquire();
  auto first_handle = std::move(first).unsafe_result();
  auto second = semaphore.acquire();
  auto third = semaphore.acquire();

  first_handle.release();
  CHECK(second.is_ready());
  CHECK(!third.is_ready());

  auto second_handle = std::move(second).unsafe_result();
  second_handle.release();
  CHECK(third.is_ready());

  auto third_handle = std::move(third).unsafe_result();
  third_handle.release();
}

static void semaphore_independent_instances_do_not_block_each_other() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore first_semaphore(1);
  cardio::primitives::semaphore second_semaphore(1);

  auto first = first_semaphore.acquire();
  auto second = second_semaphore.acquire();
  CHECK(first.is_ready());
  CHECK(second.is_ready());

  auto first_handle = std::move(first).unsafe_result();
  auto second_handle = std::move(second).unsafe_result();
  CHECK_EQ(first_semaphore.available_count(), static_cast<std::size_t>(0));
  CHECK_EQ(second_semaphore.available_count(), static_cast<std::size_t>(0));

  first_handle.release();
  CHECK_EQ(first_semaphore.available_count(), static_cast<std::size_t>(1));
  CHECK_EQ(second_semaphore.available_count(), static_cast<std::size_t>(0));

  second_handle.release();
  CHECK_EQ(second_semaphore.available_count(), static_cast<std::size_t>(1));
}

static void semaphore_nested_acquisitions_release_independently() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore first_semaphore(2);
  cardio::primitives::semaphore second_semaphore(2);

  auto first = first_semaphore.acquire();
  auto first_handle = std::move(first).unsafe_result();
  auto second = second_semaphore.acquire();
  auto second_handle = std::move(second).unsafe_result();

  CHECK_EQ(first_semaphore.available_count(), static_cast<std::size_t>(1));
  CHECK_EQ(second_semaphore.available_count(), static_cast<std::size_t>(1));

  second_handle.release();
  CHECK_EQ(first_semaphore.available_count(), static_cast<std::size_t>(1));
  CHECK_EQ(second_semaphore.available_count(), static_cast<std::size_t>(2));

  first_handle.release();
  CHECK_EQ(first_semaphore.available_count(), static_cast<std::size_t>(2));
}

static void semaphore_handles_rapid_acquire_release_cycles() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(5);
  constexpr auto iterations = std::size_t{100};

  auto handles = std::vector<cardio::primitives::lock_handle>{};
  handles.reserve(5);
  for (auto index = std::size_t{}; index < 5; ++index) {
    auto promise = semaphore.acquire();
    handles.push_back(std::move(promise).unsafe_result());
  }

  auto waiters =
      std::vector<cardio::promise<cardio::primitives::lock_handle>>{};
  waiters.reserve(iterations);
  for (auto index = std::size_t{}; index < iterations; ++index) {
    waiters.push_back(semaphore.acquire());
  }

  for (auto& waiter : waiters) {
    CHECK(!waiter.is_ready());
  }

  for (auto index = std::size_t{}; index < waiters.size(); ++index) {
    handles[index % handles.size()].release();
    CHECK(waiters[index].is_ready());
    handles[index % handles.size()] = std::move(waiters[index]).unsafe_result();
  }

  for (auto& handle : handles) {
    handle.release();
  }

  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(5));
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(0));
}

#if CARDIO_HAS_EXCEPTIONS
static void semaphore_wait_can_be_canceled() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(1);
  cardio::cancellation_source source;

  auto first = semaphore.acquire();
  auto first_handle = std::move(first).unsafe_result();
  auto pending = semaphore.acquire(source.get_cancellation());
  CHECK(!pending.is_ready());
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(1));

  CHECK(source.cancel());
  dispatcher.park();

  check_canceled(pending);
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(0));

  first_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(1));
}

static void semaphore_rejects_immediately_when_canceled_before_acquire() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(2);
  cardio::cancellation_source source;

  CHECK(source.cancel());
  auto pending = semaphore.acquire(source.get_cancellation());

  check_canceled(pending);
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(2));
}

static void semaphore_handles_multiple_simultaneous_cancellations() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(1);
  auto sources = std::vector<cardio::cancellation_source>(5);

  auto first = semaphore.acquire();
  auto first_handle = std::move(first).unsafe_result();
  auto waiters =
      std::vector<cardio::promise<cardio::primitives::lock_handle>>{};
  waiters.reserve(sources.size());
  for (auto& source : sources) {
    waiters.push_back(semaphore.acquire(source.get_cancellation()));
  }

  CHECK_EQ(semaphore.pending_count(), sources.size());
  for (auto& source : sources) {
    CHECK(source.cancel());
  }

  dispatcher.park();

  for (auto& waiter : waiters) {
    check_canceled(waiter);
  }
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(0));

  first_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(1));
}

static void semaphore_keeps_queue_integrity_when_canceled_items_are_removed() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(1);
  cardio::cancellation_source first_source;
  cardio::cancellation_source second_source;

  auto first = semaphore.acquire();
  auto first_handle = std::move(first).unsafe_result();
  auto canceled_first = semaphore.acquire(first_source.get_cancellation());
  auto canceled_second = semaphore.acquire(second_source.get_cancellation());

  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(2));
  CHECK(first_source.cancel());
  CHECK(second_source.cancel());
  dispatcher.park();

  check_canceled(canceled_first);
  check_canceled(canceled_second);
  CHECK_EQ(semaphore.pending_count(), static_cast<std::size_t>(0));

  auto surviving = semaphore.acquire();
  CHECK(!surviving.is_ready());
  first_handle.release();
  CHECK(surviving.is_ready());
  auto surviving_handle = std::move(surviving).unsafe_result();
  surviving_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(1));
}

static void semaphore_release_before_cancellation_dispatch_completes_once() {
  test_dispatcher_host dispatcher;
  cardio::primitives::semaphore semaphore(1);
  cardio::cancellation_source source;

  auto first = semaphore.acquire();
  auto first_handle = std::move(first).unsafe_result();
  auto pending = semaphore.acquire(source.get_cancellation());

  CHECK(source.cancel());
  first_handle.release();
  dispatcher.park();

  CHECK(pending.is_ready());
  auto pending_handle = std::move(pending).unsafe_result();
  pending_handle.release();
  CHECK_EQ(semaphore.available_count(), static_cast<std::size_t>(1));
}
#endif

//-----------------------------------------------------------------------------------------------

static void reader_writer_lock_allows_multiple_concurrent_readers() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));
  CHECK(!lock.has_writer());

  auto first = lock.read_lock();
  auto second = lock.read_lock();
  auto third = lock.read_lock();
  CHECK(first.is_ready());
  CHECK(second.is_ready());
  CHECK(third.is_ready());
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(3));
  CHECK(!lock.has_writer());

  auto first_handle = std::move(first).unsafe_result();
  auto second_handle = std::move(second).unsafe_result();
  auto third_handle = std::move(third).unsafe_result();
  first_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(2));
  second_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(1));
  third_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));
}

static void reader_writer_lock_allows_only_one_writer() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto first = lock.write_lock();
  CHECK(first.is_ready());
  auto first_handle = std::move(first).unsafe_result();
  CHECK(lock.has_writer());

  auto second = lock.write_lock();
  CHECK(!second.is_ready());
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(1));

  first_handle.release();
  CHECK(second.is_ready());
  auto second_handle = std::move(second).unsafe_result();
  second_handle.release();

  CHECK(!lock.has_writer());
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));
}

static void reader_writer_lock_release_is_idempotent() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto read = lock.read_lock();
  auto read_handle = std::move(read).unsafe_result();
  read_handle.release();
  read_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));

  auto write = lock.write_lock();
  auto write_handle = std::move(write).unsafe_result();
  write_handle.release();
  write_handle.release();
  CHECK(!lock.has_writer());
}

static void reader_writer_lock_tracks_pending_counts() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto writer = lock.write_lock();
  auto writer_handle = std::move(writer).unsafe_result();
  auto first_reader = lock.read_lock();
  auto second_reader = lock.read_lock();
  auto next_writer = lock.write_lock();

  CHECK(!first_reader.is_ready());
  CHECK(!second_reader.is_ready());
  CHECK(!next_writer.is_ready());
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(2));
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(1));

  writer_handle.release();

  CHECK(!first_reader.is_ready());
  CHECK(!second_reader.is_ready());
  CHECK(next_writer.is_ready());

  auto next_writer_handle = std::move(next_writer).unsafe_result();
  next_writer_handle.release();

  CHECK(first_reader.is_ready());
  CHECK(second_reader.is_ready());

  auto first_reader_handle = std::move(first_reader).unsafe_result();
  auto second_reader_handle = std::move(second_reader).unsafe_result();
  first_reader_handle.release();
  second_reader_handle.release();

  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(0));
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(0));
}

static void reader_writer_lock_blocks_readers_when_writer_is_active() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto writer = lock.write_lock();
  auto writer_handle = std::move(writer).unsafe_result();
  auto reader = lock.read_lock();
  CHECK(!reader.is_ready());
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(1));

  writer_handle.release();
  CHECK(reader.is_ready());
  auto reader_handle = std::move(reader).unsafe_result();
  reader_handle.release();
}

static void reader_writer_lock_blocks_writer_when_readers_are_active() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto first_reader = lock.read_lock();
  auto second_reader = lock.read_lock();
  auto first_reader_handle = std::move(first_reader).unsafe_result();
  auto second_reader_handle = std::move(second_reader).unsafe_result();
  auto writer = lock.write_lock();

  CHECK(!writer.is_ready());
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(1));

  first_reader_handle.release();
  CHECK(!writer.is_ready());
  second_reader_handle.release();
  CHECK(writer.is_ready());

  auto writer_handle = std::move(writer).unsafe_result();
  writer_handle.release();
}

static void reader_writer_lock_uses_write_preferring_policy_by_default() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto first_reader = lock.read_lock();
  auto first_reader_handle = std::move(first_reader).unsafe_result();
  auto writer = lock.write_lock();
  auto second_reader = lock.read_lock();

  CHECK(!writer.is_ready());
  CHECK(!second_reader.is_ready());
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(1));
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(1));

  first_reader_handle.release();
  CHECK(writer.is_ready());
  CHECK(!second_reader.is_ready());

  auto writer_handle = std::move(writer).unsafe_result();
  writer_handle.release();
  CHECK(second_reader.is_ready());

  auto second_reader_handle = std::move(second_reader).unsafe_result();
  second_reader_handle.release();
}

static void reader_writer_lock_processes_waiting_readers_together_after_writer() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto writer = lock.write_lock();
  auto writer_handle = std::move(writer).unsafe_result();
  auto first_reader = lock.read_lock();
  auto second_reader = lock.read_lock();
  auto third_reader = lock.read_lock();

  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(3));
  writer_handle.release();

  CHECK(first_reader.is_ready());
  CHECK(second_reader.is_ready());
  CHECK(third_reader.is_ready());
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(3));

  auto first_reader_handle = std::move(first_reader).unsafe_result();
  auto second_reader_handle = std::move(second_reader).unsafe_result();
  auto third_reader_handle = std::move(third_reader).unsafe_result();
  first_reader_handle.release();
  second_reader_handle.release();
  third_reader_handle.release();
}

static void reader_writer_lock_can_prefer_readers() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock(
      cardio::primitives::reader_writer_lock_policy::read_preferring);

  auto first_reader = lock.read_lock();
  auto first_reader_handle = std::move(first_reader).unsafe_result();
  auto writer = lock.write_lock();
  auto second_reader = lock.read_lock();

  CHECK(!writer.is_ready());
  CHECK(second_reader.is_ready());
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(2));

  auto second_reader_handle = std::move(second_reader).unsafe_result();
  first_reader_handle.release();
  CHECK(!writer.is_ready());
  second_reader_handle.release();
  CHECK(writer.is_ready());

  auto writer_handle = std::move(writer).unsafe_result();
  writer_handle.release();
}

static void reader_writer_lock_prioritizes_readers_with_read_preferring_policy() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock(
      cardio::primitives::reader_writer_lock_policy::read_preferring);

  auto initial_writer = lock.write_lock();
  auto initial_writer_handle = std::move(initial_writer).unsafe_result();
  auto writer = lock.write_lock();
  auto first_reader = lock.read_lock();
  auto second_reader = lock.read_lock();
  auto third_reader = lock.read_lock();

  CHECK(!writer.is_ready());
  CHECK(!first_reader.is_ready());
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(1));
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(3));

  initial_writer_handle.release();

  CHECK(first_reader.is_ready());
  CHECK(second_reader.is_ready());
  CHECK(third_reader.is_ready());
  CHECK(!writer.is_ready());

  auto first_reader_handle = std::move(first_reader).unsafe_result();
  auto second_reader_handle = std::move(second_reader).unsafe_result();
  auto third_reader_handle = std::move(third_reader).unsafe_result();
  first_reader_handle.release();
  second_reader_handle.release();
  third_reader_handle.release();

  CHECK(writer.is_ready());
  auto writer_handle = std::move(writer).unsafe_result();
  writer_handle.release();
}

static void reader_writer_lock_handles_nested_independent_locks() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock first_lock;
  cardio::primitives::reader_writer_lock second_lock;

  auto read = first_lock.read_lock();
  auto read_handle = std::move(read).unsafe_result();
  auto write = second_lock.write_lock();
  auto write_handle = std::move(write).unsafe_result();

  CHECK_EQ(first_lock.current_readers(), static_cast<std::size_t>(1));
  CHECK(second_lock.has_writer());

  write_handle.release();
  CHECK(!second_lock.has_writer());
  CHECK_EQ(first_lock.current_readers(), static_cast<std::size_t>(1));

  read_handle.release();
  CHECK_EQ(first_lock.current_readers(), static_cast<std::size_t>(0));
}

static void reader_writer_lock_allows_multiple_read_locks_from_same_context() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto first = lock.read_lock();
  auto second = lock.read_lock();
  auto third = lock.read_lock();
  auto first_handle = std::move(first).unsafe_result();
  auto second_handle = std::move(second).unsafe_result();
  auto third_handle = std::move(third).unsafe_result();

  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(3));
  first_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(2));
  second_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(1));
  third_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));
}

static void reader_writer_lock_is_not_reentrant_for_writers() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;

  auto first = lock.write_lock();
  auto first_handle = std::move(first).unsafe_result();
  auto second = lock.write_lock();

  CHECK(!second.is_ready());
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(1));

  first_handle.release();
  CHECK(second.is_ready());
  auto second_handle = std::move(second).unsafe_result();
  second_handle.release();
}

static void reader_writer_lock_handles_rapid_mixed_cycles() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;
  constexpr auto iterations = std::size_t{50};

  auto initial_writer = lock.write_lock();
  auto initial_writer_handle = std::move(initial_writer).unsafe_result();

  auto readers =
      std::vector<cardio::promise<cardio::primitives::lock_handle>>{};
  auto writers =
      std::vector<cardio::promise<cardio::primitives::lock_handle>>{};
  for (auto index = std::size_t{}; index < iterations; ++index) {
    if (index % 3 == 0) {
      writers.push_back(lock.write_lock());
    } else {
      readers.push_back(lock.read_lock());
    }
  }

  initial_writer_handle.release();

  auto progress = std::size_t{};
  while (progress < iterations) {
    auto changed = false;
    for (auto& reader : readers) {
      if (reader.is_ready()) {
        auto handle = std::move(reader).unsafe_result();
        handle.release();
        ++progress;
        changed = true;
      }
    }
    for (auto& writer : writers) {
      if (writer.is_ready()) {
        auto handle = std::move(writer).unsafe_result();
        handle.release();
        ++progress;
        changed = true;
      }
    }
    CHECK(changed);
  }

  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));
  CHECK(!lock.has_writer());
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(0));
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(0));
}

#if CARDIO_HAS_EXCEPTIONS
static void reader_writer_lock_read_wait_can_be_canceled() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;
  cardio::cancellation_source source;

  auto writer = lock.write_lock();
  auto writer_handle = std::move(writer).unsafe_result();
  auto reader = lock.read_lock(source.get_cancellation());
  CHECK(!reader.is_ready());
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(1));

  CHECK(source.cancel());
  dispatcher.park();

  check_canceled(reader);
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(0));

  writer_handle.release();
}

static void reader_writer_lock_write_wait_can_be_canceled() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;
  cardio::cancellation_source source;

  auto reader = lock.read_lock();
  auto reader_handle = std::move(reader).unsafe_result();
  auto writer = lock.write_lock(source.get_cancellation());
  CHECK(!writer.is_ready());
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(1));

  CHECK(source.cancel());
  dispatcher.park();

  check_canceled(writer);
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(0));

  reader_handle.release();
}

static void reader_writer_lock_rejects_immediately_when_canceled_before_lock() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;
  cardio::cancellation_source source;

  CHECK(source.cancel());
  auto reader = lock.read_lock(source.get_cancellation());
  auto writer = lock.write_lock(source.get_cancellation());

  check_canceled(reader);
  check_canceled(writer);
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));
  CHECK(!lock.has_writer());
}

static void reader_writer_lock_handles_multiple_simultaneous_cancellations() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;
  auto sources = std::vector<cardio::cancellation_source>(5);

  auto writer = lock.write_lock();
  auto writer_handle = std::move(writer).unsafe_result();
  auto readers =
      std::vector<cardio::promise<cardio::primitives::lock_handle>>{};
  readers.reserve(sources.size());
  for (auto& source : sources) {
    readers.push_back(lock.read_lock(source.get_cancellation()));
  }

  CHECK_EQ(lock.pending_readers_count(), sources.size());
  for (auto& source : sources) {
    CHECK(source.cancel());
  }
  dispatcher.park();

  for (auto& reader : readers) {
    check_canceled(reader);
  }
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(0));

  writer_handle.release();
}

static void reader_writer_lock_keeps_queue_integrity_when_waiters_cancel() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;
  cardio::cancellation_source first_reader_source;
  cardio::cancellation_source writer_source;

  auto writer = lock.write_lock();
  auto writer_handle = std::move(writer).unsafe_result();
  auto canceled_reader = lock.read_lock(first_reader_source.get_cancellation());
  auto canceled_writer = lock.write_lock(writer_source.get_cancellation());

  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(1));
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(1));
  CHECK(first_reader_source.cancel());
  CHECK(writer_source.cancel());
  dispatcher.park();

  check_canceled(canceled_reader);
  check_canceled(canceled_writer);
  CHECK_EQ(lock.pending_readers_count(), static_cast<std::size_t>(0));
  CHECK_EQ(lock.pending_writers_count(), static_cast<std::size_t>(0));

  auto surviving_reader = lock.read_lock();
  auto second_surviving_reader = lock.read_lock();
  CHECK(!surviving_reader.is_ready());
  CHECK(!second_surviving_reader.is_ready());

  writer_handle.release();
  CHECK(surviving_reader.is_ready());
  CHECK(second_surviving_reader.is_ready());

  auto surviving_reader_handle = std::move(surviving_reader).unsafe_result();
  auto second_surviving_reader_handle =
      std::move(second_surviving_reader).unsafe_result();
  surviving_reader_handle.release();
  second_surviving_reader_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));
}

static void reader_writer_lock_upgrade_attempt_can_be_canceled() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;
  cardio::cancellation_source source;

  auto reader = lock.read_lock();
  auto reader_handle = std::move(reader).unsafe_result();
  auto writer = lock.write_lock(source.get_cancellation());
  CHECK(!writer.is_ready());

  CHECK(source.cancel());
  dispatcher.park();

  check_canceled(writer);
  reader_handle.release();
  CHECK_EQ(lock.current_readers(), static_cast<std::size_t>(0));
}

static void reader_writer_lock_read_while_holding_writer_can_be_canceled() {
  test_dispatcher_host dispatcher;
  cardio::primitives::reader_writer_lock lock;
  cardio::cancellation_source source;

  auto writer = lock.write_lock();
  auto writer_handle = std::move(writer).unsafe_result();
  auto reader = lock.read_lock(source.get_cancellation());
  CHECK(!reader.is_ready());

  CHECK(source.cancel());
  dispatcher.park();

  check_canceled(reader);
  writer_handle.release();
  CHECK(!lock.has_writer());
}
#endif

//-----------------------------------------------------------------------------------------------

static void conditional_waits_until_triggered() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;

  auto waiter = condition.wait();
  CHECK(!waiter.is_ready());
  condition.trigger();
  CHECK(waiter.is_ready());
}

static void conditional_triggers_only_one_waiter() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;

  auto first = condition.wait();
  auto second = condition.wait();
  CHECK(!first.is_ready());
  CHECK(!second.is_ready());

  condition.trigger();
  CHECK(first.is_ready());
  CHECK(!second.is_ready());

  condition.trigger();
  CHECK(second.is_ready());
}

static void conditional_drops_trigger_when_no_waiter_exists() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;

  condition.trigger();
  auto waiter = condition.wait();
  CHECK(!waiter.is_ready());

  condition.trigger();
  CHECK(waiter.is_ready());
}

static void conditional_handles_multiple_trigger_calls_with_different_waiters() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;

  auto first = condition.wait();
  condition.trigger();
  CHECK(first.is_ready());

  auto second = condition.wait();
  CHECK(!second.is_ready());
  condition.trigger();
  CHECK(second.is_ready());
}

static void conditional_handles_sequential_trigger_wait_cycles() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;

  for (auto index = 0; index < 10; ++index) {
    auto waiter = condition.wait();
    CHECK(!waiter.is_ready());
    condition.trigger();
    CHECK(waiter.is_ready());
  }
}

static void conditional_handles_interleaved_waiters() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;

  auto first = condition.wait();
  auto second = condition.wait();
  condition.trigger();
  CHECK(first.is_ready());
  CHECK(!second.is_ready());

  auto third = condition.wait();
  condition.trigger();
  CHECK(second.is_ready());
  CHECK(!third.is_ready());

  condition.trigger();
  CHECK(third.is_ready());
}

#if CARDIO_HAS_EXCEPTIONS
static void conditional_wait_can_be_canceled() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;
  cardio::cancellation_source source;

  auto pending = condition.wait(source.get_cancellation());
  CHECK(!pending.is_ready());

  CHECK(source.cancel());
  dispatcher.park();

  check_canceled(pending);
}

static void conditional_rejects_immediately_when_canceled_before_wait() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;
  cardio::cancellation_source source;

  CHECK(source.cancel());
  auto pending = condition.wait(source.get_cancellation());

  check_canceled(pending);
}

static void conditional_handles_multiple_simultaneous_cancellations() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;
  auto sources = std::vector<cardio::cancellation_source>(5);

  auto waiters = std::vector<cardio::promise<void>>{};
  waiters.reserve(sources.size());
  for (auto& source : sources) {
    waiters.push_back(condition.wait(source.get_cancellation()));
  }

  for (auto& source : sources) {
    CHECK(source.cancel());
  }
  dispatcher.park();

  for (auto& waiter : waiters) {
    check_canceled(waiter);
  }
}

static void conditional_keeps_queue_integrity_when_waiters_cancel() {
  test_dispatcher_host dispatcher;
  cardio::primitives::conditional condition;
  cardio::cancellation_source first_source;
  cardio::cancellation_source second_source;

  auto canceled_first = condition.wait(first_source.get_cancellation());
  auto canceled_second = condition.wait(second_source.get_cancellation());

  CHECK(first_source.cancel());
  CHECK(second_source.cancel());
  dispatcher.park();

  check_canceled(canceled_first);
  check_canceled(canceled_second);

  auto surviving_first = condition.wait();
  auto surviving_second = condition.wait();
  CHECK(!surviving_first.is_ready());
  CHECK(!surviving_second.is_ready());

  condition.trigger();
  CHECK(surviving_first.is_ready());
  CHECK(!surviving_second.is_ready());
  condition.trigger();
  CHECK(surviving_second.is_ready());
}
#endif

//-----------------------------------------------------------------------------------------------

static void manually_conditional_waits_until_raised() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;

  auto waiter = condition.wait();
  CHECK(!waiter.is_ready());
  condition.raise();
  CHECK(waiter.is_ready());
}

static void manually_conditional_returns_immediately_when_already_raised() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;

  condition.raise();
  auto waiter = condition.wait();
  CHECK(waiter.is_ready());
}

static void manually_conditional_initial_state_can_be_raised() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition(true);

  auto waiter = condition.wait();
  CHECK(waiter.is_ready());
}

static void manually_conditional_trigger_releases_one_waiter() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;

  auto first = condition.wait();
  auto second = condition.wait();
  condition.trigger();
  CHECK(first.is_ready());
  CHECK(!second.is_ready());

  condition.trigger();
  CHECK(second.is_ready());
}

static void manually_conditional_raise_releases_all_waiters() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;

  auto waiters = std::vector<cardio::promise<void>>{};
  for (auto index = 0; index < 5; ++index) {
    waiters.push_back(condition.wait());
  }
  for (auto& waiter : waiters) {
    CHECK(!waiter.is_ready());
  }

  condition.raise();

  for (auto& waiter : waiters) {
    CHECK(waiter.is_ready());
  }
}

static void manually_conditional_drop_blocks_after_raise() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;

  condition.raise();
  auto first = condition.wait();
  CHECK(first.is_ready());

  condition.drop();
  auto second = condition.wait();
  CHECK(!second.is_ready());

  condition.raise();
  CHECK(second.is_ready());
}

static void manually_conditional_multiple_raise_calls_are_idempotent() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;

  condition.raise();
  condition.raise();
  condition.raise();

  auto waiter = condition.wait();
  CHECK(waiter.is_ready());
}

static void manually_conditional_multiple_drop_calls_keep_condition_dropped() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;

  condition.raise();
  condition.drop();
  condition.drop();
  condition.drop();

  auto waiter = condition.wait();
  CHECK(!waiter.is_ready());
  condition.raise();
  CHECK(waiter.is_ready());
}

static void manually_conditional_handles_rapid_raise_drop_cycles() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;

  for (auto index = 0; index < 10; ++index) {
    condition.raise();
    auto immediate = condition.wait();
    CHECK(immediate.is_ready());
    condition.drop();
    auto waiter = condition.wait();
    CHECK(!waiter.is_ready());
    condition.raise();
    CHECK(waiter.is_ready());
    condition.drop();
  }
}

#if CARDIO_HAS_EXCEPTIONS
static void manually_conditional_wait_can_be_canceled() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;
  cardio::cancellation_source source;

  auto waiter = condition.wait(source.get_cancellation());
  CHECK(!waiter.is_ready());

  CHECK(source.cancel());
  dispatcher.park();

  check_canceled(waiter);
}

static void manually_conditional_raised_state_ignores_cancellation_wait() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition(true);
  cardio::cancellation_source source;

  CHECK(source.cancel());
  auto waiter = condition.wait(source.get_cancellation());
  CHECK(waiter.is_ready());
  CHECK(waiter.try_result());
}

static void manually_conditional_rejects_when_canceled_before_dropped_wait() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;
  cardio::cancellation_source source;

  CHECK(source.cancel());
  auto waiter = condition.wait(source.get_cancellation());

  check_canceled(waiter);
}

static void manually_conditional_handles_abort_during_concurrent_waits() {
  test_dispatcher_host dispatcher;
  cardio::primitives::manually_conditional condition;
  cardio::cancellation_source source;

  auto cancelable_waiters = std::vector<cardio::promise<void>>{};
  for (auto index = 0; index < 3; ++index) {
    cancelable_waiters.push_back(condition.wait(source.get_cancellation()));
  }

  CHECK(source.cancel());
  dispatcher.park();

  for (auto& waiter : cancelable_waiters) {
    check_canceled(waiter);
  }

  auto regular_waiters = std::vector<cardio::promise<void>>{};
  for (auto index = 0; index < 3; ++index) {
    regular_waiters.push_back(condition.wait());
  }
  for (auto& waiter : regular_waiters) {
    CHECK(!waiter.is_ready());
  }

  condition.raise();
  for (auto& waiter : regular_waiters) {
    CHECK(waiter.is_ready());
  }
}
#endif

//-----------------------------------------------------------------------------------------------

int main() {
  mutex_acquires_and_releases_lock();
  mutex_tracks_pending_count();
  mutex_grants_waiters_in_fifo_order();
  mutex_raii_release_unlocks_on_scope_exit();
  mutex_release_is_idempotent();
  mutex_independent_instances_do_not_block_each_other();
  mutex_nested_independent_locks_release_in_reverse_order();
  mutex_handles_rapid_queue_cycles();
#if CARDIO_HAS_EXCEPTIONS
  mutex_wait_can_be_canceled();
  mutex_rejects_immediately_when_canceled_before_lock();
  mutex_handles_multiple_simultaneous_cancellations();
  mutex_keeps_queue_integrity_when_canceled_items_are_removed();
  mutex_release_before_cancellation_dispatch_completes_once();
#endif

  semaphore_allows_acquisitions_up_to_count();
  semaphore_rejects_zero_count();
  semaphore_release_is_idempotent();
  semaphore_tracks_pending_count();
  semaphore_blocks_when_all_resources_are_acquired();
  semaphore_preserves_fifo_order();
  semaphore_independent_instances_do_not_block_each_other();
  semaphore_nested_acquisitions_release_independently();
  semaphore_handles_rapid_acquire_release_cycles();
#if CARDIO_HAS_EXCEPTIONS
  semaphore_wait_can_be_canceled();
  semaphore_rejects_immediately_when_canceled_before_acquire();
  semaphore_handles_multiple_simultaneous_cancellations();
  semaphore_keeps_queue_integrity_when_canceled_items_are_removed();
  semaphore_release_before_cancellation_dispatch_completes_once();
#endif

  reader_writer_lock_allows_multiple_concurrent_readers();
  reader_writer_lock_allows_only_one_writer();
  reader_writer_lock_release_is_idempotent();
  reader_writer_lock_tracks_pending_counts();
  reader_writer_lock_blocks_readers_when_writer_is_active();
  reader_writer_lock_blocks_writer_when_readers_are_active();
  reader_writer_lock_uses_write_preferring_policy_by_default();
  reader_writer_lock_processes_waiting_readers_together_after_writer();
  reader_writer_lock_can_prefer_readers();
  reader_writer_lock_prioritizes_readers_with_read_preferring_policy();
  reader_writer_lock_handles_nested_independent_locks();
  reader_writer_lock_allows_multiple_read_locks_from_same_context();
  reader_writer_lock_is_not_reentrant_for_writers();
  reader_writer_lock_handles_rapid_mixed_cycles();
#if CARDIO_HAS_EXCEPTIONS
  reader_writer_lock_read_wait_can_be_canceled();
  reader_writer_lock_write_wait_can_be_canceled();
  reader_writer_lock_rejects_immediately_when_canceled_before_lock();
  reader_writer_lock_handles_multiple_simultaneous_cancellations();
  reader_writer_lock_keeps_queue_integrity_when_waiters_cancel();
  reader_writer_lock_upgrade_attempt_can_be_canceled();
  reader_writer_lock_read_while_holding_writer_can_be_canceled();
#endif

  conditional_waits_until_triggered();
  conditional_triggers_only_one_waiter();
  conditional_drops_trigger_when_no_waiter_exists();
  conditional_handles_multiple_trigger_calls_with_different_waiters();
  conditional_handles_sequential_trigger_wait_cycles();
  conditional_handles_interleaved_waiters();
#if CARDIO_HAS_EXCEPTIONS
  conditional_wait_can_be_canceled();
  conditional_rejects_immediately_when_canceled_before_wait();
  conditional_handles_multiple_simultaneous_cancellations();
  conditional_keeps_queue_integrity_when_waiters_cancel();
#endif

  manually_conditional_waits_until_raised();
  manually_conditional_returns_immediately_when_already_raised();
  manually_conditional_initial_state_can_be_raised();
  manually_conditional_trigger_releases_one_waiter();
  manually_conditional_raise_releases_all_waiters();
  manually_conditional_drop_blocks_after_raise();
  manually_conditional_multiple_raise_calls_are_idempotent();
  manually_conditional_multiple_drop_calls_keep_condition_dropped();
  manually_conditional_handles_rapid_raise_drop_cycles();
#if CARDIO_HAS_EXCEPTIONS
  manually_conditional_wait_can_be_canceled();
  manually_conditional_raised_state_ignores_cancellation_wait();
  manually_conditional_rejects_when_canceled_before_dropped_wait();
  manually_conditional_handles_abort_during_concurrent_waits();
#endif

  std::puts("cardio_primitives_test: PASS");
  return 0;
}
