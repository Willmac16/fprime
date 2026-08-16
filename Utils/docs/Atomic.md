# Utils::Atomic

## 1 Introduction

`Utils::Atomic<T>` is an atomic variable that uses a mutex only where the target platform needs one.

`std::atomic<T>` is not lock-free for every type on every platform. When it is not (a `U64` counter on a 32-bit
target is the common case), the standard library silently falls back to a hidden global lock table. That fallback
may require linking `libatomic`, may not exist at all for a bare-metal target, and is invisible in the source.

`Utils::Atomic<T>` makes the choice explicit and makes it at compile time:

| Condition | Backend | ISR-safe |
|---|---|---|
| `std::atomic<T>` is always lock-free on the target | pass-through to `std::atomic<T>` | yes |
| otherwise | value guarded by an `Os::Mutex` | no |

The selection is reported by `Utils::Atomic<T>::isLockFree()`, a `constexpr` function, so code that requires
lock-free (and therefore ISR-safe) behavior can enforce it at compile time.

## 2 Usage

### 2.1 Basic

Usage mirrors `std::atomic`:

```cpp
#include <Utils/Atomic.hpp>
...
Utils::Atomic<U32> counter(0);

counter += 5;                // atomic read-modify-write, evaluates to the new value (5)
counter++;                   // atomic increment
U32 value = counter.load();  // or: U32 value = counter;
counter = 0;                 // atomic store
```

The compound assignment operators `+=`, `-=`, `&=`, `|=`, `^=`, `++` and `--` are each a single atomic
read-modify-write; they do not decompose into a separate load and store, so no update is lost when two threads
update the same variable. Their return values match `std::atomic`: compound assignment and the pre-increment and
pre-decrement forms evaluate to the new value, the post-increment and post-decrement forms to the previous value,
and the `fetch_*` operations to the previous value.

The full interface is:

| Operation | Notes |
|---|---|
| `load(order)`, `operator T()` | atomic read |
| `store(value, order)`, `operator=(value)` | atomic write; assignment evaluates to the assigned value |
| `exchange(value, order)` | atomic write, returns the previous value |
| `compare_exchange_strong(expected, desired, order)` | returns true on success; on failure updates `expected` |
| `compare_exchange_weak(expected, desired, order)` | may fail spuriously, so call it in a loop |
| `fetch_add`, `fetch_sub`, `fetch_and`, `fetch_or`, `fetch_xor` | return the previous value |
| `operator+=`, `-=`, `&=`, `|=`, `^=`, `++`, `--` | return the new value (except post-increment/decrement) |
| `is_lock_free()` | runtime query, as on `std::atomic` |
| `isLockFree()` | `static constexpr`, usable in a `static_assert` |

The arithmetic and bitwise operations are available for integral types. `Utils::Atomic<bool>` and
`Utils::Atomic<T*>` supply the load, store, exchange and compare-exchange operations; instantiating an arithmetic
or bitwise operator on those types is a compile error.

The `std::memory_order` arguments default to `std::memory_order_seq_cst` and are honored by the lock-free backend.
The mutex-backed backend accepts them for interface compatibility and ignores them: taking and releasing the mutex
orders every access at least as strongly as `std::memory_order_seq_cst` would.

Copy construction and copy assignment are deleted, matching `std::atomic`. A `Utils::Atomic` is meant to be a
member of a long-lived object, not a value passed around.

### 2.2 Requiring a lock-free (ISR-safe) variable

The mutex-backed backend takes a mutex, so it must not be used from an interrupt service routine. Code that runs
in an ISR should state that requirement so that a port to a platform lacking the necessary atomic instruction
fails to build rather than deadlocking in flight:

```cpp
// fails to compile on a platform where a 32-bit atomic is not lock-free
static_assert(Utils::Atomic<U32>::isLockFree(), "ISR counter must be lock-free");

Utils::Atomic<U32> m_isrCounter;
```

The underlying trait, `Utils::AtomicIsLockFree<T>::value`, is also available directly and is a
`std::integral_constant`.

### 2.3 Forcing the mutex-backed implementation

The second template parameter selects the backend. It defaults to "use a mutex only where the platform needs one"
and may be set to `true` to force the mutex-backed implementation -- useful for testing that path on a host where
every relevant type is lock-free:

```cpp
Utils::Atomic<U32, true> alwaysMutexed(0);  // uses Os::Mutex even though U32 is lock-free here
```

Note that `Utils::Atomic<U32>` and `Utils::Atomic<U32, true>` are distinct types.

### 2.4 Choosing between Utils::Atomic and std::atomic

Use `std::atomic` directly when the algorithm itself requires lock-free behavior and the code already asserts it
(as `Os::Generic::LocklessPriorityQueue` and `Types::SpscQueue` do). Use `Utils::Atomic` for a shared counter,
flag, or state word where correctness -- not lock-freedom -- is the requirement, and where the code must build and
run on platforms whose atomic support differs.

## 3 Implementation Notes

- The lock-free backend is selected only for widths the standard `ATOMIC_*_LOCK_FREE` macros report as *always*
  lock-free (value 2). A width reported as *sometimes* lock-free (value 1) cannot be resolved at compile time, and
  the backend must be chosen at compile time, so those widths take the mutex-backed implementation.
- Widths are matched against the widths of the builtin types rather than assuming a particular ABI, so the trait
  is correct on platforms where, for example, `sizeof(int) != 4`.
- The mutex-backed `compare_exchange_*` compares with `operator==` rather than byte-wise as `std::atomic` does.
  For the integral and pointer types this class targets the two agree, and `operator==` avoids spurious failures
  caused by padding bytes.
- A default-constructed `Utils::Atomic` always holds a value-initialized (zero) `T`. A default-constructed
  `std::atomic` does not.
- `T` must be trivially copyable, which is enforced by a `static_assert`.

## 4 Unit Testing

Unit tests live in `Utils/test/ut/AtomicTester.cpp` and are registered in `Utils/test/ut/main.cpp`. Every
behavioral test runs against both backends by instantiating each case with the default backend and with the
mutex-backed backend forced, so both paths are covered on any host. The concurrency test runs several `Os::Task`s
performing read-modify-writes against a single counter and checks that no update is lost.

## 5 Change Log

| Date | Description |
|---|---|
| 2026-08-16 | Initial version |
