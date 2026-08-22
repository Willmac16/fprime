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
class BufferView;
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
    //! Ownership of an allocation is not a runtime property of Fw::Buffer -- holding an Fw::Buffer *is* ownership,
    //! and a non-owning reference is an Fw::BufferView instead. This enumeration remains as shared vocabulary for
    //! components that track whether storage of their own is currently lent out, which is a fact about the component
    //! rather than about any one buffer.
    enum class OwnershipState {
        NOT_OWNED,  //!< The storage is currently not owned
        OWNED,      //!< The storage is currently owned
    };

  public:
    //! The size type for a buffer - for backwards compatibility
    using SizeType = FwSizeType;

    enum {
        //! Size of Fw::Buffer when serialized
        SERIALIZED_SIZE = 3 * sizeof(SizeType) + sizeof(U32) + sizeof(U8*),
        NO_CONTEXT = 0xFFFFFFFF  //!< Value representing no context
    };

    //! Construct a buffer with no context nor data
    //!
    //! Constructs a buffer setting the context to the default no-context value of 0xffffffff. In addition, the size
    //! and data pointers are zeroed-out.
    Buffer();

#if FW_BUFFER_STRICT_OWNERSHIP
    //! Copy construction is disabled: a buffer may only be handed on by moving it
    //!
    //! See FW_BUFFER_STRICT_OWNERSHIP in FpConfig.h. Take a `const Buffer&` to inspect a buffer, `Fw::move` to hand
    //! it on, and `alias()` for an Fw::BufferView where a second, non-owning reference is genuinely wanted.
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
    //! When FW_BUFFER_STRICT_OWNERSHIP is enabled, destroying a buffer that still refers to an allocation is a
    //! programming error and asserts: holding an Fw::Buffer is holding responsibility for that allocation, and this
    //! one was neither moved on to another owner nor handed back to its manager.
    //!
    //! There is no ownership flag to consult, because there is nothing to consult it about. A reference that is not
    //! responsible for an allocation is an Fw::BufferView, which is a different type and destroys silently.
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

    //! Equality operator against a view, true when both refer to the same memory in the same way
    //! \param src: view to test against
    //! \return: true if equivalent, false otherwise
    bool operator==(const BufferView& src) const;

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

    //! Construct a non-owning view of the memory this buffer refers to
    //!
    //! Where a second reference to one allocation is genuinely wanted -- a manager keeping a record of what it handed
    //! out, a test recording what it observed, a sub-range of a packet being parsed -- that reference is an
    //! Fw::BufferView. It carries the same descriptor and reaches the same memory, but it is a different type, so it
    //! cannot be mistaken for the buffer nor handed on as though it were.
    //!
    //! \return a view of the same memory this buffer refers to
    BufferView alias() const;

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
    //! Give up this buffer's claim and empty the handle
    //!
    //! Reachable only through Fw::BufferOwner. Resets the buffer to the default-constructed state; the memory itself
    //! is not freed.
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
};
//! A non-owning reference to memory an Fw::Buffer refers to
//!
//! Fw::Buffer is the owning handle: holding one means being answerable for returning the allocation, which is why it
//! is move-only and why dropping one is an error. Plenty of code has no such responsibility and only needs to look at
//! the memory, or at part of it -- a component parsing a sub-range out of a packet it was handed, a manager keeping a
//! record of what it lent out, a test recording what it observed. That is what this type is for.
//!
//! Making the distinction a type rather than a flag on Fw::Buffer is what lets the ownership rules be checked by the
//! compiler instead of at runtime. A view cannot be moved into a member that wants a buffer, cannot be handed to a
//! port that carries one, and cannot be mistaken for the thing that has to be returned. Nothing has to ask a buffer
//! whether it is really an owner, because a reference that is not one has a different type.
//!
//! A view does not keep the memory alive and does not know when it goes away. It refers to whatever the buffer it was
//! taken from referred to, for as long as that allocation lasts; using one after the allocation has gone back to its
//! manager is the same mistake as using a raw pointer after a free, and this type does not prevent it. What it does
//! is make every place that holds such a reference visible in the source.
class BufferView final {
  public:
    //! Construct a view referring to nothing
    BufferView();

    //! Construct a view of the given memory
    //!
    //! \param data: pointer to the memory being viewed
    //! \param size: size of the memory being viewed
    //! \param context: user-specified context, carried for identification. Default: no context
    BufferView(U8* data, FwSizeType size, U32 context = Buffer::NO_CONTEXT);

