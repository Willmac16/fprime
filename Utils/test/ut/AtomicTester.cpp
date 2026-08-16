// ======================================================================
// \title  AtomicTester.cpp
// \brief  cpp file for Utils::Atomic test harness implementation class
//
// \copyright
// Copyright 2026, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include "AtomicTester.hpp"

#include <Os/Task.hpp>

namespace Utils {

namespace {

//! Number of tasks used by the concurrency test
constexpr U32 CONCURRENT_TASK_COUNT = 4;
//! Number of increment/decrement rounds each task performs
constexpr U32 CONCURRENT_ITERATIONS = 10000;

//! A type wider than any atomic instruction on any supported platform, so it always selects the
//! mutex-backed backend regardless of the host running the test
struct WideValue {
    U64 first;
    U64 second;

    bool operator==(const WideValue& other) const {
        return (this->first == other.first) && (this->second == other.second);
    }
};

// ----------------------------------------------------------------------
// Templated test bodies, run against both the lock-free and the mutex-backed backend
// ----------------------------------------------------------------------

template <typename AtomicType, typename T>
void checkLoadStore() {
    AtomicType defaulted;
    ASSERT_EQ(defaulted.load(), static_cast<T>(0)) << "default construction must zero the value";

    AtomicType value(static_cast<T>(7));
    ASSERT_EQ(value.load(), static_cast<T>(7));

    value.store(static_cast<T>(11));
    ASSERT_EQ(value.load(), static_cast<T>(11));
    ASSERT_EQ(static_cast<T>(value), static_cast<T>(11)) << "conversion operator must load the value";

    // assignment returns the assigned value, matching std::atomic
    ASSERT_EQ((value = static_cast<T>(3)), static_cast<T>(3));
    ASSERT_EQ(value.load(), static_cast<T>(3));

    // explicit memory orders are accepted by both backends
    value.store(static_cast<T>(5), std::memory_order_release);
    ASSERT_EQ(value.load(std::memory_order_acquire), static_cast<T>(5));
}

template <typename AtomicType, typename T>
void checkExchangeAndCompareExchange() {
    AtomicType value(static_cast<T>(4));
    ASSERT_EQ(value.exchange(static_cast<T>(9)), static_cast<T>(4)) << "exchange returns the previous value";
    ASSERT_EQ(value.load(), static_cast<T>(9));

    T expected = static_cast<T>(9);
    ASSERT_TRUE(value.compare_exchange_strong(expected, static_cast<T>(12)));
    ASSERT_EQ(value.load(), static_cast<T>(12));

    expected = static_cast<T>(9);
    ASSERT_FALSE(value.compare_exchange_strong(expected, static_cast<T>(20)));
    ASSERT_EQ(expected, static_cast<T>(12)) << "a failed compare-exchange reports the observed value";
    ASSERT_EQ(value.load(), static_cast<T>(12));

    expected = static_cast<T>(12);
    while (!value.compare_exchange_weak(expected, static_cast<T>(15))) {
        // compare_exchange_weak may fail spuriously; the loop is the documented usage
    }
    ASSERT_EQ(value.load(), static_cast<T>(15));
}

template <typename AtomicType, typename T>
void checkArithmeticOperators() {
    AtomicType value(static_cast<T>(10));

    // compound assignment returns the new value, matching std::atomic
    ASSERT_EQ(value += static_cast<T>(5), static_cast<T>(15));
    ASSERT_EQ(value.load(), static_cast<T>(15));
    ASSERT_EQ(value -= static_cast<T>(3), static_cast<T>(12));
    ASSERT_EQ(value.load(), static_cast<T>(12));

    ASSERT_EQ(++value, static_cast<T>(13)) << "pre-increment returns the new value";
    ASSERT_EQ(value++, static_cast<T>(13)) << "post-increment returns the previous value";
    ASSERT_EQ(value.load(), static_cast<T>(14));
    ASSERT_EQ(--value, static_cast<T>(13)) << "pre-decrement returns the new value";
    ASSERT_EQ(value--, static_cast<T>(13)) << "post-decrement returns the previous value";
    ASSERT_EQ(value.load(), static_cast<T>(12));

    // the fetch_ operations return the previous value
    ASSERT_EQ(value.fetch_add(static_cast<T>(4)), static_cast<T>(12));
    ASSERT_EQ(value.load(), static_cast<T>(16));
    ASSERT_EQ(value.fetch_sub(static_cast<T>(6)), static_cast<T>(16));
    ASSERT_EQ(value.load(), static_cast<T>(10));
}

template <typename AtomicType, typename T>
void checkBitwiseOperators() {
    AtomicType value(static_cast<T>(0x0F));

    ASSERT_EQ(value &= static_cast<T>(0x0C), static_cast<T>(0x0C));
    ASSERT_EQ(value |= static_cast<T>(0x11), static_cast<T>(0x1D));
    ASSERT_EQ(value ^= static_cast<T>(0x0F), static_cast<T>(0x12));
    ASSERT_EQ(value.load(), static_cast<T>(0x12));

    ASSERT_EQ(value.fetch_and(static_cast<T>(0x10)), static_cast<T>(0x12));
    ASSERT_EQ(value.fetch_or(static_cast<T>(0x01)), static_cast<T>(0x10));
    ASSERT_EQ(value.fetch_xor(static_cast<T>(0x11)), static_cast<T>(0x11));
    ASSERT_EQ(value.load(), static_cast<T>(0x00));
}

//! Context shared by the tasks of the concurrency test
template <typename AtomicType>
struct CounterContext {
    AtomicType* counter;
    U32 iterations;
};

//! Task routine performing read-modify-writes that net out to one increment per iteration
template <typename AtomicType>
void counterTaskRoutine(void* pointer) {
    CounterContext<AtomicType>* context = static_cast<CounterContext<AtomicType>*>(pointer);
    for (U32 i = 0; i < context->iterations; i++) {
        *(context->counter) += 3;
        (*(context->counter))++;
        *(context->counter) -= 3;
    }
}

template <typename AtomicType>
void checkConcurrentIncrement() {
    AtomicType counter(0);
    CounterContext<AtomicType> context = {&counter, CONCURRENT_ITERATIONS};

    Os::Task tasks[CONCURRENT_TASK_COUNT];
    for (U32 i = 0; i < CONCURRENT_TASK_COUNT; i++) {
        Os::Task::Arguments arguments(Os::TaskString("AtomicUt"), counterTaskRoutine<AtomicType>, &context);
        ASSERT_EQ(Os::Task::Status::OP_OK, tasks[i].start(arguments));
    }
    for (U32 i = 0; i < CONCURRENT_TASK_COUNT; i++) {
        ASSERT_EQ(Os::Task::Status::OP_OK, tasks[i].join());
    }

    // every increment must survive: a non-atomic counter loses updates here
    ASSERT_EQ(counter.load(), static_cast<U64>(CONCURRENT_TASK_COUNT) * static_cast<U64>(CONCURRENT_ITERATIONS));
}

}  // namespace

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

AtomicTester ::AtomicTester() {}

AtomicTester ::~AtomicTester() {}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void AtomicTester ::testBackendSelection() {
    // a type that is always lock-free takes the pass-through backend
    ASSERT_TRUE(AtomicIsLockFree<U8>::value);
    ASSERT_TRUE(Atomic<U8>::isLockFree());
    Atomic<U8> lockFree(0);
    ASSERT_TRUE(lockFree.is_lock_free());

    // a type wider than any atomic instruction always takes the mutex-backed backend
    ASSERT_FALSE(AtomicIsLockFree<WideValue>::value);
    ASSERT_FALSE(Atomic<WideValue>::isLockFree());
    Atomic<WideValue> wide;
    ASSERT_FALSE(wide.is_lock_free());

    // the backend may also be forced, which is how the tests below cover both paths on any host
    ASSERT_FALSE((Atomic<U8, true>::isLockFree()));
    Atomic<U8, true> forced(0);
    ASSERT_FALSE(forced.is_lock_free());
}

void AtomicTester ::testLoadStore() {
    checkLoadStore<Atomic<U32>, U32>();
    checkLoadStore<Atomic<U32, true>, U32>();
    checkLoadStore<Atomic<U8>, U8>();
    checkLoadStore<Atomic<U8, true>, U8>();
    checkLoadStore<Atomic<I16>, I16>();
    checkLoadStore<Atomic<I16, true>, I16>();
    checkLoadStore<Atomic<U64>, U64>();
    checkLoadStore<Atomic<U64, true>, U64>();
}

void AtomicTester ::testExchangeAndCompareExchange() {
    checkExchangeAndCompareExchange<Atomic<U32>, U32>();
    checkExchangeAndCompareExchange<Atomic<U32, true>, U32>();
    checkExchangeAndCompareExchange<Atomic<U64>, U64>();
    checkExchangeAndCompareExchange<Atomic<U64, true>, U64>();
    checkExchangeAndCompareExchange<Atomic<I32>, I32>();
    checkExchangeAndCompareExchange<Atomic<I32, true>, I32>();
}

void AtomicTester ::testArithmeticOperators() {
    checkArithmeticOperators<Atomic<U32>, U32>();
    checkArithmeticOperators<Atomic<U32, true>, U32>();
    checkArithmeticOperators<Atomic<U8>, U8>();
    checkArithmeticOperators<Atomic<U8, true>, U8>();
    checkArithmeticOperators<Atomic<I32>, I32>();
    checkArithmeticOperators<Atomic<I32, true>, I32>();
    checkArithmeticOperators<Atomic<U64>, U64>();
    checkArithmeticOperators<Atomic<U64, true>, U64>();
}

void AtomicTester ::testBitwiseOperators() {
    checkBitwiseOperators<Atomic<U32>, U32>();
    checkBitwiseOperators<Atomic<U32, true>, U32>();
    checkBitwiseOperators<Atomic<U8>, U8>();
    checkBitwiseOperators<Atomic<U8, true>, U8>();
    checkBitwiseOperators<Atomic<U64>, U64>();
    checkBitwiseOperators<Atomic<U64, true>, U64>();
}

void AtomicTester ::testNonLockFreeTypes() {
    // a type too wide for any atomic instruction is still fully usable through the mutex backend
    const WideValue initial = {1, 2};
    Atomic<WideValue> wide(initial);
    ASSERT_TRUE(wide.load() == initial);

    WideValue expected = initial;
    const WideValue next = {3, 4};
    ASSERT_TRUE(wide.compare_exchange_strong(expected, next));
    ASSERT_TRUE(wide.load() == next);

    expected = initial;
    ASSERT_FALSE(wide.compare_exchange_strong(expected, initial));
    ASSERT_TRUE(expected == next);

    const WideValue last = {5, 6};
    ASSERT_TRUE(wide.exchange(last) == next);
    ASSERT_TRUE(wide.load() == last);

    // bool and pointer types have no arithmetic operators but support the load/store/exchange set
    Atomic<bool> flag(false);
    ASSERT_FALSE(flag.load());
    ASSERT_FALSE(flag.exchange(true));
    ASSERT_TRUE(flag.load());

    U32 storage = 0;
    Atomic<U32*> pointer(nullptr);
    ASSERT_TRUE(pointer.exchange(&storage) == nullptr);
    ASSERT_TRUE(pointer.load() == &storage);
}

void AtomicTester ::testConcurrentIncrement() {
    checkConcurrentIncrement<Atomic<U64>>();
    checkConcurrentIncrement<Atomic<U64, true>>();
}

}  // namespace Utils
