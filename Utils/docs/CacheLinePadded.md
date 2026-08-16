# Utils::CacheLinePadded

## 1 Introduction

`Utils::CacheLinePadded<T>` wraps a value so that it is aligned to, and padded out to a whole multiple of, a
cache line. It exists to prevent **false sharing**: two independently, frequently written objects (a
[`Utils::Atomic`](Atomic.md) counter is the common case, but this works for any `T`) placed adjacently in a
struct or array can end up in the same cache line purely by memory-layout accident. When that happens, one
core's write invalidates the line for every other core reading or writing the *other* object in that line, even
though the two objects have nothing to do with each other -- a pure throughput cost, sometimes an order of
magnitude or more under real contention, with no correctness benefit.

It costs nothing when objects aren't actually contended by different cores, so it is **opt-in**: nothing in
`Utils::Atomic` (or anywhere else) reaches for it automatically. Wrapping every small atomic in a project by
default would waste 64 (or more) bytes per instance for no benefit in the common flight-software case where two
atomics are never touched from different cores at the same time. Reach for it only for a specific, measured hot
path with real cross-core contention.

## 2 Usage

```cpp
#include <Utils/Atomic.hpp>
#include <Utils/CacheLinePadded.hpp>

struct Counters {
    Utils::CacheLinePadded<Utils::Atomic<U32>> producerCount{0};
    Utils::CacheLinePadded<Utils::Atomic<U32>> consumerCount{0};
};
...
counters.producerCount.get() += 1;      // .get() gives full access to the wrapped Atomic<U32>
U32 snapshot = counters.consumerCount.get().load();
counters.producerCount->store(0);       // operator-> also reaches the wrapped value's members
```

`get()` (and `operator->`) give unrestricted access to the wrapped value -- every operation `Utils::Atomic<T>`
(or plain `T`) supports is available exactly as if it weren't wrapped; `CacheLinePadded` only changes layout, not
behavior.

Like `Utils::Atomic`, copy construction and copy assignment are deleted.

### 2.1 Choosing a line size

The second template parameter is the assumed cache line size in bytes, defaulting to
`Utils::CACHE_LINE_PADDED_DEFAULT_LINE_SIZE` (64 -- correct for most x86_64 and aarch64 desktop/server parts).
There is no single correct value across fprime's target platforms (some embedded ARM cores use 32 bytes; some
newer x86 parts effectively use 128 because of the adjacent-line prefetcher), so a project that knows its
target's real line size should pass it explicitly:

```cpp
Utils::CacheLinePadded<Utils::Atomic<U32>, 32> counter{0};  // target uses a 32-byte line
```

## 3 Why not just `alignas`?

A bare `alignas(N)` on a struct member looks like it should be enough, but it isn't:

```cpp
struct Naive {
    alignas(64) Utils::Atomic<U32> a;  // starts on a fresh 64-byte line...
    Utils::Atomic<U32> b;              // ...but this can still land in the SAME line as `a`
};
```

`alignas` on a member only constrains where *that* member starts; it does not extend the member's size, so the
compiler is free to place the very next member immediately after it -- inside the same cache line. Measured on a
representative build (`Utils/test/ut/CacheLinePaddedTester.cpp`, `TestSeparation`), `a` and `b` above land 4 bytes
apart, both within the same 64-byte line.

`CacheLinePadded<T>` applies `alignas` to the wrapping *class* instead of to a member. That is a meaningful
difference: for any complete type, the C++ standard requires `sizeof` to always be a whole multiple of `alignof`
(this is what makes arrays of the type work correctly), so an `alignas`-qualified class automatically has its
`sizeof` padded out to match. `CacheLinePadded` exists so that fprime code gets that guarantee for free, correct
for whatever line size is specified, instead of every use site hand-writing (and needing to keep in sync with a
target's real line size) an ad hoc trailing padding member.

## 4 Unit Testing

Unit tests live in `Utils/test/ut/CacheLinePaddedTester.cpp` and are registered in `Utils/test/ut/main.cpp`. They
check the alignment/size invariant directly (including for a `T` wider than the line size, which must round up
to the next whole multiple), that `get()`/`operator->` give full access to the wrapped value, that adjacent
struct members and adjacent array elements are measurably separated by at least one line size, and that two
padded counters in the same struct, hammered concurrently by several `Os::Task`s, both come out with every
update accounted for.

## 5 Change Log

| Date | Description |
|---|---|
| 2026-08-16 | Initial version |
