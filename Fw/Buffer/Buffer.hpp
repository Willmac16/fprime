// ======================================================================
// \title  Buffer.hpp
// \author mstarch
// \brief  hpp file for Fw::Buffer definition
//
// \copyright
// Copyright 2009-2020, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================
#ifndef BUFFER_HPP_
#define BUFFER_HPP_

#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Types/Serializable.hpp>
#if FW_SERIALIZABLE_TO_STRING
#include <Fw/Types/StringType.hpp>
#ifdef BUILD_UT
#include <Fw/Types/String.hpp>
#include <iostream>
#endif
#endif

// Forward declaration for UTs
namespace Fw {
class BufferTester;
class BufferOwner;
}  // namespace Fw

namespace Fw {

//! Buffer used for wrapping pointer to data for efficient transmission
//!
//! Fw::Buffer is a wrapper for a pointer to data. It allows for data to be passed around the system without a copy of
//! the data itself. However, it comes with the expectation that the user maintain and protect this memory as it moves
//! about the system until such a time as it is returned.
//!
//! Fw::Buffer is composed of several elements: a U8* pointer to the original allocation, an offset into that
//! allocation, a size of the represented data, a capacity recording the original allocation size, and a U32 context
//! describing the origin of that data, such that it may be freed at some later point. The default context of 0xFFFFFFFF
//! should not be used for tracking purposes, as it represents a context-free buffer.
//!
//! The original allocation pointer is always recoverable via getOriginalData(). Consuming leading bytes (e.g. a
//! header) is done with advance(), which adjusts the offset and size without losing the original pointer. setData()
//! and setSize() are bounds-checked against the original allocation: to wrap unrelated memory, construct a new
//! Fw::Buffer or call set().
//!
//! Fw::Buffer also comes with functions to return a representation of the data as a LinearBufferBase. These two
//! functions allow easy access to the data as if it were a serialize or deserialize buffer. This can aid in writing and
//! reading the wrapped data whereas the standard serialize and deserialize methods treat the data as a pointer to
//! prevent excessive copying.
//!
class Buffer : public Fw::Serializable {
    friend class Fw::BufferTester;
    // Grants and revokes ownership on behalf of whoever is answerable for the memory. See Fw::BufferOwner.
    friend class Fw::BufferOwner;

  public:
    //! Buffer ownership state
    //!
    //! Records whether this particular Fw::Buffer is the one responsible for returning the memory it wraps. Several
    //! Fw::Buffer objects may refer to the same allocation -- see `alias()` -- but at most one of them should be
    //! OWNED, and that one is answerable for the allocation.
    //!
    //! A buffer is NOT_OWNED unless `claim()` says otherwise. Moving a buffer carries the state to the destination;
    //! copying or aliasing one does not, because the result is another reference, not another owner.
    enum class OwnershipState {
        NOT_OWNED,  //!< The buffer is currently not owned
        OWNED,      //!< The buffer is currently owned
    };

  public:
    //! The size type for a buffer - for backwards compatibility
    using SizeType = FwSizeType;

    enum {
        SERIALIZED_SIZE = 3 * sizeof(SizeType) + sizeof(U32) + sizeof(U8*),  //!< Size of Fw::Buffer when serialized
        NO_CONTEXT = 0xFFFFFFFF                                              //!< Value representing no context
    };

    //! Construct a buffer with no context nor data
    //!
    //! Constructs a buffer setting the context to the default no-context value of 0xffffffff. In addition, the size
    //! and data pointers are zeroed-out.
    Buffer();

#if FW_BUFFER_STRICT_OWNERSHIP
    //! Copy construction is disabled: a buffer may only be handed on by moving it
    //!
    //! See FW_BUFFER_STRICT_OWNERSHIP in FpConfig.h. Take a `const Buffer&` to inspect a buffer without claiming it,
    //! `Fw::move` to hand it on, and `alias()` where a second reference is genuinely wanted.
    Buffer(const Buffer& src) = delete;
#else
    //! Construct a buffer by copying members from a reference to another buffer. Does not copy wrapped data.
    //!
    Buffer(const Buffer& src);
#endif

