//
// Created by mstarch on 11/13/20.
//
#include <gtest/gtest.h>
#include <Fw/FPrimeBasicTypes.hpp>
#include <utility>
#include "Fw/Buffer/Buffer.hpp"
#include "Fw/Types/test/ut/LinearBufferBaseTester.hpp"

namespace Fw {
class BufferTester {
  public:
    // ----------------------------------------------------------------------
    // Construction and destruction
    // ----------------------------------------------------------------------
    BufferTester() {}

    ~BufferTester() {}

    // ----------------------------------------------------------------------
    // Tests
    // ----------------------------------------------------------------------
    void test_basic() {
        U8 data[100];
        U8 faux[100];
        Fw::Buffer buffer;
        // Check basic guarantees
        ASSERT_EQ(buffer.m_context, Fw::Buffer::NO_CONTEXT);
        buffer.set(data, sizeof(data));
        buffer.setContext(1234);
        ASSERT_EQ(buffer.getData(), data);
        ASSERT_EQ(buffer.getOriginalData(), data);
        ASSERT_EQ(buffer.getSize(), sizeof(data));
        ASSERT_EQ(buffer.getCapacity(), sizeof(data));
        ASSERT_EQ(buffer.getOffset(), 0);
        ASSERT_EQ(buffer.getContext(), 1234);

        // Test set method is equivalent
        Fw::Buffer buffer_set;
        buffer_set.set(data, sizeof(data), 1234);
        ASSERT_EQ(buffer_set, buffer);

        // Check constructors and assignments. alias() rather than a copy so this file also compiles under
        // FW_BUFFER_STRICT_OWNERSHIP, where the copy operations are deleted; the semantics under test are the same.
        Fw::Buffer buffer_new = buffer.alias();
        ASSERT_EQ(buffer_new.getData(), data);
        ASSERT_EQ(buffer_new.getSize(), sizeof(data));
        ASSERT_EQ(buffer_new.getContext(), 1234);
        ASSERT_EQ(buffer, buffer_new);

        // Creating empty buffer
        Fw::Buffer testBuffer(nullptr, 0);
        ASSERT_EQ(testBuffer.getData(), nullptr);
        ASSERT_EQ(testBuffer.getSize(), 0);

        // Assignment operator with transitivity
        Fw::Buffer buffer_assignment1, buffer_assignment2;
        ASSERT_NE(buffer_assignment1.getData(), data);
        ASSERT_NE(buffer_assignment1.getSize(), sizeof(data));
        ASSERT_NE(buffer_assignment1.getContext(), 1234);
        ASSERT_NE(buffer_assignment2.getData(), data);
        ASSERT_NE(buffer_assignment2.getSize(), sizeof(data));
        ASSERT_NE(buffer_assignment2.getContext(), 1234);
        buffer_assignment1 = (buffer_assignment2 = buffer.alias()).alias();
        ASSERT_EQ(buffer_assignment1.getData(), data);
        ASSERT_EQ(buffer_assignment1.getSize(), sizeof(data));
        ASSERT_EQ(buffer_assignment1.getContext(), 1234);
        ASSERT_EQ(buffer_assignment2.getData(), data);
        ASSERT_EQ(buffer_assignment2.getSize(), sizeof(data));
        ASSERT_EQ(buffer_assignment2.getContext(), 1234);

        // Check modifying the aliases does not destroy the original
        buffer_new.set(faux, 0);
        buffer_new.setContext(22222);
        buffer_assignment1.set(faux, 0);
        buffer_assignment1.setContext(22222);
        buffer_assignment2.set(faux, 0);
        buffer_assignment2.setContext(22222);

        ASSERT_EQ(buffer.getData(), data);
        ASSERT_EQ(buffer.getSize(), sizeof(data));
        ASSERT_EQ(buffer.getContext(), 1234);
    }

