// ======================================================================
// \title  Atomic.hpp
// \brief  hpp file for a portable atomic variable with a mutex-backed fallback
//
// \copyright
// Copyright 2026, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#ifndef UTILS_ATOMIC_HPP
#define UTILS_ATOMIC_HPP

#include <Fw/FPrimeBasicTypes.hpp>
#include <Os/Mutex.hpp>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace Utils {

//! \brief compile-time check that a `std::atomic` of width WIDTH is always lock-free
//!
//! C++14 lacks `std::atomic<T>::is_always_lock_free` (C++17), so this is derived from the standard
//! `ATOMIC_*_LOCK_FREE` macros (0 = never, 1 = sometimes, 2 = always lock-free), selected by matching
//! the width of the corresponding builtin type so that no particular ABI (e.g. `sizeof(int) == 4`) is
//! assumed. Only always-lock-free (value 2) widths are accepted: a sometimes-lock-free width cannot be
//! resolved at compile time and the backend of Utils::Atomic must be chosen at compile time, so those
//! widths take the mutex-backed implementation.
//!
//! \note Derived from `std::integral_constant` so that `value` may be odr-used (bound to a reference,
//! for example by a test assertion) without this header having to supply a definition of its own.
template <FwSizeType WIDTH>
struct AtomicWidthIsLockFree
    : std::integral_constant<bool,
                             ((sizeof(unsigned char) == WIDTH) && (ATOMIC_CHAR_LOCK_FREE == 2)) ||
                                 ((sizeof(unsigned short) == WIDTH) && (ATOMIC_SHORT_LOCK_FREE == 2)) ||
                                 ((sizeof(unsigned int) == WIDTH) && (ATOMIC_INT_LOCK_FREE == 2)) ||
                                 ((sizeof(unsigned long) == WIDTH) && (ATOMIC_LONG_LOCK_FREE == 2)) ||
                                 ((sizeof(unsigned long long) == WIDTH) && (ATOMIC_LLONG_LOCK_FREE == 2))> {};

//! \brief compile-time check that `std::atomic<T>` is always lock-free on this platform
//!
//! Types whose width does not match an always-lock-free builtin width (e.g. a 64-bit counter on a
//! 32-bit target, or any type wider than the largest atomic instruction) report false, and
//! Utils::Atomic uses its mutex-backed implementation for them.
template <typename T>
struct AtomicIsLockFree : AtomicWidthIsLockFree<static_cast<FwSizeType>(sizeof(T))> {};

//! \brief `bool` has a dedicated lock-free macro
template <>
struct AtomicIsLockFree<bool> : std::integral_constant<bool, (ATOMIC_BOOL_LOCK_FREE == 2)> {};

//! \brief pointers have a dedicated lock-free macro
template <typename T>
struct AtomicIsLockFree<T*> : std::integral_constant<bool, (ATOMIC_POINTER_LOCK_FREE == 2)> {};

//! \brief true when T supports fetch_and/fetch_or/fetch_xor and the bitwise/arithmetic compound
//! assignment operators (`&=`, `|=`, `^=`, and -- jointly with AtomicSupportsPointerOps -- `+=`, `-=`,
//! `++`, `--`)
//!
//! This matches the type category for which the C++ standard specializes `std::atomic` with those
//! operations: every integral type other than `bool`. `bool` is itself classified as an integral type
//! by the standard (unlike an enumeration type, which `std::is_integral` correctly excludes), so it
//! needs an explicit exclusion here; `std::atomic<bool>` is the generic/primary template and offers only
//! load, store, exchange and compare_exchange, not the fetch_* family.
template <typename T>
struct AtomicSupportsIntegralOps
    : std::integral_constant<bool, std::is_integral<T>::value && !std::is_same<T, bool>::value> {};

//! \brief true when T is a pointer, which supports fetch_add/fetch_sub and `+=`, `-=`, `++`, `--` with a
//! `std::ptrdiff_t` element offset (`std::atomic<T*>`'s pointer specialization)
//!
//! Pointers do not support the bitwise fetch_and/fetch_or/fetch_xor operations or `&=`, `|=`, `^=`.
template <typename T>
struct AtomicSupportsPointerOps : std::is_pointer<T> {};

//! \brief the argument type of fetch_add/fetch_sub (and the operand of `+=`, `-=`, `++`, `--`) for T
//!
//! T itself for integral types. For pointer types, `std::ptrdiff_t`, so that the argument advances the
//! pointer by that many elements -- matching `std::atomic<T*>::fetch_add` -- rather than (nonsensically)
//! being another pointer value of type T.
template <typename T>
struct AtomicDeltaType {
    using type = T;
};

