// ======================================================================
// \title  CacheLinePadded.hpp
// \brief  hpp file for a wrapper that isolates a value onto its own cache line(s)
//
// \copyright
// Copyright 2026, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#ifndef UTILS_CACHE_LINE_PADDED_HPP
#define UTILS_CACHE_LINE_PADDED_HPP

#include <Fw/FPrimeBasicTypes.hpp>
#include <type_traits>
#include <utility>

namespace Utils {

//! \brief assumed cache line size, in bytes, when none is given to CacheLinePadded explicitly
//!
//! There is no single correct value across fprime's target platforms (some embedded ARM cores use 32
//! bytes; most x86_64/aarch64 desktop and server parts use 64; some newer x86 parts effectively use 128
//! because of the adjacent-line prefetcher). 64 is a reasonable, common default; a project that knows its
//! target's real line size should pass it explicitly as CacheLinePadded's second template parameter.
constexpr FwSizeType CACHE_LINE_PADDED_DEFAULT_LINE_SIZE = 64;

//! \class CacheLinePadded
//! \brief wraps a T so that it is aligned to, and padded out to a whole multiple of, a cache line
//!
//! Placing several independently, frequently written objects (a `Utils::Atomic` counter is the common
//! case, but this works for any T) adjacently in a struct or array lets two cores' writes to *different*
//! objects invalidate each other's cache lines purely because the objects happen to share one -- false
//! sharing. It costs nothing when the objects aren't actually contended by different cores, and can cost
//! a lot (an order of magnitude or more in the worst case) when they are.
//!
//! Preventing it requires both ends of the object to land on cache-line boundaries: the object must
//! *start* on a boundary (so it doesn't share a line with whatever precedes it), and its *size* must be a
//! whole multiple of the line size (so whatever follows it doesn't share a line with its tail).
//!
//! \warning A bare `alignas(64)` on a struct member only gives the first property. It establishes where
//! the member starts but does not extend its size, so the very next member can still be packed into the
//! remaining bytes of that same cache line:
//!
//! ```c++
//! struct Naive {
//!     alignas(64) Utils::Atomic<U32> a;  // starts on a fresh line...
//!     Utils::Atomic<U32> b;              // ...but this can still land in the *same* line as `a`
//! };
//! ```
//!
//! `CacheLinePadded<T>` gives both properties, by applying `alignas` to the wrapping *class* rather than
//! to a member of it. That is a meaningful difference, not a stylistic one: for any complete type,
//! `sizeof` is required to always be a whole multiple of `alignof`, so an `alignas`-qualified class has
//! its `sizeof` padded out to match automatically -- the compiler does the padding this class exists to
//! guarantee, rather than the ad hoc trailing member fprime code would otherwise have to hand-write (and
//! keep in sync with a specific target's line size) at every use site.
//!
//! ```c++
//! struct Counters {
//!     Utils::CacheLinePadded<Utils::Atomic<U32>> producerCount{0};
//!     Utils::CacheLinePadded<Utils::Atomic<U32>> consumerCount{0};
//! };
//! ...
//! counters.producerCount.get() += 1;
//! ```
//!
//! Unlike `Utils::Atomic` itself, this is opt-in and must be applied explicitly at each use site: wrapping
//! every small atomic in a project by default would waste 64 (or more) bytes per instance for no benefit
//! in the common case (in flight software especially) where two atomics are not actually contended by
//! different cores. Reach for it only for a specific, measured hot path with real cross-core contention.
//!
//! \tparam T the wrapped type
//! \tparam LINE_SIZE assumed cache line size in bytes; must be a power of two no smaller than `alignof(T)`
template <typename T, FwSizeType LINE_SIZE = CACHE_LINE_PADDED_DEFAULT_LINE_SIZE>
class alignas(LINE_SIZE) CacheLinePadded {
    static_assert((LINE_SIZE & (LINE_SIZE - 1)) == 0, "CacheLinePadded LINE_SIZE must be a power of two");
    static_assert(LINE_SIZE >= alignof(T), "CacheLinePadded LINE_SIZE must be at least alignof(T)");

  public:
    //! \brief construct the wrapped value, forwarding every argument to T's constructor
    template <typename... Args>
    explicit CacheLinePadded(Args&&... args) : m_value(std::forward<Args>(args)...) {}

    //! \brief copy construction is forbidden, matching Utils::Atomic
    CacheLinePadded(const CacheLinePadded& other) = delete;

    //! \brief copy assignment is forbidden, matching Utils::Atomic
    CacheLinePadded& operator=(const CacheLinePadded& other) = delete;

    //! \brief access the wrapped value
    T& get() { return this->m_value; }

    //! \brief access the wrapped value
    const T& get() const { return this->m_value; }

    //! \brief access a member of the wrapped value
    T* operator->() { return &this->m_value; }

    //! \brief access a member of the wrapped value
    const T* operator->() const { return &this->m_value; }

  private:
    T m_value;  //!< the wrapped value; alignas on the class pads sizeof() out around this
};

}  // namespace Utils

#endif  // UTILS_CACHE_LINE_PADDED_HPP
