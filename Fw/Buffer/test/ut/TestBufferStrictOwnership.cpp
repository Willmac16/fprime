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
#include "Fw/Buffer/Buffer.hpp"
#include "Fw/LanguageHelpers.hpp"

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

// A buffer that never claimed anything is safe to destroy
TEST(StrictOwnership, DefaultConstructedBufferIsSafeToDestroy) {
    Fw::Buffer buffer;
    ASSERT_FALSE(buffer.isValid());
    ASSERT_EQ(buffer.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
    // Destruction at end of scope must not assert
}

// Referring to memory is not the same as being answerable for it: an unclaimed buffer destroys silently
TEST(StrictOwnership, UnclaimedBufferHoldingDataIsSafeToDestroy) {
    Fw::Buffer buffer(g_data, sizeof(g_data), 1234);
    ASSERT_TRUE(buffer.isValid());
    ASSERT_EQ(buffer.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
    // Destruction at end of scope must not assert
}

// Dropping a buffer that was answerable for its allocation is the leak this configuration exists to catch
TEST(StrictOwnership, DestroyingOwnedBufferAsserts) {
    ASSERT_DEATH_IF_SUPPORTED(
        {
            Fw::Buffer buffer(g_data, sizeof(g_data), 1234);
            buffer.claim();
        },
        "");
}

// release() is how an owner states the allocation has been disposed of
TEST(StrictOwnership, ReleaseMakesOwnedBufferSafeToDestroy) {
    Fw::Buffer buffer(g_data, sizeof(g_data), 1234);
    buffer.claim();
    ASSERT_EQ(buffer.getOwnershipState(), Fw::Buffer::OwnershipState::OWNED);
    buffer.release();
    ASSERT_EQ(buffer.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
    // release() gives up the claim without disturbing what the buffer refers to
    ASSERT_EQ(buffer.getOriginalData(), g_data);
    ASSERT_EQ(buffer.getSize(), sizeof(g_data));
    ASSERT_EQ(buffer.getContext(), 1234);
    // Destruction at end of scope must not assert
}

// An alias is another reference, not another owner. This is what keeps the destructor check meaningful: if aliases
// had to be released too, release() would be a blanket silencer rather than a statement about disposal.
TEST(StrictOwnership, AliasOfOwnedBufferIsNotAnOwner) {
    Fw::Buffer owner(g_data, sizeof(g_data), 1234);
    owner.claim();
    {
        Fw::Buffer record = owner.alias();
        ASSERT_EQ(record.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
        ASSERT_EQ(record.getOriginalData(), g_data);
        // record is destroyed here, unreleased, and must not assert
    }
    // The owner is untouched by the alias coming and going
    ASSERT_EQ(owner.getOwnershipState(), Fw::Buffer::OwnershipState::OWNED);
    owner.release();
}

// Moving hands the claim over along with the data
TEST(StrictOwnership, MoveConstructionTransfersOwnership) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    source.claim();
    Fw::Buffer destination(Fw::move(source));

    ASSERT_EQ(destination.getOwnershipState(), Fw::Buffer::OwnershipState::OWNED);
    ASSERT_EQ(destination.getOriginalData(), g_data);
    ASSERT_EQ(destination.getContext(), 1234);
    ASSERT_EQ(source.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
    ASSERT_FALSE(source.isValid());

    destination.release();
    // Source is destroyed at end of scope having given up both the data and the claim
}

TEST(StrictOwnership, MoveAssignmentTransfersOwnership) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    source.claim();
    Fw::Buffer destination;
    destination = Fw::move(source);

    ASSERT_EQ(destination.getOwnershipState(), Fw::Buffer::OwnershipState::OWNED);
    ASSERT_EQ(source.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
    ASSERT_FALSE(source.isValid());

    destination.release();
}

// Moving an unclaimed buffer does not conjure ownership out of nothing
TEST(StrictOwnership, MovingUnclaimedBufferStaysUnowned) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    Fw::Buffer destination(Fw::move(source));
    ASSERT_EQ(destination.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
    ASSERT_TRUE(destination.isValid());
    // Neither is destroyed with a claim outstanding
}

// A moved-from buffer is emptied and disclaimed, not poisoned: it can take hold of memory again
TEST(StrictOwnership, MovedFromBufferIsReusable) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    source.claim();
    Fw::Buffer destination(Fw::move(source));

    source.set(g_data, 8, 5678);
    ASSERT_TRUE(source.isValid());
    ASSERT_EQ(source.getSize(), 8);
    ASSERT_EQ(source.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);

    destination.release();
}

// Self-move must not strip the buffer of its claim
TEST(StrictOwnership, SelfMoveAssignmentKeepsTheClaim) {
    Fw::Buffer buffer(g_data, sizeof(g_data), 1234);
    buffer.claim();
    // Assign through an alias so this is a genuine self-move rather than a directly diagnosable one
    Fw::Buffer* self = &buffer;
    buffer = Fw::move(*self);

    ASSERT_TRUE(buffer.isValid());
    ASSERT_EQ(buffer.getOriginalData(), g_data);
    ASSERT_EQ(buffer.getOwnershipState(), Fw::Buffer::OwnershipState::OWNED);

    buffer.release();
}

// Handing a buffer down a chain of owners leaves exactly one of them answerable for it
TEST(StrictOwnership, ChainedMovesLeaveOneOwner) {
    Fw::Buffer first(g_data, sizeof(g_data), 1234);
    first.claim();
    Fw::Buffer second(Fw::move(first));
    Fw::Buffer third;
    third = Fw::move(second);

    ASSERT_EQ(first.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
    ASSERT_EQ(second.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);
    ASSERT_EQ(third.getOwnershipState(), Fw::Buffer::OwnershipState::OWNED);
    ASSERT_EQ(third.getOriginalData(), g_data);

    third.release();
}

// Moving preserves the offset/size/capacity bookkeeping that identifies the original allocation
TEST(StrictOwnership, MovePreservesOffsetAndCapacity) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    source.advance(7);

    Fw::Buffer destination(Fw::move(source));
    ASSERT_EQ(destination.getOriginalData(), g_data);
    ASSERT_EQ(destination.getData(), g_data + 7);
    ASSERT_EQ(destination.getOffset(), 7);
    ASSERT_EQ(destination.getSize(), sizeof(g_data) - 7);
    ASSERT_EQ(destination.getCapacity(), sizeof(g_data));
}

// Pointing an owning buffer at different memory would drop the allocation it is answerable for
TEST(StrictOwnership, ReWrappingOwnedBufferAsserts) {
    ASSERT_DEATH_IF_SUPPORTED(
        {
            Fw::Buffer buffer(g_data, sizeof(g_data), 1234);
            buffer.claim();
            buffer.set(g_data, 8, 5678);
        },
        "");
}

// Ownership is a local property: it does not travel over the wire
TEST(StrictOwnership, DeserializedBufferIsNotAnOwner) {
    Fw::Buffer source(g_data, sizeof(g_data), 1234);
    source.claim();

    U8 wire[Fw::Buffer::SERIALIZED_SIZE];
    Fw::ExternalSerializeBuffer serialized(wire, sizeof(wire));
    ASSERT_EQ(serialized.serializeFrom(source), Fw::FW_SERIALIZE_OK);

    Fw::Buffer received;
    ASSERT_EQ(serialized.deserializeTo(received), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(received.getOriginalData(), g_data);
    ASSERT_EQ(received.getOwnershipState(), Fw::Buffer::OwnershipState::NOT_OWNED);

    source.release();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