//! \brief pointer specialization of AtomicDeltaType: advances by element count, not by byte count
template <typename T>
struct AtomicDeltaType<T*> {
    using type = std::ptrdiff_t;
};

namespace AtomicInternal {

//! \brief lock-free backend for Utils::Atomic: a thin pass-through to `std::atomic`
//!
//! Selected when `std::atomic<T>` is always lock-free on the target platform. Every operation compiles
//! to the native atomic instruction sequence, so this backend is ISR-safe.
template <typename T>
class LockFreeBackend {
  public:
    //! \brief construct the backend with an initial value
    explicit LockFreeBackend(T value) : m_value(value) {}

    //! \brief true when operations are lock-free; always true for this backend
    bool is_lock_free() const { return this->m_value.is_lock_free(); }

    //! \brief atomically read the value
    T load(std::memory_order order = std::memory_order_seq_cst) const { return this->m_value.load(order); }

    //! \brief atomically write the value
    void store(T value, std::memory_order order = std::memory_order_seq_cst) { this->m_value.store(value, order); }

    //! \brief atomically write the value and return the previous value
    T exchange(T value, std::memory_order order = std::memory_order_seq_cst) {
        return this->m_value.exchange(value, order);
    }

    //! \brief atomically replace the value with `desired` when it equals `expected`
    //! \return true on success; on failure `expected` is updated with the observed value
    bool compare_exchange_strong(T& expected, T desired, std::memory_order order = std::memory_order_seq_cst) {
        return this->m_value.compare_exchange_strong(expected, desired, order);
    }

    //! \brief as compare_exchange_strong, but allowed to fail spuriously
    bool compare_exchange_weak(T& expected, T desired, std::memory_order order = std::memory_order_seq_cst) {
        return this->m_value.compare_exchange_weak(expected, desired, order);
    }

    //! \brief compare_exchange_strong with distinct success/failure memory orders
    bool compare_exchange_strong(T& expected, T desired, std::memory_order success, std::memory_order failure) {
        return this->m_value.compare_exchange_strong(expected, desired, success, failure);
    }

    //! \brief compare_exchange_weak with distinct success/failure memory orders
    bool compare_exchange_weak(T& expected, T desired, std::memory_order success, std::memory_order failure) {
        return this->m_value.compare_exchange_weak(expected, desired, success, failure);
    }

    //! \brief atomically add to the value and return the previous value
    //!
    //! Requires an integral T (other than bool) or a pointer T -- see AtomicSupportsIntegralOps and
    //! AtomicSupportsPointerOps. For pointer T, `argument` is an element offset applied through pointer
    //! arithmetic, not a byte offset.
    T fetch_add(typename AtomicDeltaType<T>::type argument, std::memory_order order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value || AtomicSupportsPointerOps<T>::value,
                      "Utils::Atomic<T>::fetch_add requires an integral T (other than bool) or a pointer T");
        return this->m_value.fetch_add(argument, order);
    }

    //! \brief atomically subtract from the value and return the previous value
    //!
    //! Requires an integral T (other than bool) or a pointer T; see fetch_add.
    T fetch_sub(typename AtomicDeltaType<T>::type argument, std::memory_order order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value || AtomicSupportsPointerOps<T>::value,
                      "Utils::Atomic<T>::fetch_sub requires an integral T (other than bool) or a pointer T");
        return this->m_value.fetch_sub(argument, order);
    }

    //! \brief atomically bitwise-and the value and return the previous value
    //! Requires an integral T other than bool; see AtomicSupportsIntegralOps.
    T fetch_and(T argument, std::memory_order order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value,
                      "Utils::Atomic<T>::fetch_and requires an integral T other than bool");
        return this->m_value.fetch_and(argument, order);
    }

    //! \brief atomically bitwise-or the value and return the previous value
    //! Requires an integral T other than bool; see AtomicSupportsIntegralOps.
    T fetch_or(T argument, std::memory_order order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value,
                      "Utils::Atomic<T>::fetch_or requires an integral T other than bool");
        return this->m_value.fetch_or(argument, order);
    }

    //! \brief atomically bitwise-xor the value and return the previous value
    //! Requires an integral T other than bool; see AtomicSupportsIntegralOps.
    T fetch_xor(T argument, std::memory_order order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value,
                      "Utils::Atomic<T>::fetch_xor requires an integral T other than bool");
        return this->m_value.fetch_xor(argument, order);
    }

  private:
    std::atomic<T> m_value;  //!< the underlying lock-free atomic
};