    void test_advance() {
        U8 data[100];
        Fw::Buffer buffer(data, sizeof(data), 1234);

        // Advance forward: offset grows, size shrinks, end fixed, original recoverable
        buffer.advance(10);
        ASSERT_EQ(buffer.getData(), data + 10);
        ASSERT_EQ(buffer.getOriginalData(), data);
        ASSERT_EQ(buffer.getOffset(), 10);
        ASSERT_EQ(buffer.getSize(), sizeof(data) - 10);
        ASSERT_EQ(buffer.getCapacity(), sizeof(data));

        // Advance backward restores
        buffer.advance(-10);
        ASSERT_EQ(buffer.getData(), data);
        ASSERT_EQ(buffer.getOffset(), 0);
        ASSERT_EQ(buffer.getSize(), sizeof(data));

        // setData within the original allocation updates the offset
        buffer.setData(data + 25);
        ASSERT_EQ(buffer.getData(), data + 25);
        ASSERT_EQ(buffer.getOriginalData(), data);
        ASSERT_EQ(buffer.getOffset(), 25);
        ASSERT_EQ(buffer.getSize(), sizeof(data) - 25);

        // setSize is checked against offset + capacity
        buffer.setSize(sizeof(data) - 25);
        ASSERT_EQ(buffer.getSize(), sizeof(data) - 25);

        // Aliases preserve offset and capacity
        Fw::Buffer copy = buffer.alias();
        ASSERT_EQ(copy.getOriginalData(), data);
        ASSERT_EQ(copy.getOffset(), 25);
        ASSERT_EQ(copy.getCapacity(), sizeof(data));
        ASSERT_EQ(copy, buffer);

        // Out-of-bounds operations assert
        U8* unrelated = new U8[100];
        ASSERT_DEATH_IF_SUPPORTED(buffer.setData(unrelated), "");
        delete[] unrelated;
        ASSERT_DEATH_IF_SUPPORTED(buffer.setSize(sizeof(data) - 25 + 1), "");
        ASSERT_DEATH_IF_SUPPORTED(buffer.advance(-26), "");
        ASSERT_DEATH_IF_SUPPORTED(buffer.advance(static_cast<FwSignedSizeType>(sizeof(data))), "");
    }

    void test_move() {
        U8 data[100];
        Fw::Buffer buffer(data, sizeof(data), 1234);
        buffer.advance(7);

        // Move construction hands the wrapped data over wholesale
        Fw::Buffer moved(std::move(buffer));
        ASSERT_EQ(moved.getOriginalData(), data);
        ASSERT_EQ(moved.getData(), data + 7);
        ASSERT_EQ(moved.getOffset(), 7);
        ASSERT_EQ(moved.getSize(), sizeof(data) - 7);
        ASSERT_EQ(moved.getCapacity(), sizeof(data));
        ASSERT_EQ(moved.getContext(), 1234);

        // ... and leaves the source referring to nothing, so it cannot be used to return the same allocation twice
        ASSERT_FALSE(buffer.isValid());
        ASSERT_EQ(buffer.getOriginalData(), nullptr);
        ASSERT_EQ(buffer.getData(), nullptr);
        ASSERT_EQ(buffer.getOffset(), 0);
        ASSERT_EQ(buffer.getSize(), 0);
        ASSERT_EQ(buffer.getCapacity(), 0);
        ASSERT_EQ(buffer.getContext(), Fw::Buffer::NO_CONTEXT);

        // The moved-to buffer's serialization representation follows the data it now wraps
        auto serializer = moved.getSerializer();
        ASSERT_EQ(serializer.getBuffAddr(), data + 7);
        ASSERT_EQ(serializer.getCapacity(), sizeof(data) - 7);

        // Move assignment behaves the same way
        Fw::Buffer destination;
        destination = std::move(moved);
        ASSERT_EQ(destination.getOriginalData(), data);
        ASSERT_EQ(destination.getData(), data + 7);
        ASSERT_EQ(destination.getOffset(), 7);
        ASSERT_EQ(destination.getSize(), sizeof(data) - 7);
        ASSERT_EQ(destination.getCapacity(), sizeof(data));
        ASSERT_EQ(destination.getContext(), 1234);
        ASSERT_FALSE(moved.isValid());
        ASSERT_EQ(moved.getOriginalData(), nullptr);
        ASSERT_EQ(moved.getContext(), Fw::Buffer::NO_CONTEXT);

        // A moved-from buffer is not poisoned: it can wrap data again
        U8 other[10];
        moved.set(other, sizeof(other), 5678);
        ASSERT_TRUE(moved.isValid());
        ASSERT_EQ(moved.getData(), other);
        ASSERT_EQ(destination.getOriginalData(), data);

        // Self-move-assignment leaves the buffer untouched rather than clearing it
        Fw::Buffer* alias = &destination;
        destination = std::move(*alias);
        ASSERT_EQ(destination.getOriginalData(), data);
        ASSERT_EQ(destination.getOffset(), 7);
        ASSERT_EQ(destination.getSize(), sizeof(data) - 7);
        ASSERT_EQ(destination.getContext(), 1234);

        // Moving an empty buffer is well-defined: both ends up empty
        Fw::Buffer empty;
        Fw::Buffer empty_destination(std::move(empty));
        ASSERT_FALSE(empty.isValid());
        ASSERT_FALSE(empty_destination.isValid());
        ASSERT_EQ(empty_destination.getContext(), Fw::Buffer::NO_CONTEXT);

        // Move-assigning an empty buffer over a valid one drops the valid one's data
        empty_destination = std::move(destination);
        ASSERT_TRUE(empty_destination.isValid());
        empty_destination = std::move(empty);
        ASSERT_FALSE(empty_destination.isValid());
        ASSERT_EQ(empty_destination.getOriginalData(), nullptr);
    }

