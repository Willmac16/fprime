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

// An owning handle can be handed on, but never duplicated
static_assert(not std::is_copy_constructible<Fw::Buffer>::value, "Fw::Buffer must not be copy constructible");
static_assert(not std::is_copy_assignable<Fw::Buffer>::value, "Fw::Buffer must not be copy assignable");
static_assert(std::is_move_constructible<Fw::Buffer>::value, "Fw::Buffer must be move constructible");
static_assert(std::is_move_assignable<Fw::Buffer>::value, "Fw::Buffer must be move assignable");

// A view carries no responsibility, so duplicating one costs nothing and means nothing
static_assert(std::is_copy_constructible<Fw::BufferView>::value, "Fw::BufferView must be copy constructible");
static_assert(std::is_copy_assignable<Fw::BufferView>::value, "Fw::BufferView must be copy assignable");

// The two are distinct types: a view cannot stand in for a buffer anywhere one is wanted
static_assert(not std::is_convertible<Fw::BufferView, Fw::Buffer>::value,
              "Fw::BufferView must not convert to Fw::Buffer");
static_assert(not std::is_assignable<Fw::Buffer&, Fw::BufferView>::value,
              "Fw::BufferView must not be assignable to Fw::Buffer");

namespace {

//! Backing memory for buffers under test. Nothing frees it: Fw::Buffer only ever refers to memory owned elsewhere.
U8 g_data[64];

//! Stands in for the component answerable for g_data
//!
//! Creating and reclaiming owning handles is reachable only through Fw::BufferOwner, so the tests go through it the
//! same way production code has to: by being the manager.
class TestBufferOwner final : public Fw::BufferOwner {
  public:
    using Fw::BufferOwner::allocateBuffer;
    using Fw::BufferOwner::releaseBuffer;
};

TestBufferOwner g_owner;

}  // namespace

// A buffer that holds nothing is safe to destroy
TEST(StrictOwnership, EmptyBufferIsSafeToDestroy) {
    Fw::Buffer buffer;
    ASSERT_FALSE(buffer.isValid());
    // Destruction at end of scope must not assert
}

// Dropping an owning handle is the leak this configuration exists to catch
TEST(StrictOwnership, DestroyingHeldBufferAsserts) {
    ASSERT_DEATH_IF_SUPPORTED({ Fw::Buffer buffer = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234); }, "");
}

// A view carries no responsibility, so dropping one is not a leak however much memory it refers to. This is what
// makes the destructor check worth having: it never fires on a reference, so it never has to be silenced.
TEST(StrictOwnership, DroppingAViewIsSilent) {
    Fw::BufferView view(g_data, sizeof(g_data), 1234);
    ASSERT_TRUE(view.isValid());
    // Destruction at end of scope must not assert
}

// A view of an owned buffer is likewise free to come and go
TEST(StrictOwnership, ViewOfOwnedBufferIsSilent) {
    Fw::Buffer owner = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    {
        Fw::BufferView record = owner.alias();
        ASSERT_EQ(record.getOriginalData(), g_data);
        ASSERT_TRUE(owner == record);
        // record is destroyed here and must not assert
    }
    ASSERT_TRUE(owner.isValid());
    g_owner.releaseBuffer(owner);
}

// Views copy freely: a second reference is not a second owner, and there is no state to get wrong
TEST(StrictOwnership, ViewsCopyFreely) {
    Fw::BufferView first(g_data, sizeof(g_data), 1234);
    Fw::BufferView second = first;
    Fw::BufferView third;
    third = second;
    ASSERT_TRUE(first == second);
    ASSERT_TRUE(second == third);
}

// Taking a buffer back empties the handle the caller was holding: the use-after-free guard on the return path
TEST(StrictOwnership, ReleaseEmptiesTheHandle) {
    Fw::Buffer buffer = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    g_owner.releaseBuffer(buffer);

    ASSERT_FALSE(buffer.isValid());
    ASSERT_EQ(buffer.getOriginalData(), nullptr);
    ASSERT_EQ(buffer.getData(), nullptr);
    ASSERT_EQ(buffer.getSize(), 0);
    ASSERT_EQ(buffer.getCapacity(), 0);
    ASSERT_EQ(buffer.getContext(), Fw::Buffer::NO_CONTEXT);
    // Destruction at end of scope must not assert
}

// The serialization view follows the handle, so a reader built after the buffer went back reaches nothing
TEST(StrictOwnership, ReleasedBufferYieldsNoSerializer) {
    Fw::Buffer buffer = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    g_owner.releaseBuffer(buffer);

    auto serializer = buffer.getSerializer();
    ASSERT_EQ(serializer.getBuffAddr(), nullptr);
    ASSERT_EQ(serializer.getCapacity(), 0);
}

// Moving hands responsibility over along with the data
TEST(StrictOwnership, MoveTransfersResponsibility) {
    Fw::Buffer source = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    Fw::Buffer destination(Fw::move(source));

    ASSERT_TRUE(destination.isValid());
    ASSERT_EQ(destination.getOriginalData(), g_data);
    ASSERT_FALSE(source.isValid());

    g_owner.releaseBuffer(destination);
    // Source is destroyed at end of scope having given the allocation up
}