//! \brief mutex-backed backend for Utils::Atomic
//!
//! Selected when `std::atomic<T>` is not guaranteed lock-free on the target platform. Rather than let
//! the standard library fall back on a hidden global lock table (which may require `libatomic`, may not
//! be available on a bare-metal target, and is invisible to the reader), the value is protected by an
//! explicit `Os::Mutex`.
//!
//! \warning This backend takes a mutex and therefore must not be used from an interrupt service routine.
//! Code that must be ISR-safe should `static_assert(Utils::AtomicIsLockFree<T>::value)` on the value type.
//!
//! The `std::memory_order` arguments are accepted for interface compatibility and ignored. Mutual exclusion
//! plus the acquire-on-lock/release-on-unlock semantics of `Os::Mutex` are sufficient to make every operation
//! on `m_value` race-free and to give it a well-defined, globally visible modification order -- so a single
//! `Atomic` instance behaves correctly regardless of the order requested. That is not the same guarantee as
//! `std::memory_order_seq_cst`, though: seq_cst additionally places every seq_cst operation on *every* atomic
//! object into one total order agreed on by all threads, and per-mutex acquire/release does not establish
//! that relationship between operations on two independently-locked objects (for example, two separate
//! `Atomic` instances used as the two flags of a Dekker's-algorithm-style protocol). Code relying on that
//! cross-object guarantee needs true `seq_cst` atomics, not this backend.
//!
//! \note `m_value` and `m_mutex` are not cache-line aligned or padded. Several mutex-backed `Atomic`
//! members placed adjacently in a struct can therefore share a cache line and contend under concurrent
//! access from different cores. That is a throughput concern, not a correctness one (the mutex still
//! serializes access correctly); a caller in a hot, multi-core path who cares should add explicit padding
//! or `alignas` around the member, sized for their own target's cache line.
template <typename T>
class MutexBackend {
  public:
    //! \brief construct the backend with an initial value
    explicit MutexBackend(T value) : m_value(value) {}

    //! \brief true when operations are lock-free; always false for this backend
    bool is_lock_free() const { return false; }

    //! \brief atomically read the value
    T load(std::memory_order = std::memory_order_seq_cst) const {
        Os::ScopeLock lock(this->m_mutex);
        return this->m_value;
    }

    //! \brief atomically write the value
    void store(T value, std::memory_order = std::memory_order_seq_cst) {
        Os::ScopeLock lock(this->m_mutex);
        this->m_value = value;
    }

    //! \brief atomically write the value and return the previous value
    T exchange(T value, std::memory_order = std::memory_order_seq_cst) {
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = value;
        return previous;
    }

    //! \brief atomically replace the value with `desired` when it equals `expected`
    //!
    //! \note Unlike `std::atomic`, the comparison uses `operator==` on T rather than a byte-wise
    //! comparison. For the integral and pointer types this class targets the two agree, and `operator==`
    //! avoids spurious failures from padding bytes.
    //!
    //! \return true on success; on failure `expected` is updated with the observed value
    bool compare_exchange_strong(T& expected, T desired, std::memory_order = std::memory_order_seq_cst) {
        Os::ScopeLock lock(this->m_mutex);
        const bool matched = (this->m_value == expected);
        if (matched) {
            this->m_value = desired;
        } else {
            expected = this->m_value;
        }
        return matched;
    }

    //! \brief as compare_exchange_strong; this backend never fails spuriously
    bool compare_exchange_weak(T& expected, T desired, std::memory_order order = std::memory_order_seq_cst) {
        return this->compare_exchange_strong(expected, desired, order);
    }

    //! \brief compare_exchange_strong with distinct success/failure memory orders; both are ignored
    bool compare_exchange_strong(T& expected, T desired, std::memory_order, std::memory_order) {
        return this->compare_exchange_strong(expected, desired);
    }

    //! \brief compare_exchange_weak with distinct success/failure memory orders; both are ignored
    bool compare_exchange_weak(T& expected, T desired, std::memory_order, std::memory_order) {
        return this->compare_exchange_strong(expected, desired);
    }