    void test_representations() {
        U8 data[100];
        Fw::Buffer buffer(data, sizeof(data), 1234);

        // Test serialization and that it stops before overflowing
        auto serializer = buffer.getSerializer();
        for (U32 i = 0; i < sizeof(data) / 4; i++) {
            ASSERT_EQ(serializer.serializeFrom(i), Fw::FW_SERIALIZE_OK);
        }
        Fw::SerializeStatus stat = serializer.serializeFrom(100);
        ASSERT_NE(stat, Fw::FW_SERIALIZE_OK);

        // And that another call to repr resets it
        serializer.resetSer();
        ASSERT_EQ(serializer.serializeFrom(0), Fw::FW_SERIALIZE_OK);

        // Now deserialize all the things
        auto deserializer = buffer.getDeserializer();
        U32 out;
        for (U32 i = 0; i < sizeof(data) / 4; i++) {
            ASSERT_EQ(deserializer.deserializeTo(out), Fw::FW_SERIALIZE_OK);
            ASSERT_EQ(i, out);
        }
        ASSERT_NE(deserializer.deserializeTo(out), Fw::FW_SERIALIZE_OK);
        deserializer.setBuffLen(buffer.getSize());
        ASSERT_EQ(deserializer.deserializeTo(out), Fw::FW_SERIALIZE_OK);
        ASSERT_EQ(0, out);
    }

    void test_serialization() {
        U8 data[100];
        U8 wire[100];

        Fw::Buffer buffer(data, sizeof(data), 1234);
        buffer.advance(7);

        Fw::ExternalSerializeBuffer externalSerializeBuffer(wire, sizeof(wire));
        externalSerializeBuffer.serializeFrom(buffer);
        Fw::LinearBufferBaseTester::verifySerLocLT(externalSerializeBuffer, sizeof(data));

        Fw::Buffer buffer_new;
        externalSerializeBuffer.deserializeTo(buffer_new);
        ASSERT_EQ(buffer_new, buffer);
        ASSERT_EQ(buffer_new.getOriginalData(), data);
        ASSERT_EQ(buffer_new.getData(), data + 7);
        ASSERT_EQ(buffer_new.getOffset(), 7);
        ASSERT_EQ(buffer_new.getCapacity(), sizeof(data));
    }
};
}  // namespace Fw

TEST(Nominal, BasicBuffer) {
    Fw::BufferTester tester;
    tester.test_basic();
}

TEST(Nominal, Advance) {
    Fw::BufferTester tester;
    tester.test_advance();
}

TEST(Nominal, Move) {
    Fw::BufferTester tester;
    tester.test_move();
}

TEST(Nominal, Representations) {
    Fw::BufferTester tester;
    tester.test_representations();
}

TEST(Nominal, Serialization) {
    Fw::BufferTester tester;
    tester.test_serialization();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
