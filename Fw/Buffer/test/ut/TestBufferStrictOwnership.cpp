// ======================================================================
// \title  TestBufferStrictOwnership.cpp
// \brief  unit tests for Fw::Buffer built with FW_BUFFER_STRICT_OWNERSHIP enabled
//
// Strict ownership is a compile-time configuration, so it needs its own test executable: this file is compiled
// together with Buffer.cpp and FW_BUFFER_STRICT_OWNERSHIP=1. See Fw/Buffer/CMakeLists.txt.
// ======================================================================
#include <gtest/gtest.h>
#include <Fw/FPrimeBasicTypes.hpp>
#include <type_traits>
#include <utility>
#include "Fw/Buffer/Buffer.hpp"

static_assert(FW_BUFFER_STRICT_OWNERSHIP,
              "This test executable is only meaningful with FW_BUFFER_STRICT_OWNERSHIP enabled");

// The whole point of strict ownership: a buffer can be handed on, but never duplicated
static_assert(not std::is_copy_constructible<Fw::Buffer>::value, "Fw::Buffer must not be copy constructible");
static_assert(not std::is_copy_assignable<Fw::Buffer>::value, "Fw::Buffer must not be copy assignable");
static_assert(std::is_move_constructible<Fw::Buffer>::value, "Fw::Buffer must be move constructible");
static_assert(std::is_move_assignable<Fw::Buffer>::value, "Fw::Buffer must be move assignable");

namespace {

//! Backing memory for buffers under test. Nothing frees it: Fw::Buffer only ever refers to memory owned elsewhere.
U8 g_data[64];

}  // namespace

// A buffer that never took hold of anything is safe to destroy
TEST(StrictOwnership, DefaultConstructedBufferIsSafeToDestroy) {
    Fw::Buffer buffer;
    ASSERT_FALSE(buffer.isValid());
    // Destruction at end of scope must not assert
}

// Destroying a buffer that still refers to data is the leak this configuration exists to catch
TEST(StrictOwnership, DestroyingHeldBufferAsserts) {
    ASSERT_DEATH_IF_SUPPORTED(
        {
            Fw::Buffer buffer(g_data, sizeof(g_data), 1234);
            (void)buffer.getSize();
        },
        "");
}

// release() is how a buffer states it is no longer responsible for the memory
TEST(StrictOwnership, ReleaseMakesBufferSafeToDestroy) {
    Fw::Buffer buffer(g_data, sizeof(g_data), 1234);
    ASSERT_TRUE(buffer.isValid());
    buffer.release();
    ASSERT_FALSE(buffer.isValid());
    ASSERT_EQ(buffer.getOriginalData(), nullptr);
    ASSERT_EQ(buffer.getSize(), 0);
    ASSERT_EQ(buffer.getCapacity(), 0);
    ASSERT_EQ(buffer.getContext(), Fw::Buffer::NO_CONTEXT);
    // Destruction at end of scope must not assert
}

// Moving hands the data over: the destination must be released, the source must not
TEST(StrictOwnership, MoveConstructionTransfersResponsibility) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    Fw::Buffer destination(std::move(source));

    ASSERT_TRUE(destination.isValid());
    ASSERT_EQ(destination.getOriginalData(), g_data);
    ASSERT_EQ(destination.getContext(), 1234);
    ASSERT_FALSE(source.isValid());

    destination.release();
    // Both destroyed at end of scope: source was emptied by the move, destination by release()
}

TEST(StrictOwnership, MoveAssignmentTransfersResponsibility) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    Fw::Buffer destination;
    destination = std::move(source);

    ASSERT_TRUE(destination.isValid());
    ASSERT_EQ(destination.getOriginalData(), g_data);
    ASSERT_FALSE(source.isValid());

    destination.release();
}

// A moved-from buffer is emptied, not poisoned: it can take hold of memory again
TEST(StrictOwnership, MovedFromBufferIsReusable) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    Fw::Buffer destination(std::move(source));

    source.set(g_data, 8, 5678);
    ASSERT_TRUE(source.isValid());
    ASSERT_EQ(source.getSize(), 8);

    source.release();
    destination.release();
}

// Self-move must not empty the buffer out from under its only owner
TEST(StrictOwnership, SelfMoveAssignmentKeepsTheBuffer) {
    Fw::Buffer buffer(g_data, sizeof(g_data), 1234);
    // Assign through an alias so this is a genuine self-move rather than a diagnosable `x = std::move(x)`
    Fw::Buffer* alias = &buffer;
    buffer = std::move(*alias);

    ASSERT_TRUE(buffer.isValid());
    ASSERT_EQ(buffer.getOriginalData(), g_data);
    ASSERT_EQ(buffer.getContext(), 1234);

    buffer.release();
}

// Handing a buffer on down a chain of owners leaves exactly one of them holding it
TEST(StrictOwnership, ChainedMovesLeaveOneOwner) {
    Fw::Buffer first(g_data, sizeof(g_data), 1234);
    Fw::Buffer second(std::move(first));
    Fw::Buffer third;
    third = std::move(second);

    ASSERT_FALSE(first.isValid());
    ASSERT_FALSE(second.isValid());
    ASSERT_TRUE(third.isValid());
    ASSERT_EQ(third.getOriginalData(), g_data);

    third.release();
}

// Moving preserves the offset/size/capacity bookkeeping that identifies the original allocation
TEST(StrictOwnership, MovePreservesOffsetAndCapacity) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    source.advance(7);

    Fw::Buffer destination(std::move(source));
    ASSERT_EQ(destination.getOriginalData(), g_data);
    ASSERT_EQ(destination.getData(), g_data + 7);
    ASSERT_EQ(destination.getOffset(), 7);
    ASSERT_EQ(destination.getSize(), sizeof(g_data) - 7);
    ASSERT_EQ(destination.getCapacity(), sizeof(g_data));

    destination.release();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