    //! \brief atomically add to the value and return the previous value
    //!
    //! Requires an integral T (other than bool) or a pointer T; see LockFreeBackend::fetch_add. For
    //! pointer T, ordinary pointer arithmetic (`T* + std::ptrdiff_t`) gives the same element-wise
    //! semantics as `std::atomic<T*>::fetch_add`.
    T fetch_add(typename AtomicDeltaType<T>::type argument, std::memory_order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value || AtomicSupportsPointerOps<T>::value,
                      "Utils::Atomic<T>::fetch_add requires an integral T (other than bool) or a pointer T");
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous + argument);
        return previous;
    }

    //! \brief atomically subtract from the value and return the previous value
    //! Requires an integral T (other than bool) or a pointer T; see fetch_add.
    T fetch_sub(typename AtomicDeltaType<T>::type argument, std::memory_order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value || AtomicSupportsPointerOps<T>::value,
                      "Utils::Atomic<T>::fetch_sub requires an integral T (other than bool) or a pointer T");
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous - argument);
        return previous;
    }

    //! \brief atomically bitwise-and the value and return the previous value
    //! Requires an integral T other than bool; see AtomicSupportsIntegralOps.
    T fetch_and(T argument, std::memory_order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value,
                      "Utils::Atomic<T>::fetch_and requires an integral T other than bool");
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous & argument);
        return previous;
    }

    //! \brief atomically bitwise-or the value and return the previous value
    //! Requires an integral T other than bool; see AtomicSupportsIntegralOps.
    T fetch_or(T argument, std::memory_order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value,
                      "Utils::Atomic<T>::fetch_or requires an integral T other than bool");
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous | argument);
        return previous;
    }

    //! \brief atomically bitwise-xor the value and return the previous value
    //! Requires an integral T other than bool; see AtomicSupportsIntegralOps.
    T fetch_xor(T argument, std::memory_order = std::memory_order_seq_cst) {
        static_assert(AtomicSupportsIntegralOps<T>::value,
                      "Utils::Atomic<T>::fetch_xor requires an integral T other than bool");
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous ^ argument);
        return previous;
    }

  private:
    T m_value;                  //!< the guarded value; only touched while holding m_mutex
    mutable Os::Mutex m_mutex;  //!< guards m_value; mutable so that load() may be const
};

//! \brief select the backend implementing Utils::Atomic
template <typename T, bool USE_MUTEX>
struct BackendSelector {
    using type = LockFreeBackend<T>;
};

//! \brief mutex-backed selection
template <typename T>
struct BackendSelector<T, true> {
    using type = MutexBackend<T>;
};

}  // namespace AtomicInternal

//! \class Atomic
//! \brief an atomic variable that uses a mutex only where the platform needs one
//!
//! `Utils::Atomic<T>` provides the subset of the `std::atomic<T>` interface that flight software needs
//! -- `load`, `store`, `exchange`, `compare_exchange_*`, the `fetch_*` operations, and the compound
//! assignment operators `+=`, `-=`, `&=`, `|=`, `^=`, `++` and `--` -- with one difference in behavior:
//! the backend is chosen at compile time.
//!
//! - When `std::atomic<T>` is *always* lock-free on the target platform, `Utils::Atomic<T>` is a
//!   zero-overhead pass-through to `std::atomic<T>` and is ISR-safe.
//! - Otherwise (for example a `U64` counter on a 32-bit target) the value is protected by an explicit
//!   `Os::Mutex` instead of the standard library's hidden lock table. This keeps the behavior visible
//!   and portable, and removes any dependency on `libatomic` being available for the target.
//!
//! `isLockFree()` reports which backend was selected at compile time, so a caller that requires
//! lock-free (and therefore ISR-safe) behavior can enforce it:
//!
//! ```c++
//! static_assert(Utils::Atomic<U32>::isLockFree(), "U32 counter must be lock-free for ISR use");
//! ```
//!
//! Usage mirrors `std::atomic`:
//!
//! ```c++
//! Utils::Atomic<U32> counter(0);
//! counter += 5;               // atomic read-modify-write, returns the new value
//! counter++;                  // atomic increment
//! U32 value = counter.load(); // or: U32 value = counter;
//! ```
//!
//! The compound assignment operators, like their `std::atomic` counterparts, are each a single atomic
//! read-modify-write; they do not decompose into a separate load and store. Which ones are available
//! depends on T, matching `std::atomic`'s own type-category specializations:
//!
//! - Integral T other than `bool`: all of `+=`, `-=`, `&=`, `|=`, `^=`, `++`, `--`.
//! - Pointer T: `+=`, `-=`, `++`, `--` only, each taking (or acting as) a `std::ptrdiff_t` element
//!   offset -- `pointerAtomic += 3` advances the pointer by three elements, not three bytes -- matching
//!   `std::atomic<T*>`. There is no bitwise pointer arithmetic, so `&=`, `|=`, `^=` are not available.
//! - `bool` and any other trivially copyable T (structs, enums, ...): none of the above. `bool` is
//!   explicitly excluded even though it would otherwise compile through integral promotion (`bool + bool`
//!   is a valid, if meaningless, expression); enums and structs are excluded because they generally lack
//!   the necessary operators in the first place. Use `load`/`store`/`exchange`/`compare_exchange_*`.
//!
//! An operator instantiated for a T outside its supported category is a compile error (a `static_assert`
//! inside the corresponding backend method), evaluated only when that operator is actually called -- an
//! `Atomic<bool>` or `Atomic<SomeStruct>` remains fully usable through the always-available operations.
//!
//! \tparam T value type; must be trivially copyable
//! \tparam USE_MUTEX selects the mutex-backed implementation. Defaults to "only where the platform needs
//!         it" and may be set to `true` to force the mutex-backed implementation (for example to test
//!         that backend on a host where every type is lock-free).
//!
//! \warning Copy construction and copy assignment are deleted, matching `std::atomic`. `Atomic<T>` is
//! intended to be a member of a long-lived object, not a value passed around.
//!
//! \note The selected backend is a base class purely as a mixin: it supplies the public operations above and
//! is not polymorphic, so an `Atomic` is never destroyed through a pointer to its backend.
template <typename T, bool USE_MUTEX = !AtomicIsLockFree<T>::value>
class Atomic : public AtomicInternal::BackendSelector<T, USE_MUTEX>::type {
  private:
    using Base = typename AtomicInternal::BackendSelector<T, USE_MUTEX>::type;
    //! \brief the operand type of +=, -=, ++ and -- for T: T itself, or std::ptrdiff_t for pointer T
    using Delta = typename AtomicDeltaType<T>::type;