    //! Construct a buffer by transferring the wrapped data from another buffer
    //!
    //! Move construction transfers the wrapped data, offset, size, capacity, and context from `src` into the newly
    //! constructed buffer. `src` is left in the default-constructed state: a null data pointer, zero offset, size,
    //! and capacity, and the no-context value of 0xFFFFFFFF.
    //!
    //! Fw::Buffer does not free the memory it wraps, so a move does not release anything. What it does do is make
    //! the transfer of *responsibility* for that memory explicit: after the move only the destination refers to
    //! the wrapped data, so the moved-from buffer cannot be used to return or re-send the same allocation. Prefer
    //! a move over a copy anywhere a buffer is handed off for good -- stored into a member for later return, placed
    //! in a queue, or returned from a function -- and reserve copies for cases where both buffers must stay valid.
    //!
    //! \param src: buffer to transfer the wrapped data from
    Buffer(Buffer&& src);

    //! Construct a buffer to wrap the given data pointer of given size
    //!
    //! Wraps the given data pointer with given size in a buffer. The context by default is set to NO_CONTEXT but can
    //! be set to specify a specific context.
    //! \param data: data pointer to wrap
    //! \param size: size of data located at data pointer
    //! \param context: user-specified context to track creation. Default: no context
    Buffer(U8* data, FwSizeType size, U32 context = NO_CONTEXT);

#if FW_BUFFER_STRICT_OWNERSHIP
    //! Copy assignment is disabled: a buffer may only be handed on by moving it
    //!
    //! See FW_BUFFER_STRICT_OWNERSHIP in FpConfig.h.
    Buffer& operator=(const Buffer& src) = delete;
#else
    //! Assignment operator to set given buffer's members from another without copying wrapped data
    //!
    Buffer& operator=(const Buffer& src);
#endif

    //! Move assignment operator transferring the wrapped data from another buffer
    //!
    //! Transfers the wrapped data, offset, size, capacity, and context from `src` into this buffer, leaving `src`
    //! in the default-constructed state. See the move constructor for the ownership rationale. Self-move-assignment
    //! is a no-op and leaves this buffer unchanged.
    //!
    //! \param src: buffer to transfer the wrapped data from
    //! \return reference to this buffer
    Buffer& operator=(Buffer&& src);

    //! Destroy this buffer
    //!
    //! When FW_BUFFER_STRICT_OWNERSHIP is enabled, destroying an OWNED buffer is a programming error and asserts:
    //! this buffer was answerable for its allocation and was neither moved on to another owner nor released, so the
    //! allocation has been dropped on the floor. Destroying a NOT_OWNED buffer is silent however much data it refers
    //! to, because a buffer that never claimed responsibility has nothing to drop.
    //!
    //! That distinction is what keeps the check meaningful. A check keyed on the data pointer instead could not tell
    //! an owner apart from an alias, so every alias would have to be silenced -- and the silencing would hide real
    //! leaks just as effectively.
    //!
    //! When FW_BUFFER_STRICT_OWNERSHIP is disabled this destructor does nothing.
    ~Buffer();

    //! Equality operator returning true when buffers are equivalent
    //!
    //! Buffers are deemed equivalent if they contain a pointer to the same data, with the same size, and the same
    //! context. The representation of that buffer for use with serialization and deserialization need not be
    //! equivalent.
    //! \param src: buffer to test against
    //! \return: true if equivalent, false otherwise
    bool operator==(const Buffer& src) const;

    // ----------------------------------------------------------------------
    // Serialization functions
    // ----------------------------------------------------------------------

    //! Returns a LinearBufferBase representation of the wrapped data for serializing
    //!
    //! Returns a LinearBufferBase representation of the wrapped data allowing for serializing other types of data
    //! to the wrapped buffer. Once obtained the user should call one of two functions: `sbb.resetSer();` to setup for
    //! serialization, or `sbb.setBuffLen(buffer.getSize());` to setup for deserializing.
    //! \return representation of the wrapped data to aid in serializing to it
    DEPRECATED(LinearBufferBase& getSerializeRepr(), "Switch to .getSerializer() and .getDeserializer()");

    //! Returns a ExternalSerializeBufferWithMemberCopy representation of the wrapped data for serializing
    //!
    //! \warning The serialization pointer of the returned ExternalSerializeBufferWithMemberCopy object is set to zero
    //! \warning so that serialization will start at the beginning of the memory pointed to by the Fw::Buffer. If that
    //! \warning behavior is not desired the caller may manipulate the serialization offsets with moveSerToOffset
    //! \warning and serializeSkip methods prior to serialization.
    //!
    //! \return representation of the wrapped data to aid in serializing to it
    ExternalSerializeBufferWithMemberCopy getSerializer();

