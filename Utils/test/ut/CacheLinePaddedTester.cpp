// ======================================================================
// \title  CacheLinePaddedTester.cpp
// \brief  cpp file for Utils::CacheLinePadded test harness implementation class
//
// \copyright
// Copyright 2026, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#include "CacheLinePaddedTester.hpp"

#include <Os/Task.hpp>
#include <Utils/Atomic.hpp>
#include <cstdint>
#include <type_traits>

namespace Utils {

namespace {

//! Line size used throughout these tests; small enough to keep the fixtures cheap, and distinct from
//! CACHE_LINE_PADDED_DEFAULT_LINE_SIZE so a bug that ignores the template parameter is still caught
constexpr FwSizeType TEST_LINE_SIZE = 32;

//! A plain, ordinarily copyable and movable struct, standing in for "most T"
struct MovableValue {
    I32 tag;
    MovableValue() : tag(0) {}
    explicit MovableValue(I32 t) : tag(t) {}
};

//! A type with no default constructor at all, standing in for a T that mandates construction arguments
struct NoDefaultValue {
    I32 tag;
    explicit NoDefaultValue(I32 t) : tag(t) {}
};

//! A plain aggregate (no user-declared constructor of any kind), to check zero-initialization independent
//! of a class type supplying its own default constructor
struct Aggregate {
    U32 a;
    U16 b;
};

//! Two independently-updated padded counters, declared adjacently like a real hot-path use site
struct AdjacentCounters {
    CacheLinePadded<Atomic<U32>, TEST_LINE_SIZE> first{0};
    CacheLinePadded<Atomic<U32>, TEST_LINE_SIZE> second{0};
};

//! Number of tasks contending for each counter in the concurrency test
constexpr U32 CONCURRENT_TASK_COUNT = 4;
//! Number of increments each task performs
constexpr U32 CONCURRENT_ITERATIONS = 10000;

//! Context shared by the tasks of the concurrency test
struct CounterContext {
    AdjacentCounters* counters;
    U32 iterations;
};

//! One task hammers `first`, the other hammers `second`, exercising the same struct concurrently
void counterTaskRoutine(void* pointer) {
    CounterContext* context = static_cast<CounterContext*>(pointer);
    for (U32 i = 0; i < context->iterations; i++) {
        context->counters->first.get()++;
        context->counters->second.get() += 2;
    }
}

}  // namespace

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

CacheLinePaddedTester ::CacheLinePaddedTester() {}

CacheLinePaddedTester ::~CacheLinePaddedTester() {}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void CacheLinePaddedTester ::testDefaultConstruction() {
    // a raw scalar T (not itself a class with its own zeroing default constructor) must still be
    // value-initialized, not left indeterminate
    CacheLinePadded<U32, TEST_LINE_SIZE> scalar;
    ASSERT_EQ(scalar.get(), 0u);

    // a plain aggregate, with no user-declared default constructor of its own, is zero-initialized too
    CacheLinePadded<Aggregate, TEST_LINE_SIZE> aggregate;
    ASSERT_EQ(aggregate.get().a, 0u);
    ASSERT_EQ(aggregate.get().b, 0u);

    // wrapping Utils::Atomic defers to Atomic's own default constructor, which is itself guaranteed to
    // zero-initialize (see AtomicTester::testLoadStore)
    CacheLinePadded<Atomic<U32>, TEST_LINE_SIZE> atomic;
    ASSERT_EQ(atomic.get().load(), 0u);

    static_assert(std::is_default_constructible<CacheLinePadded<MovableValue, TEST_LINE_SIZE>>::value,
                  "must stay default constructible when T is");

    // Declaring CacheLinePadded<T> for a T with no default constructor is fine on its own (the bare
    // default constructor's body is only instantiated if actually called); the forwarding constructor
    // remains the only usable one. Note there is deliberately no
    // `static_assert(!is_default_constructible<CacheLinePadded<NoDefaultValue>>::value)` here: that trait
    // reports a false positive for this class shape (confirmed separately, not checked in, since this
    // codebase has no negative-compile-test infrastructure) -- it only checks this constructor's
    // declaration, not whether its dependent body would actually instantiate, so it cannot see the
    // failure that `CacheLinePadded<NoDefaultValue> x;` produces if actually written.
    CacheLinePadded<NoDefaultValue, TEST_LINE_SIZE> noDefault(NoDefaultValue(3));
    ASSERT_EQ(noDefault.get().tag, 3);
}

void CacheLinePaddedTester ::testAlignmentAndSize() {
    using Padded = CacheLinePadded<Atomic<U32>, TEST_LINE_SIZE>;

    ASSERT_EQ(alignof(Padded), TEST_LINE_SIZE);
    ASSERT_EQ(sizeof(Padded) % TEST_LINE_SIZE, static_cast<FwSizeType>(0))
        << "sizeof must be a whole multiple of the line size so the next member/element is pushed clear";

    // a type wider than the line size still rounds up to the next whole multiple
    struct Wide {
        U8 bytes[TEST_LINE_SIZE + 1];
    };
    using WidePadded = CacheLinePadded<Wide, TEST_LINE_SIZE>;
    ASSERT_EQ(sizeof(WidePadded) % TEST_LINE_SIZE, static_cast<FwSizeType>(0));
    ASSERT_GE(sizeof(WidePadded), sizeof(Wide));
}

void CacheLinePaddedTester ::testAccess() {
    CacheLinePadded<Atomic<U32>, TEST_LINE_SIZE> padded(10);

    // get() gives full, transparent access to the wrapped Atomic, including its operators
    ASSERT_EQ(padded.get().load(), 10u);
    padded.get() += 5;
    ASSERT_EQ(padded.get().load(), 15u);
    padded.get()++;
    ASSERT_EQ(padded.get().load(), 16u);

    // operator-> reaches the wrapped value's members directly
    ASSERT_EQ(padded->load(), 16u);
}

void CacheLinePaddedTester ::testSeparation() {
    AdjacentCounters counters;
    const auto addrFirst = reinterpret_cast<uintptr_t>(&counters.first.get());
    const auto addrSecond = reinterpret_cast<uintptr_t>(&counters.second.get());

    ASSERT_NE(addrFirst / TEST_LINE_SIZE, addrSecond / TEST_LINE_SIZE)
        << "adjacent padded members must not land in the same line";
    ASSERT_GE(addrSecond - addrFirst, TEST_LINE_SIZE);

    // an array of padded elements stays separated element to element too
    CacheLinePadded<Atomic<U32>, TEST_LINE_SIZE> elements[3];
    for (FwSizeType i = 1; i < 3; i++) {
        const auto previous = reinterpret_cast<uintptr_t>(&elements[i - 1].get());
        const auto current = reinterpret_cast<uintptr_t>(&elements[i].get());
        ASSERT_GE(current - previous, TEST_LINE_SIZE);
    }
}

void CacheLinePaddedTester ::testConcurrentAccess() {
    AdjacentCounters counters;
    CounterContext context = {&counters, CONCURRENT_ITERATIONS};

    Os::Task tasks[CONCURRENT_TASK_COUNT];
    for (U32 i = 0; i < CONCURRENT_TASK_COUNT; i++) {
        Os::Task::Arguments arguments(Os::TaskString("CacheLinePaddedUt"), counterTaskRoutine, &context);
        ASSERT_EQ(Os::Task::Status::OP_OK, tasks[i].start(arguments));
    }
    for (U32 i = 0; i < CONCURRENT_TASK_COUNT; i++) {
        ASSERT_EQ(Os::Task::Status::OP_OK, tasks[i].join());
    }

    // no update lost on either counter, despite both living in the same struct
    ASSERT_EQ(counters.first.get().load(), CONCURRENT_TASK_COUNT * CONCURRENT_ITERATIONS);
    ASSERT_EQ(counters.second.get().load(), CONCURRENT_TASK_COUNT * CONCURRENT_ITERATIONS * 2);
}

void CacheLinePaddedTester ::testCopyMoveDefersToT() {
    using PaddedMovable = CacheLinePadded<MovableValue, TEST_LINE_SIZE>;
    using PaddedAtomic = CacheLinePadded<Atomic<U32>, TEST_LINE_SIZE>;

    // wrapping a plain copyable/movable T keeps the wrapper copyable/movable
    static_assert(std::is_copy_constructible<PaddedMovable>::value, "must stay copy constructible");
    static_assert(std::is_move_constructible<PaddedMovable>::value, "must stay move constructible");
    static_assert(std::is_copy_assignable<PaddedMovable>::value, "must stay copy assignable");
    static_assert(std::is_move_assignable<PaddedMovable>::value, "must stay move assignable");

    // wrapping Utils::Atomic, which is deliberately neither, makes the wrapper neither, automatically
    static_assert(!std::is_copy_constructible<PaddedAtomic>::value, "must stay non-copy-constructible");
    static_assert(!std::is_move_constructible<PaddedAtomic>::value, "must stay non-move-constructible");
    static_assert(!std::is_copy_assignable<PaddedAtomic>::value, "must stay non-copy-assignable");
    static_assert(!std::is_move_assignable<PaddedAtomic>::value, "must stay non-move-assignable");

    // and it actually works, correctly, through the real (not forwarding-constructor-hijacked) copy/move
    PaddedMovable source(5);
    PaddedMovable moved(std::move(source));
    ASSERT_EQ(moved.get().tag, 5);

    PaddedMovable a(1);
    PaddedMovable b(2);
    b = a;  // copy assignment
    ASSERT_EQ(b.get().tag, 1);

    PaddedMovable c(3);
    PaddedMovable d(4);
    d = std::move(c);  // move assignment
    ASSERT_EQ(d.get().tag, 3);
}

}  // namespace Utils
