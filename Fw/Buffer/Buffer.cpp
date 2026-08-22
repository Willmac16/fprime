// ======================================================================
// \title  Buffer.cpp
// \author mstarch
// \brief  cpp file for Fw::Buffer implementation
//
// \copyright
// Copyright 2009-2020, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================
#include <Fw/Buffer/Buffer.hpp>
#include <Fw/FPrimeBasicTypes.hpp>
#include <Fw/Types/Assert.hpp>

#if FW_SERIALIZABLE_TO_STRING
#include <Fw/Types/String.hpp>
#endif
#include <cstring>

namespace Fw {

Buffer::Buffer()
    : Serializable(),
      m_serialize_repr(),
      m_bufferData(nullptr),
      m_offset(0),
      m_size(0),
      m_capacity(0),
      m_context(0xFFFFFFFF) {}

#if !FW_BUFFER_STRICT_OWNERSHIP
Buffer::Buffer(const Buffer& src)
    : Serializable(),
      m_serialize_repr(),
      m_bufferData(src.m_bufferData),
      m_offset(src.m_offset),
      m_size(src.m_size),
      m_capacity(src.m_capacity),
      m_context(src.m_context) {
    if (src.m_bufferData != nullptr) {
        this->m_serialize_repr.setExtBuffer(this->m_bufferData + this->m_offset, this->m_size);
    }
}
#endif

Buffer::Buffer(Buffer&& src)
    : Serializable(),
      m_serialize_repr(),
      m_bufferData(src.m_bufferData),
      m_offset(src.m_offset),
      m_size(src.m_size),
      m_capacity(src.m_capacity),
      m_context(src.m_context) {
    if (this->m_bufferData != nullptr) {
        this->m_serialize_repr.setExtBuffer(this->m_bufferData + this->m_offset, this->m_size);
    }
    // Only this buffer may refer to the wrapped data once the move completes
    src.reset();
}

Buffer::Buffer(U8* data, FwSizeType size, U32 context)
    : Serializable(),
      m_serialize_repr(),
      m_bufferData(data),
      m_offset(0),
      m_size(size),
      m_capacity(size),
      m_context(context) {
    if (m_bufferData != nullptr) {
        this->m_serialize_repr.setExtBuffer(this->m_bufferData, this->m_size);
    }
}

#if !FW_BUFFER_STRICT_OWNERSHIP
Buffer& Buffer::operator=(const Buffer& src) {
    // Ward against self-assignment
    if (this != &src) {
        this->m_bufferData = src.m_bufferData;
        this->m_offset = src.m_offset;
        this->m_size = src.m_size;
        this->m_capacity = src.m_capacity;
        this->m_context = src.m_context;
        if (this->m_bufferData != nullptr) {
            this->m_serialize_repr.setExtBuffer(this->m_bufferData + this->m_offset, this->m_size);
        }
    }
    return *this;
}
#endif

Buffer& Buffer::operator=(Buffer&& src) {
    // Ward against self-assignment: a self-move must not invalidate this buffer
    if (this != &src) {
        this->m_bufferData = src.m_bufferData;
        this->m_offset = src.m_offset;
        this->m_size = src.m_size;
        this->m_capacity = src.m_capacity;
        this->m_context = src.m_context;
        if (this->m_bufferData != nullptr) {
            this->m_serialize_repr.setExtBuffer(this->m_bufferData + this->m_offset, this->m_size);
        } else {
            this->m_serialize_repr.clear();
        }
        // Only this buffer may refer to the wrapped data once the move completes
        src.reset();
    }
    return *this;
}

Buffer::~Buffer() {
#if FW_BUFFER_STRICT_OWNERSHIP
    // Holding an Fw::Buffer is holding responsibility for an allocation. This one was neither moved on to another
    // owner nor handed back, so nobody returned it. A reference with no such responsibility is an Fw::BufferView.
    FW_ASSERT(this->m_bufferData == nullptr,
              static_cast<FwAssertArgType>(reinterpret_cast<PlatformPointerCastType>(this->m_bufferData)),
              static_cast<FwAssertArgType>(this->m_size), static_cast<FwAssertArgType>(this->m_context));
#endif
}

bool Buffer::operator==(const Buffer& src) const {
    return (this->m_bufferData == src.m_bufferData) && (this->m_offset == src.m_offset) &&
           (this->m_size == src.m_size) && (this->m_capacity == src.m_capacity) && (this->m_context == src.m_context);
}

BufferView Buffer::alias() const {
    BufferView aliased(this->m_bufferData, this->m_capacity, this->m_context);
    if (this->m_bufferData != nullptr) {
        aliased.advance(static_cast<FwSignedSizeType>(this->m_offset));
        aliased.setSize(this->m_size);
    }
    return aliased;
}

void Buffer::release() {
    // Empty the handle rather than only clearing the claim: on the sync path this is the caller's own buffer, and
    // leaving it pointing at memory that has gone back into the pool is the use-after-free this prevents
    this->reset();
}

void Buffer::reset() {
    this->m_bufferData = nullptr;
    this->m_offset = 0;
    this->m_size = 0;
    this->m_capacity = 0;
    this->m_context = NO_CONTEXT;
    this->m_serialize_repr.clear();
}

bool Buffer::operator==(const BufferView& src) const {
    return (this->m_bufferData == src.getOriginalData()) && (this->m_offset == src.getOffset()) &&
           (this->m_size == src.getSize()) && (this->m_capacity == src.getCapacity()) &&
           (this->m_context == src.getContext());
}

bool Buffer::isValid() const {
    return (this->m_bufferData != nullptr) && (this->m_size > 0);
}

U8* Buffer::getData() const {
    return (this->m_bufferData == nullptr) ? nullptr : (this->m_bufferData + this->m_offset);
}

U8* Buffer::getOriginalData() const {
    return this->m_bufferData;
}

FwSizeType Buffer::getSize() const {
    return this->m_size;
}

FwSizeType Buffer::getCapacity() const {
    return this->m_capacity;
}

FwSizeType Buffer::getOffset() const {
    return this->m_offset;
}

U32 Buffer::getContext() const {
    return this->m_context;
}

void Buffer::advance(const FwSignedSizeType amount) {
    FW_ASSERT(this->m_bufferData != nullptr);
    // New offset must remain within [0, capacity]
    if (amount < 0) {
        FW_ASSERT(this->m_offset >= static_cast<FwSizeType>(-amount), static_cast<FwAssertArgType>(this->m_offset),
                  static_cast<FwAssertArgType>(amount));
    } else {
        // Advancing must not move past the end of the represented data
        FW_ASSERT(static_cast<FwSizeType>(amount) <= this->m_size, static_cast<FwAssertArgType>(this->m_size),
                  static_cast<FwAssertArgType>(amount));
    }
    this->m_offset = static_cast<FwSizeType>(static_cast<FwSignedSizeType>(this->m_offset) + amount);
    // Keep the end of the represented data fixed
    this->m_size = static_cast<FwSizeType>(static_cast<FwSignedSizeType>(this->m_size) - amount);
    FW_ASSERT(this->m_offset + this->m_size <= this->m_capacity, static_cast<FwAssertArgType>(this->m_offset),
              static_cast<FwAssertArgType>(this->m_size), static_cast<FwAssertArgType>(this->m_capacity));
    this->m_serialize_repr.setExtBuffer(this->m_bufferData + this->m_offset, this->m_size);
}

void Buffer::setData(U8* const data) {
    FW_ASSERT(this->m_bufferData != nullptr);
    FW_ASSERT(data >= this->m_bufferData && data <= &this->m_bufferData[this->m_capacity]);
    this->advance(static_cast<FwSignedSizeType>(data - (this->m_bufferData + this->m_offset)));
}

void Buffer::setSize(const FwSizeType size) {
    FW_ASSERT(this->m_offset + size <= this->m_capacity, static_cast<FwAssertArgType>(this->m_offset),
              static_cast<FwAssertArgType>(size), static_cast<FwAssertArgType>(this->m_capacity));
    this->m_size = size;
    if (m_bufferData != nullptr) {
        this->m_serialize_repr.setExtBuffer(this->m_bufferData + this->m_offset, this->m_size);
    }
}

void Buffer::setContext(const U32 context) {
    this->m_context = context;
}

void Buffer::set(U8* const data, const FwSizeType size, const U32 context) {
#if FW_BUFFER_STRICT_OWNERSHIP
    // Pointing an owning buffer at different memory drops the allocation it was answerable for
    FW_ASSERT(this->m_bufferData == nullptr,
              static_cast<FwAssertArgType>(reinterpret_cast<PlatformPointerCastType>(this->m_bufferData)));
#endif
    this->m_bufferData = data;
    this->m_offset = 0;
    this->m_size = size;
    this->m_capacity = size;
    if (m_bufferData != nullptr) {
        this->m_serialize_repr.setExtBuffer(this->m_bufferData, this->m_size);
    }
    this->m_context = context;
}

Fw::ExternalSerializeBufferWithMemberCopy Buffer::getSerializer() {
    if (this->isValid()) {
        Fw::ExternalSerializeBufferWithMemberCopy esb(this->m_bufferData + this->m_offset, this->m_size);
        esb.resetSer();
        return esb;
    } else {
        return ExternalSerializeBufferWithMemberCopy();
    }
}

Fw::ExternalSerializeBufferWithMemberCopy Buffer::getDeserializer() {
    if (this->isValid()) {
        Fw::ExternalSerializeBufferWithMemberCopy esb(this->m_bufferData + this->m_offset, this->m_size);
        Fw::SerializeStatus stat = esb.setBuffLen(this->m_size);
        FW_ASSERT(stat == Fw::FW_SERIALIZE_OK);
        return esb;
    } else {
        return ExternalSerializeBufferWithMemberCopy();
    }
}

Fw::SerializeStatus Buffer::serializeTo(Fw::SerialBufferBase& buffer, Fw::Endianness mode) const {
    Fw::SerializeStatus stat;
    stat = buffer.serializeFrom(reinterpret_cast<PlatformPointerCastType>(this->m_bufferData), mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    stat = buffer.serializeFrom(this->m_size, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    stat = buffer.serializeFrom(this->m_context, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    stat = buffer.serializeFrom(this->m_offset, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    stat = buffer.serializeFrom(this->m_capacity, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    return stat;
}

Fw::SerializeStatus Buffer::deserializeFrom(Fw::SerialBufferBase& buffer, Fw::Endianness mode) {
    Fw::SerializeStatus stat;
    PlatformPointerCastType pointer;
    stat = buffer.deserializeTo(pointer, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    this->m_bufferData = reinterpret_cast<U8*>(pointer);

    stat = buffer.deserializeTo(this->m_size, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    stat = buffer.deserializeTo(this->m_context, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    stat = buffer.deserializeTo(this->m_offset, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }
    stat = buffer.deserializeTo(this->m_capacity, mode);
    if (stat != Fw::FW_SERIALIZE_OK) {
        return stat;
    }

    if (this->m_bufferData != nullptr) {
        this->m_serialize_repr.setExtBuffer(this->m_bufferData + this->m_offset, this->m_size);
    }
    return stat;
}

// ----------------------------------------------------------------------
// Fw::BufferView
// ----------------------------------------------------------------------

BufferView::BufferView()
    : m_bufferData(nullptr), m_offset(0), m_size(0), m_capacity(0), m_context(Buffer::NO_CONTEXT) {}

BufferView::BufferView(U8* data, FwSizeType size, U32 context)
    : m_bufferData(data), m_offset(0), m_size(size), m_capacity(size), m_context(context) {}

bool BufferView::operator==(const BufferView& src) const {
    return (this->m_bufferData == src.m_bufferData) && (this->m_offset == src.m_offset) &&
           (this->m_size == src.m_size) && (this->m_capacity == src.m_capacity) && (this->m_context == src.m_context);
}

bool BufferView::operator==(const Buffer& src) const {
    return src == *this;
}

bool BufferView::isValid() const {
    return (this->m_bufferData != nullptr) && (this->m_size > 0);
}

U8* BufferView::getData() const {
    return (this->m_bufferData == nullptr) ? nullptr : (this->m_bufferData + this->m_offset);
}

U8* BufferView::getOriginalData() const {
    return this->m_bufferData;
}

FwSizeType BufferView::getSize() const {
    return this->m_size;
}

FwSizeType BufferView::getCapacity() const {
    return this->m_capacity;
}

FwSizeType BufferView::getOffset() const {
    return this->m_offset;
}

U32 BufferView::getContext() const {
    return this->m_context;
}

void BufferView::advance(const FwSignedSizeType amount) {
    FW_ASSERT(this->m_bufferData != nullptr);
    if (amount < 0) {
        FW_ASSERT(this->m_offset >= static_cast<FwSizeType>(-amount), static_cast<FwAssertArgType>(this->m_offset),
                  static_cast<FwAssertArgType>(amount));
    } else {
        FW_ASSERT(static_cast<FwSizeType>(amount) <= this->m_size, static_cast<FwAssertArgType>(this->m_size),
                  static_cast<FwAssertArgType>(amount));
    }
    this->m_offset = static_cast<FwSizeType>(static_cast<FwSignedSizeType>(this->m_offset) + amount);
    this->m_size = static_cast<FwSizeType>(static_cast<FwSignedSizeType>(this->m_size) - amount);
    FW_ASSERT(this->m_offset + this->m_size <= this->m_capacity, static_cast<FwAssertArgType>(this->m_offset),
              static_cast<FwAssertArgType>(this->m_size), static_cast<FwAssertArgType>(this->m_capacity));
}

void BufferView::setSize(const FwSizeType size) {
    FW_ASSERT(this->m_offset + size <= this->m_capacity, static_cast<FwAssertArgType>(this->m_offset),
              static_cast<FwAssertArgType>(size), static_cast<FwAssertArgType>(this->m_capacity));
    this->m_size = size;
}

Fw::ExternalSerializeBufferWithMemberCopy BufferView::getSerializer() const {
    if (this->isValid()) {
        Fw::ExternalSerializeBufferWithMemberCopy esb(this->getData(), this->m_size);
        esb.resetSer();
        return esb;
    }
    return ExternalSerializeBufferWithMemberCopy();
}

Fw::ExternalSerializeBufferWithMemberCopy BufferView::getDeserializer() const {
    if (this->isValid()) {
        Fw::ExternalSerializeBufferWithMemberCopy esb(this->getData(), this->m_size);
        const Fw::SerializeStatus stat = esb.setBuffLen(this->m_size);
        FW_ASSERT(stat == Fw::FW_SERIALIZE_OK, static_cast<FwAssertArgType>(stat));
        return esb;
    }
    return ExternalSerializeBufferWithMemberCopy();
}

// ----------------------------------------------------------------------
// Fw::BufferOwner
// ----------------------------------------------------------------------

Buffer BufferOwner::allocateBuffer(U8* data, FwSizeType size, U32 context) const {
    return Buffer(data, size, context);
}

void BufferOwner::releaseBuffer(Buffer& buffer) const {
    buffer.release();
}

#if FW_SERIALIZABLE_TO_STRING
void Buffer::toString(Fw::StringBase& text) const {
    static const char* formatString = "(data = %p, size = %u, context = %u, offset = %u, capacity = %u)";
    (void)text.format(formatString, this->m_bufferData, this->m_size, this->m_context, this->m_offset,
                      this->m_capacity);  // display string may safely truncate
}
#endif

#ifdef BUILD_UT
std::ostream& operator<<(std::ostream& os, const Buffer& obj) {
    Fw::String str;
    obj.toString(str);
    os << str.toChar();
    return os;
}
#endif

}  // end namespace Fw
