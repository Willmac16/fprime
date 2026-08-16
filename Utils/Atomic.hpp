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
    T fetch_add(T argument, std::memory_order order = std::memory_order_seq_cst) {
        return this->m_value.fetch_add(argument, order);
    }

    //! \brief atomically subtract from the value and return the previous value
    T fetch_sub(T argument, std::memory_order order = std::memory_order_seq_cst) {
        return this->m_value.fetch_sub(argument, order);
    }

    //! \brief atomically bitwise-and the value and return the previous value
    T fetch_and(T argument, std::memory_order order = std::memory_order_seq_cst) {
        return this->m_value.fetch_and(argument, order);
    }

    //! \brief atomically bitwise-or the value and return the previous value
    T fetch_or(T argument, std::memory_order order = std::memory_order_seq_cst) {
        return this->m_value.fetch_or(argument, order);
    }

    //! \brief atomically bitwise-xor the value and return the previous value
    T fetch_xor(T argument, std::memory_order order = std::memory_order_seq_cst) {
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
    T fetch_add(T argument, std::memory_order = std::memory_order_seq_cst) {
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous + argument);
        return previous;
    }

    //! \brief atomically subtract from the value and return the previous value
    T fetch_sub(T argument, std::memory_order = std::memory_order_seq_cst) {
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous - argument);
        return previous;
    }

    //! \brief atomically bitwise-and the value and return the previous value
    T fetch_and(T argument, std::memory_order = std::memory_order_seq_cst) {
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous & argument);
        return previous;
    }

    //! \brief atomically bitwise-or the value and return the previous value
    T fetch_or(T argument, std::memory_order = std::memory_order_seq_cst) {
        Os::ScopeLock lock(this->m_mutex);
        const T previous = this->m_value;
        this->m_value = static_cast<T>(previous | argument);
        return previous;
    }

    //! \brief atomically bitwise-xor the value and return the previous value
    T fetch_xor(T argument, std::memory_order = std::memory_order_seq_cst) {
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
//! read-modify-write; they do not decompose into a separate load and store. They are defined for the
//! integral types only: `Atomic<T*>` and `Atomic<bool>` supply `load`, `store`, `exchange` and the
//! compare-exchange operations, and instantiating an arithmetic or bitwise operator on such a type is a
//! compile error.
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

    //! \brief atomically add to the value
    //! \return the new value, matching `std::atomic`
    T operator+=(T argument) {
        assertNotBool();
        return static_cast<T>(this->fetch_add(argument) + argument);
    }

    //! \brief atomically subtract from the value
    //! \return the new value, matching `std::atomic`
    T operator-=(T argument) {
        assertNotBool();
        return static_cast<T>(this->fetch_sub(argument) - argument);
    }

    //! \brief atomically bitwise-and the value
    //! \return the new value, matching `std::atomic`
    T operator&=(T argument) {
        assertNotBool();
        return static_cast<T>(this->fetch_and(argument) & argument);
    }

    //! \brief atomically bitwise-or the value
    //! \return the new value, matching `std::atomic`
    T operator|=(T argument) {
        assertNotBool();
        return static_cast<T>(this->fetch_or(argument) | argument);
    }

    //! \brief atomically bitwise-xor the value
    //! \return the new value, matching `std::atomic`
    T operator^=(T argument) {
        assertNotBool();
        return static_cast<T>(this->fetch_xor(argument) ^ argument);
    }

    //! \brief atomically increment the value
    //! \return the new value
    T operator++() {
        assertNotBool();
        return static_cast<T>(this->fetch_add(static_cast<T>(1)) + static_cast<T>(1));
    }

    //! \brief atomically increment the value
    //! \return the previous value
    T operator++(int) {
        assertNotBool();
        return this->fetch_add(static_cast<T>(1));
    }

    //! \brief atomically decrement the value
    //! \return the new value
    T operator--() {
        assertNotBool();
        return static_cast<T>(this->fetch_sub(static_cast<T>(1)) - static_cast<T>(1));
    }

    //! \brief atomically decrement the value
    //! \return the previous value
    T operator--(int) {
        assertNotBool();
        return this->fetch_sub(static_cast<T>(1));
    }

  private:
    //! \brief block the arithmetic/bitwise operators for `bool`
    //!
    //! `bool` supports `+`, `&`, `|`, `^` etc. via integral promotion, so unlike `Atomic<T*>` (where pointer
    //! arithmetic naturally fails to compile against these operators' signatures), nothing stops
    //! `MutexBackend<bool>::fetch_add` and friends from compiling. `LockFreeBackend<bool>` is safe without
    //! help, because `std::atomic<bool>` has no `fetch_*` members at all -- but that protection would vanish
    //! for a project that forces `Atomic<bool, true>`. This assertion is only evaluated when one of these
    //! operators is actually instantiated (i.e. called), so `Atomic<bool>` remains usable via `load`/`store`/
    //! `exchange`/`compare_exchange_*` exactly as documented.
    static void assertNotBool() {
        static_assert(!std::is_same<T, bool>::value,
                      "Utils::Atomic<bool> does not support arithmetic or bitwise operators; use load()/store()/"
                      "exchange()/compare_exchange_*() instead");
    }
};

}  // namespace Utils

#endif  // UTILS_ATOMIC_HPP