TEST(StrictOwnership, MoveAssignmentTransfersResponsibility) {
    Fw::Buffer source = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    Fw::Buffer destination;
    destination = Fw::move(source);

    ASSERT_TRUE(destination.isValid());
    ASSERT_FALSE(source.isValid());

    g_owner.releaseBuffer(destination);
}

// A moved-from buffer is emptied, not poisoned: its manager can hand it an allocation again
TEST(StrictOwnership, MovedFromBufferIsReusable) {
    Fw::Buffer source = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    Fw::Buffer destination(Fw::move(source));

    source = g_owner.allocateBuffer(g_data, 8, 5678);
    ASSERT_TRUE(source.isValid());
    ASSERT_EQ(source.getSize(), 8);

    g_owner.releaseBuffer(source);
    g_owner.releaseBuffer(destination);
}

// Self-move must not empty the buffer out from under its only owner
TEST(StrictOwnership, SelfMoveAssignmentKeepsTheBuffer) {
    Fw::Buffer buffer = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    // Assign through an alias so this is a genuine self-move rather than a directly diagnosable one
    Fw::Buffer* self = &buffer;
    buffer = Fw::move(*self);

    ASSERT_TRUE(buffer.isValid());
    ASSERT_EQ(buffer.getOriginalData(), g_data);

    g_owner.releaseBuffer(buffer);
}

// Handing a buffer down a chain leaves exactly one holder answerable for it
TEST(StrictOwnership, ChainedMovesLeaveOneOwner) {
    Fw::Buffer first = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    Fw::Buffer second(Fw::move(first));
    Fw::Buffer third;
    third = Fw::move(second);

    ASSERT_FALSE(first.isValid());
    ASSERT_FALSE(second.isValid());
    ASSERT_TRUE(third.isValid());

    g_owner.releaseBuffer(third);
}

// Re-pointing a buffer that still holds an allocation would drop it
TEST(StrictOwnership, ReWrappingHeldBufferAsserts) {
    ASSERT_DEATH_IF_SUPPORTED(
        {
            Fw::Buffer buffer = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
            buffer.set(g_data, 8, 5678);
        },
        "");
}

// A view keeps the offset and capacity bookkeeping that identifies the original allocation
TEST(StrictOwnership, ViewPreservesOffsetAndCapacity) {
    Fw::Buffer buffer = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    buffer.advance(7);

    Fw::BufferView view = buffer.alias();
    ASSERT_EQ(view.getOriginalData(), g_data);
    ASSERT_EQ(view.getData(), g_data + 7);
    ASSERT_EQ(view.getOffset(), 7);
    ASSERT_EQ(view.getSize(), sizeof(g_data) - 7);
    ASSERT_EQ(view.getCapacity(), sizeof(g_data));

    g_owner.releaseBuffer(buffer);
}

// Moving preserves that bookkeeping too
TEST(StrictOwnership, MovePreservesOffsetAndCapacity) {
    Fw::Buffer source = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
    source.advance(7);

    Fw::Buffer destination(Fw::move(source));
    ASSERT_EQ(destination.getOriginalData(), g_data);
    ASSERT_EQ(destination.getData(), g_data + 7);
    ASSERT_EQ(destination.getOffset(), 7);
    ASSERT_EQ(destination.getSize(), sizeof(g_data) - 7);
    ASSERT_EQ(destination.getCapacity(), sizeof(g_data));

    g_owner.releaseBuffer(destination);
}

// Serialization carries the descriptor only. An async port call serializes a buffer into a queue rather than taking
// it, so the far side reconstitutes a handle that is answerable for the allocation just as this one was.
TEST(StrictOwnership, DeserializedBufferIsAnOwningHandle) {
    U8 wire[Fw::Buffer::SERIALIZED_SIZE];
    Fw::ExternalSerializeBuffer serialized(wire, sizeof(wire));
    {
        Fw::Buffer source = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
        ASSERT_EQ(serialized.serializeFrom(source), Fw::FW_SERIALIZE_OK);
        g_owner.releaseBuffer(source);
    }
    Fw::Buffer received;
    ASSERT_EQ(serialized.deserializeTo(received), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(received.getOriginalData(), g_data);
    ASSERT_TRUE(received.isValid());

    g_owner.releaseBuffer(received);
}

// Dropping a buffer that arrived over the wire is a leak, and is caught. This is the async-hop case: generated
// dispatch deserializes into a local, hands it to the handler, and destroys it -- so a handler that neither moves
// the buffer on nor returns it is reported rather than silently losing the allocation.
TEST(StrictOwnership, DroppingADeserializedBufferAsserts) {
    U8 wire[Fw::Buffer::SERIALIZED_SIZE];
    Fw::ExternalSerializeBuffer serialized(wire, sizeof(wire));
    {
        Fw::Buffer source = g_owner.allocateBuffer(g_data, sizeof(g_data), 1234);
        ASSERT_EQ(serialized.serializeFrom(source), Fw::FW_SERIALIZE_OK);
        g_owner.releaseBuffer(source);
    }
    ASSERT_DEATH_IF_SUPPORTED(
        {
            Fw::Buffer received;
            (void)serialized.deserializeTo(received);
        },
        "");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