    //! Views are freely copyable: copying a reference creates no new responsibility
    BufferView(const BufferView& src) = default;

    //! Views are freely copyable: copying a reference creates no new responsibility
    BufferView& operator=(const BufferView& src) = default;

    //! Destroy a view. Always silent: a view was never answerable for anything.
    ~BufferView() = default;

    //! Equality operator returning true when views refer to the same memory in the same way
    //! \param src: view to test against
    //! \return true if equivalent, false otherwise
    bool operator==(const BufferView& src) const;

    //! Equality operator against an owning buffer, true when both refer to the same memory in the same way
    //! \param src: buffer to test against
    //! \return: true if equivalent, false otherwise
    bool operator==(const Buffer& src) const;

    //! Returns true if the view refers to data (pointer != nullptr and size > 0)
    bool isValid() const;

    //! Returns pointer to the current data (original pointer plus offset)
    U8* getData() const;

    //! Returns the original pointer, regardless of any advance/setSize adjustments
    U8* getOriginalData() const;

    //! Returns size of the viewed data
    FwSizeType getSize() const;

    //! Returns the capacity (size of the original region)
    FwSizeType getCapacity() const;

    //! Returns the current offset from the original pointer
    FwSizeType getOffset() const;

    //! Returns the context carried from the buffer this view was taken from
    U32 getContext() const;

    //! Moves the offset forward (positive) or backward (negative) by the given amount
    //!
    //! Narrows the view without losing the original pointer, exactly as Fw::Buffer::advance does. Asserts if the
    //! resulting offset is outside [0, capacity] or the resulting size would be negative.
    //!
    //! \param amount: signed number of bytes to move the offset by
    void advance(FwSignedSizeType amount);

    //! Sets size of the viewed data
    //!
    //! Asserts unless offset + size <= capacity.
    //! \param size: new size of the viewed region
    void setSize(FwSizeType size);

    //! Returns a serializer over the viewed data
    //! \return representation of the viewed data to aid in serializing to it
    ExternalSerializeBufferWithMemberCopy getSerializer() const;

    //! Returns a deserializer over the viewed data
    //! \return representation of the viewed data to aid in deserializing from it
    ExternalSerializeBufferWithMemberCopy getDeserializer() const;

  private:
    U8* m_bufferData;       //!< Pointer to the original region
    FwSizeType m_offset;    //!< Offset of the viewed data within the region
    FwSizeType m_size;      //!< Size of the viewed data in bytes
    FwSizeType m_capacity;  //!< Size of the original region in bytes
    U32 m_context;          //!< Context carried from the buffer this view was taken from
};

//! Base class for the component answerable for a pool of Fw::Buffer memory
//!
//! An Fw::Buffer's ownership state may only be changed by the component that hands the allocation out and takes it
//! back -- a buffer manager, a static memory pool, a driver managing its own storage. Deriving from this class is how
//! a component declares itself to be that, and it is the only way to create an owning buffer or take one back.
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
//!         return this->allocateBuffer(this->m_storage, size);  // the caller is answerable for it
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

    //! Create the owning handle for an allocation this component is handing out
    //!
    //! There is no separate act of claiming: an Fw::Buffer is the claim, so producing one is how a manager says the
    //! recipient is now answerable for the memory. Whoever receives it must move it on or hand it back.
    //!
    //! \param data: pointer to the allocation being handed out
    //! \param size: size of the allocation
    //! \param context: user-specified context to track the allocation. Default: no context
    //! \return an owning buffer over the allocation
    Buffer allocateBuffer(U8* data, FwSizeType size, U32 context = Buffer::NO_CONTEXT) const;

    //! Take `buffer` back, leaving the handle referring to nothing
    //!
    //! Resets `buffer` to the default-constructed state: null data pointer, zero offset, size and capacity,
    //! NO_CONTEXT, and NOT_OWNED. The memory itself is not freed -- that is the manager's business -- but the handle
    //! the caller was holding no longer reaches it.
    //!
    //! Emptying the handle rather than only clearing its claim is what makes a use-after-free on the return path
    //! impossible rather than merely detectable. A sync port call passes the same Fw::Buffer object on both sides,
    //! so a component that hands a buffer back and then reaches through its own handle finds nothing there instead
    //! of memory that now belongs to someone else. See the note on aliases in Fw/Buffer/docs/sdd.md for the case
    //! this does not cover.
    //!
    //! \param buffer: buffer being taken back
    void releaseBuffer(Buffer& buffer) const;
};
}  // end namespace Fw
#endif /* BUFFER_HPP_ */