    //! Returns a ExternalSerializeBufferWithMemberCopy representation of the wrapped data for deserializing
    //!
    //! \warning The entire buffer (up to getSize) is available for deserialization.
    //!
    //! \return representation of the wrapped data to aid in deserializing to it
    ExternalSerializeBufferWithMemberCopy getDeserializer();

    //! Serializes this buffer to a LinearBufferBase
    //!
    //! This serializes the buffer to a LinearBufferBase, however, it DOES NOT serialize the wrapped data. It only
    //! serializes the pointer to said data, the size, and context. This is done for efficiency in moving around data,
    //! and is the primary usage of Fw::Buffer. To serialize the wrapped data, use either the data pointer accessor
    //! or the serialize buffer base representation and serialize from that.
    //! \param serialBuffer: serialize buffer to write data into
    //! \return: status of serialization
    Fw::SerializeStatus serializeTo(Fw::SerialBufferBase& serialBuffer,
                                    Fw::Endianness mode = Fw::Endianness::BIG) const;

    //! Deserializes this buffer from a LinearBufferBase
    //!
    //! This deserializes the buffer from a LinearBufferBase, however, it DOES NOT handle serialized data. It only
    //! deserializes the pointer to said data, the size, and context. This is done for efficiency in moving around data,
    //! and is the primary usage of Fw::Buffer. To deserialize the wrapped data, use either the data pointer accessor
    //! or the serialize buffer base representation and deserialize from that.
    //! \param buffer: serialize buffer to read data into
    //! \return: status of serialization
    Fw::SerializeStatus deserializeFrom(Fw::SerialBufferBase& buffer, Fw::Endianness mode = Fw::Endianness::BIG);

    // ----------------------------------------------------------------------
    // Accessor functions
    // ----------------------------------------------------------------------

    //! Returns true if the buffer is valid (data pointer != nullptr and size > 0)
    //!
    bool isValid() const;

    //! Returns pointer to the current data (original allocation pointer plus offset)
    //!
    U8* getData() const;

    //! Returns the original allocation pointer, regardless of any advance/setData adjustments
    //!
    U8* getOriginalData() const;

    //! Returns size of wrapped data
    //!
    FwSizeType getSize() const;

    //! Returns the capacity (size of the original allocation)
    //!
    FwSizeType getCapacity() const;

    //! Returns the current offset from the original allocation pointer
    //!
    FwSizeType getOffset() const;

    //! Returns creation context
    //!
    U32 getContext() const;

    //! Moves the offset forward (positive) or backward (negative) by the given amount
    //!
    //! The size is updated such that the end of the represented data is unchanged. Asserts if the resulting
    //! offset is outside [0, capacity] or the resulting size would be negative.
    //! \param amount: signed number of bytes to move the offset by
    void advance(FwSignedSizeType amount);

    //! Sets pointer to current data within the original allocation
    //!
    //! The supplied pointer must lie within the original allocation (original pointer + capacity); the offset is
    //! updated accordingly and the size is adjusted such that the end of the represented data is unchanged.
    //! Asserts when the pointer is outside the original allocation. To wrap unrelated memory, construct a new
    //! Fw::Buffer or call set().
    //! \param data: pointer within the original allocation
    void setData(U8* data);

    //! Sets size of wrapped data
    //!
    //! Asserts unless offset + size <= capacity.
    //! \param size: new size of the represented data
    void setSize(FwSizeType size);

    //! Sets creation context
    //!
    void setContext(U32 context);

    //! Sets all values, resetting the original allocation pointer, with capacity = size and offset = 0
    //! \param data: data pointer to wrap
    //! \param size: size of data located at data pointer
    //! \param context: user-specified context to track creation. Default: no context
    void set(U8* data, FwSizeType size, U32 context = NO_CONTEXT);

    //! Return whether this buffer is answerable for the memory it wraps
    //! \return OWNED if this buffer has claimed the allocation, NOT_OWNED otherwise
    OwnershipState getOwnershipState() const;

    //! Construct a second buffer over the same wrapped data, deliberately
    //!
    //! Strict ownership forbids implicit copies so that handing a buffer on is always visible in the source. It does
    //! not forbid two objects referring to the same allocation where that is genuinely what is wanted -- a manager
    //! keeping a record of what it handed out, a test recording what it observed, a wrapper aliasing a buffer it was
    //! given by reference. `alias()` is that operation, spelled out so it can be found and reviewed.
    //!
    //! The returned buffer refers to the same memory with the same original pointer, offset, size, capacity, and
    //! context, but it is always NOT_OWNED: an alias is another reference, not another owner. Responsibility for the
    //! allocation stays exactly where it was, and destroying the alias is silent even under
    //! FW_BUFFER_STRICT_OWNERSHIP.
    //!
    //! \return a non-owning buffer referring to the same wrapped data as this one
    Buffer alias() const;

#if FW_SERIALIZABLE_TO_STRING || BUILD_UT
    //! Supports writing this buffer to a string representation
    void toString(Fw::StringBase& text) const;
#endif

#ifdef BUILD_UT
    //! Supports GTest framework for outputting this type to a stream
    //!
    friend std::ostream& operator<<(std::ostream& os, const Buffer& obj);
#endif

