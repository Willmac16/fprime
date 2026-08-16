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
| `compare_exchange_strong/weak(expected, desired, success, failure)` | distinct success/failure memory orders |
| `fetch_add`, `fetch_sub`, `fetch_and`, `fetch_or`, `fetch_xor` | return the previous value |
| `operator+=`, `-=`, `&=`, `|=`, `^=`, `++`, `--` | return the new value (except post-increment/decrement) |
| `is_lock_free()` | runtime query, as on `std::atomic` |
| `isLockFree()` | `static constexpr`, usable in a `static_assert` |

Which of `+=`, `-=`, `&=`, `|=`, `^=`, `++`, `--` are available depends on `T`, matching `std::atomic`'s own
type-category specializations:

| T | Available operators | Notes |
|---|---|---|
| integral, other than `bool` | all seven | ordinary arithmetic/bitwise semantics |
| pointer | `+=`, `-=`, `++`, `--` only | each takes (or acts as) a `std::ptrdiff_t` **element** offset -- `cursor += 3` advances the pointer by three elements, not three bytes, matching `std::atomic<T*>`. No bitwise pointer arithmetic exists, so `&=`, `|=`, `^=` are unavailable. |
| `bool`, or any other trivially copyable T (structs, enums, ...) | none | use `load`/`store`/`exchange`/`compare_exchange_*` instead |

`bool` needs an explicit exclusion (enforced by a `static_assert`) because it is otherwise classified as an
integral type by the standard and `bool + bool` compiles via integer promotion; enums and other non-integral types
are excluded automatically because `std::is_integral` already returns false for them. Instantiating (i.e. calling)
an unavailable operator is a compile error; it does not prevent using the rest of the type's interface -- an
`Atomic<bool>` remains fully usable through `load`/`store`/`exchange`/`compare_exchange_*`.

`fetch_add`/`fetch_sub` (and therefore `+=`/`-=`/`++`/`--`) take a `T` argument for integral `T`, or a
`std::ptrdiff_t` element offset for pointer `T` -- there is no `Delta`-typed public alias, but the parameter type
of these operations reflects it directly.

The `std::memory_order` arguments default to `std::memory_order_seq_cst` and are honored by the lock-free backend.
The mutex-backed backend accepts them for interface compatibility but ignores them, because taking and releasing
`Os::Mutex` already gives every operation on that one `Atomic` instance a well-defined, race-free, globally visible
order regardless of what was requested. That is weaker than true `seq_cst`, though: `seq_cst` additionally places
every `seq_cst` operation on *every* atomic object into a single total order agreed on by all threads, and
per-mutex acquire/release does not establish that relationship between two independently-locked objects (for
example, two separate `Atomic` instances used as the flags of a Dekker's-algorithm-style protocol). Code relying
on that cross-object guarantee needs a lock-free, truly `seq_cst` atomic, not this backend.

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
- Neither backend cache-line aligns or pads its guarded value. Several `Atomic` members placed adjacently in a
  struct can share a cache line and contend (false-share) under concurrent access from different cores -- a
  throughput concern, not a correctness one. This is a deliberate default, not an oversight: baking in a fixed
  alignment (there is no single correct cache-line size across fprime's target platforms) would silently grow
  every instance, the wrong default for a memory-constrained embedded target. A caller in a hot, contended path
  who wants this should wrap the member in `Utils::CacheLinePadded<T>` (see
  [Utils::CacheLinePadded](CacheLinePadded.md)) rather than hand-writing `alignas` at the use site: a bare
  `alignas(N)` on a struct member only guarantees where that member *starts*, not that the *next* member is pushed
  clear of its cache line, so it does not actually solve the problem on its own.

## 4 Unit Testing

Unit tests live in `Utils/test/ut/AtomicTester.cpp` and are registered in `Utils/test/ut/main.cpp`. Every
behavioral test runs against both backends by instantiating each case with the default backend and with the
mutex-backed backend forced, so both paths are covered on any host. The concurrency test runs several `Os::Task`s
performing read-modify-writes against a single counter and checks that no update is lost.

## 5 Change Log

| Date | Description |
|---|---|
| 2026-08-16 | Initial version |
