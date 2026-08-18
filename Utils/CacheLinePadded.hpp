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
//! \note Copy and move construction/assignment are deliberately *not* declared here (no `= delete`, no
//! hand-written `= default`): with only the forwarding constructor below user-provided, the compiler
//! generates all four for `CacheLinePadded<T>` exactly as it would for a plain struct holding a `T`
//! member, which means they simply defer to whatever `T` itself supports. `CacheLinePadded<T>` is
//! copyable/movable whenever `T` is (e.g. a plain struct), and is neither, automatically, whenever `T`
//! is neither (e.g. `Utils::Atomic<T>`, which is deliberately not copyable or movable -- see Atomic.hpp).
//! This wrapper only changes memory layout, not value semantics, so it has no reason to be more
//! restrictive than the type it wraps.
//!
//! \warning Extended alignment (`LINE_SIZE` greater than the platform's default `new`-alignment, which
//! `alignof(std::max_align_t)` is a lower bound for) is only guaranteed to be honored by dynamic
//! allocation (`new`, `std::vector`, ...) from C++17 onward. Before C++14/17 aligned-new support was
//! required (fprime targets C++14), a heap-allocated or `std::vector`-held `CacheLinePadded<T, 64>` may
//! silently receive less alignment than requested on some toolchains. Prefer placing it as a direct
//! member or a fixed-size array member of a statically- or stack-allocated object (the common case in
//! flight software), where ordinary object layout rules apply and this does not arise.
//!
//! \tparam T the wrapped type
//! \tparam LINE_SIZE assumed cache line size in bytes; must be a power of two no smaller than `alignof(T)`
template <typename T, FwSizeType LINE_SIZE = CACHE_LINE_PADDED_DEFAULT_LINE_SIZE>
class alignas(LINE_SIZE) CacheLinePadded {
    static_assert((LINE_SIZE & (LINE_SIZE - 1)) == 0, "CacheLinePadded LINE_SIZE must be a power of two");
    static_assert(LINE_SIZE >= alignof(T), "CacheLinePadded LINE_SIZE must be at least alignof(T)");

  public:
    //! \brief default-construct the wrapped value
    //!
    //! `m_value()` is value-initialization, not default-initialization: a scalar T (e.g. `Atomic<U32>`'s
    //! own wrapped `U32`, or a bare `U32` used as T directly) is zero-initialized rather than left with an
    //! indeterminate value, and a class-type T with a user-provided default constructor (e.g.
    //! `Utils::Atomic<T>`, which itself guarantees a zeroed value) is default-constructed normally --
    //! value-initialization and default-initialization are equivalent for any type with a user-provided
    //! default constructor, so there is no wasted double-initialization for that case. This declaration
    //! does not require T to actually be default-constructible: like any member function of a class
    //! template, its body is only instantiated if this constructor is actually called, so
    //! `CacheLinePadded<T>` for a T with no default constructor remains fully usable through the
    //! forwarding constructor below.
    CacheLinePadded() : m_value() {}

    //! \brief construct the wrapped value, forwarding every argument to T's constructor
    //!
    //! \note Disabled (via the trailing `enable_if`) when called with a single argument that is (or
    //! decays to) `CacheLinePadded` itself. Without that exclusion, a call like
    //! `CacheLinePadded other(std::move(existing))` would be captured by this constructor instead of
    //! falling through to the real copy/move constructor: a "universal reference" constructor like this
    //! one is an exact-match candidate for an argument of the class's own type, and -- as verified against
    //! this exact class -- that exact match can still beat the class's own (implicitly generated)
    //! copy/move constructor in overload resolution when that constructor would be deleted (e.g. because
    //! `T` is a `Utils::Atomic`), turning a clean "use of deleted function" diagnostic into a confusing
    //! failure to instantiate this constructor's body instead. See Scott Meyers, *Effective Modern C++*,
    //! Item 26, for the general form of this pitfall with universal-reference constructors.
    template <
        typename First,
        typename... Rest,
        typename = typename std::enable_if<
            !((sizeof...(Rest) == 0) && std::is_same<CacheLinePadded, typename std::decay<First>::type>::value)>::type>
    explicit CacheLinePadded(First&& first, Rest&&... rest)
        : m_value(std::forward<First>(first), std::forward<Rest>(rest)...) {}

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