  private:
    //! Declare this buffer answerable for the memory it wraps
    //!
    //! Reachable only through Fw::BufferOwner: granting ownership is the business of whoever hands out the
    //! allocation, not of the code being handed one.
    //!
    //! It is invalid to claim a buffer that refers to no data.
    void claim();

    //! Give up this buffer's claim on the memory it wraps
    //!
    //! Reachable only through Fw::BufferOwner. Marks the buffer NOT_OWNED, leaving everything else -- data pointer,
    //! offset, size, capacity, context -- untouched; the memory itself is not freed.
    void release();

    //! Reset this buffer to the default-constructed state, giving up any claim, without touching the wrapped memory
    //!
    //! Used to empty the source of a move so that only the destination refers to the wrapped data.
    void reset();

    Fw::ExternalSerializeBuffer m_serialize_repr;  //<! Representation for serialization and deserialization functions
    U8* m_bufferData;                              //<! data - A pointer to the original allocation
    FwSizeType m_offset;                           //<! offset - Offset of the current data within the allocation
    FwSizeType m_size;                             //<! size - The data size in bytes
    FwSizeType m_capacity;                         //<! capacity - Size of the original allocation in bytes
    U32 m_context;                                 //!< Creation context for disposal
    OwnershipState m_ownership;                    //!< Whether this buffer is answerable for the wrapped memory
};
//! Base class for the component answerable for a pool of Fw::Buffer memory
//!
//! An Fw::Buffer's ownership state may only be changed by the component that hands the allocation out and takes it
//! back -- a buffer manager, a static memory pool, a driver managing its own storage. Deriving from this class is how
//! a component declares itself to be that, and it is the only way to reach Fw::Buffer's claim and release.
//!
//! Everyone else disposes of a buffer by moving it on, and that restriction is what makes the leak check under
//! FW_BUFFER_STRICT_OWNERSHIP worth having. If any component could release a buffer, releasing it would be the
//! obvious way to quiet an assertion -- and quieting that assertion is exactly what a leak looks like. Keeping both
//! ends of ownership with the manager leaves a component holding a buffer it owns one way to be rid of it: hand it
//! to someone else.
//!
//! Deriving from Fw::BufferOwner is a deliberate and greppable act. It does not make the transitions correct on its
//! own -- a manager can still release a buffer it never handed out -- but it puts them somewhere they get reviewed.
//!
//! ```c++
//! class MyBufferPool final : public MyBufferPoolComponentBase, public Fw::BufferOwner {
//!     Fw::Buffer allocate(FwSizeType size) {
//!         Fw::Buffer buffer(this->m_storage, size);
//!         this->claimBuffer(buffer);  // the caller is answerable for it from here
//!         return buffer;
//!     }
//!     void handBack(Fw::Buffer& buffer) {
//!         this->releaseBuffer(buffer);  // back in the pool, nobody is answerable for it
//!     }
//! };
//! ```
class BufferOwner {
  protected:
    //! Construct a buffer owner
    BufferOwner() = default;

    //! Destroy a buffer owner
    //!
    //! Not virtual: Fw::BufferOwner is a mixin declaring a capability, never deleted through a base pointer.
    ~BufferOwner() = default;

    //! Declare `buffer` answerable for its allocation, marking it OWNED
    //!
    //! It is invalid to claim a buffer that refers to no data.
    //!
    //! \param buffer: buffer being handed out
    void claimBuffer(Buffer& buffer) const;

    //! Give up `buffer`'s claim on its allocation, marking it NOT_OWNED
    //!
    //! Leaves the data pointer, offset, size, capacity, and context alone; the memory is not freed. Calling this on
    //! a buffer that was never claimed is a no-op.
    //!
    //! \param buffer: buffer being taken back
    void releaseBuffer(Buffer& buffer) const;
};
}  // end namespace Fw
#endif /* BUFFER_HPP_ */