    static_assert(std::is_trivially_copyable<T>::value, "Utils::Atomic requires a trivially copyable type");

  public:
    //! \brief construct an atomic holding a value-initialized (zero) T
    //!
    //! \note Unlike a default-constructed `std::atomic`, the value is always initialized.
    Atomic() : Base(T()) {}

    //! \brief construct an atomic holding the supplied value
    //!
    //! Implicit, matching `std::atomic`, so that `Utils::Atomic<U32> counter = 0;` compiles.
    Atomic(T value) : Base(value) {}  // NOLINT(google-explicit-constructor)

    //! \brief copy construction is forbidden
    Atomic(const Atomic& other) = delete;

    //! \brief copy assignment is forbidden
    Atomic& operator=(const Atomic& other) = delete;

    //! \brief true when this instantiation is lock-free, known at compile time
    static constexpr bool isLockFree() { return !USE_MUTEX; }

    //! \brief atomically store a value
    //! \return the stored value, matching `std::atomic`
    T operator=(T value) {
        this->store(value);
        return value;
    }

    //! \brief atomically read the value
    operator T() const { return this->load(); }

    //! \brief atomically add to the value (or, for pointer T, advance it by `argument` elements)
    //! \return the new value, matching `std::atomic`
    T operator+=(Delta argument) { return static_cast<T>(this->fetch_add(argument) + argument); }

    //! \brief atomically subtract from the value (or, for pointer T, retreat it by `argument` elements)
    //! \return the new value, matching `std::atomic`
    T operator-=(Delta argument) { return static_cast<T>(this->fetch_sub(argument) - argument); }

    //! \brief atomically bitwise-and the value
    //! \return the new value, matching `std::atomic`
    T operator&=(T argument) { return static_cast<T>(this->fetch_and(argument) & argument); }

    //! \brief atomically bitwise-or the value
    //! \return the new value, matching `std::atomic`
    T operator|=(T argument) { return static_cast<T>(this->fetch_or(argument) | argument); }

    //! \brief atomically bitwise-xor the value
    //! \return the new value, matching `std::atomic`
    T operator^=(T argument) { return static_cast<T>(this->fetch_xor(argument) ^ argument); }

    //! \brief atomically increment the value (or, for pointer T, advance it by one element)
    //! \return the new value
    T operator++() {
        const Delta one = static_cast<Delta>(1);
        return static_cast<T>(this->fetch_add(one) + one);
    }

    //! \brief atomically increment the value (or, for pointer T, advance it by one element)
    //! \return the previous value
    T operator++(int) { return this->fetch_add(static_cast<Delta>(1)); }

    //! \brief atomically decrement the value (or, for pointer T, retreat it by one element)
    //! \return the new value
    T operator--() {
        const Delta one = static_cast<Delta>(1);
        return static_cast<T>(this->fetch_sub(one) - one);
    }

    //! \brief atomically decrement the value (or, for pointer T, retreat it by one element)
    //! \return the previous value
    T operator--(int) { return this->fetch_sub(static_cast<Delta>(1)); }
};

}  // namespace Utils

#endif  // UTILS_ATOMIC_HPP
