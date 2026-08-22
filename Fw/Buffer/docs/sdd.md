# Fw::Buffer Serializable / Fw::BufferGet Port / Fw::BufferSend Port

## 1 Introduction

This module provides the following elements:

* A type `Fw::Buffer` representing a wrapper around a variable-size buffer. This allows for passing a reference to the
allocated memory around without a copy. Typically the memory is allocated in a buffer manager or similar component but
this is not required.
* A port `Fw::BufferGet` for requesting a buffer of type `Fw::Buffer` from
a [`BufferManager`](../../../Svc/BufferManager/docs/sdd.md) and similar components.

* A port `Fw::BufferSend` for sending a buffer of type `Fw::Buffer` from one component to another.

## 2 Design

The `Fw::Buffer` type wraps a pointer to memory and the size of that memory region. Thus, allowing users to pass the
pointer and size around as a pair without incurring a copy of the data at each step. **Note:** `Fw::Buffer` is not safe
to pass outside a given address space.

### 2.1 The Type Fw::Buffer

`Fw::Buffer` is a serializable class defining the following (private) fields. These fields are accessed through accessor functions.

Name | Type | Accessors | Purpose
---- | ---- | --------- | -------
`m_bufferData` | `U8*`        | `getOriginalData()`           | Pointer to the original allocation wrapped by this buffer
`m_offset`     | `FwSizeType` | `getOffset()`/`setData()`/`advance()` | Offset of the current data within the original allocation; `getData()` returns `m_bufferData + m_offset`
`m_size`       | `FwSizeType` | `getSize()`/`setSize()`       | Size of the data region currently represented by this buffer
`m_capacity`   | `FwSizeType` | `getCapacity()`               | Size of the original allocation; set on construction or `set()`
`m_context`    | `U32`        | `getContext()`/`setContext()` | Context of buffer's origin. Used to track buffers created by [`BufferManager`](../../../Svc/BufferManager/docs/sdd.md)

A value _B_ of type `Fw::Buffer` is **valid** if `m_bufferData != nullptr` and
`m_size > 0`; otherwise it is **invalid**.
The interface function `isValid` reports whether a buffer is valid.
Calling this function on a buffer _B_ returns `true` if _B_ is valid, otherwise `false`.

If a buffer _B_ is invalid, then the pointer returned by _B_ `.getData()` and the
serialization interfaces returned by
_B_ `.getSerializer()` and _B_ `.getDeserializer()` are considered invalid and should not be used.

#### 2.1.1 Original Pointer, Offset, and Capacity

`Fw::Buffer` stores its original allocation pointer plus an offset rather than allowing raw manipulation of the data
pointer. This contract guarantees that the original allocation pointer is always recoverable via `getOriginalData()`,
regardless of how much downstream consumers have advanced into the buffer. Components that must re-identify a buffer
when ownership is returned (e.g. a buffer manager reclaiming an allocation) may therefore key on `getOriginalData()`.

The following contractual expectations apply:

* Constructing a buffer with `Fw::Buffer(data, size, context)` or calling `set(data, size, context)` establishes a new
  original allocation: the offset is reset to `0` and the capacity is set to `size`.
* `advance(amount)` moves the offset forward (positive) or backward (negative) and updates the size such that the end
  of the represented data is unchanged. Consuming leading bytes (e.g. a frame header) must be done with `advance()`.
  An assertion fails if the resulting offset falls outside `[0, capacity]` or the resulting size would be negative.
* `setData(pointer)` requires the supplied pointer to lie within the original allocation
  (`[getOriginalData(), getOriginalData() + getCapacity()]`); the offset is updated accordingly. An assertion fails
  for a pointer outside the original allocation. **To wrap unrelated memory, construct a new `Fw::Buffer` or call
  `set()`** — reusing an existing buffer for unrelated memory via `setData()` is not permitted.
* `setSize(size)` requires `getOffset() + size <= getCapacity()`; an assertion fails otherwise.
* Serialization (`serializeTo`/`deserializeFrom`) carries the original pointer, offset, and capacity so that
  provenance survives transfer across ports.

#### 2.1.2 Copy and Move Semantics

`Fw::Buffer` is both copyable and movable. Neither operation touches the wrapped data: only the pointer, offset, size,
capacity, and context are transferred. The difference is what happens to the source.

| Operation | Source after the operation | Use when |
|---|---|---|
| Copy construction / copy assignment | Unchanged: it still refers to the wrapped data | Both buffers are meant to stay valid, e.g. retaining a reference while forwarding another |
| Move construction / move assignment | Reset to the default-constructed state: null pointer, zero offset/size/capacity, `NO_CONTEXT` | The buffer is handed off for good, e.g. stored into a member for later return, or returned from a function |

`Fw::Buffer` does not own the memory it wraps and does not free anything, so a move releases nothing. What it does do
is make the hand-off of *responsibility* explicit. Because the moved-from buffer is left invalid, it cannot be used to
return, free, or re-send the same allocation a second time — the buffer-ownership mistake that a plain copy leaves
undetectable. Prefer a move wherever a buffer is passed along rather than shared.

```c++
// Take custody of an incoming buffer for later return. The caller's buffer is left invalid, so it cannot
// be returned or re-sent while this component still holds it.
void MyComponent::bufferSendIn_handler(FwIndexType portNum, Fw::Buffer& buffer) {
    this->m_heldBuffer = Fw::move(buffer);
    FW_ASSERT(not buffer.isValid());
}
```

Note that `Fw::BufferSend` and `Fw::BufferGet` pass `Fw::Buffer` by non-`const` reference, so a buffer cannot be moved
directly into a port call. Move at the point where custody actually changes: into a member, into a queue entry, or out
of a function returning `Fw::Buffer`.

A moved-from buffer is reset, not poisoned: calling `set()` on it, or assigning another buffer to it, makes it usable
again. Self-move-assignment is a no-op and leaves the buffer unchanged.

`release()` states that this buffer is no longer answerable for the memory it wraps, without freeing anything or
disturbing what the buffer refers to. Use it where an allocation has been disposed of by some means other than a move
-- returned to its manager, or handed on as a raw pointer. See the next section for what ownership means.

#### 2.1.3 Strict Ownership (`FW_BUFFER_STRICT_OWNERSHIP`)

By default a copy of an `Fw::Buffer` is perfectly legal, and so is letting a buffer holding an allocation go out of
scope. Both are how buffer leaks and double-returns happen, and neither leaves any trace. The
`FW_BUFFER_STRICT_OWNERSHIP` setting in `config/FpConfig.h` turns both into failures.

##### Ownership is a property of the buffer, not of the data

`Fw::Buffer` carries an `OwnershipState`: a buffer is `NOT_OWNED` unless something calls `claim()` on it. At most one
buffer referring to a given allocation should be `OWNED`, and that one is answerable for returning it.

| Operation | Effect on ownership |
|---|---|
| `claim()` | Marks this buffer `OWNED`. Whoever hands out an allocation calls it on what it hands out. |
| `release()` | Marks this buffer `NOT_OWNED`, leaving the data pointer, offset, size, capacity, and context alone. States that the allocation has been disposed of, not that it has been freed. |
| move | Carries the state to the destination; the source is emptied and left `NOT_OWNED`. |
| copy / `alias()` | Result is always `NOT_OWNED`. Another reference is not another owner. |
| deserialization | Result is `NOT_OWNED`. Ownership is a local property and is not carried on the wire. |

This is what makes the destructor check worth having. Keying it on whether the buffer refers to data instead would
make an owner indistinguishable from an alias, so every alias would need silencing — and the silencing would hide
real leaks just as effectively. Reference counting would answer the same question, but a count cannot survive being
serialized into a message queue on an async port hop, and shared mutable state in a value type passed through every
port in the system is not a trade F´ should make.

##### What changes when the setting is on

| | Default (`0`) | Strict (`1`) |
|---|---|---|
| `Fw::Buffer b = other;` | Compiles; both refer to the allocation | **Build error**: copy constructor is deleted |
| `b = other;` | Compiles; both refer to the allocation | **Build error**: copy assignment is deleted |
| `b = other.alias();` | Second reference, `NOT_OWNED` | Same |
| `b = Fw::move(other);` | Transfers data and ownership; `other` left empty | Same |
| Destroying an `OWNED` buffer | Silent | **Assertion failure** |
| Destroying a `NOT_OWNED` buffer | Silent | Silent, however much data it refers to |
| `set()` on an `OWNED` buffer | Silent | **Assertion failure**: it would drop the allocation |

`claim()`, `release()`, `alias()`, and `getOwnershipState()` are available in both configurations, so components can
be written once and built either way. `Svc::BufferManager` already uses them: it claims the buffer it returns from
`bufferGetCallee`, and releases the one handed back on `bufferSendIn`.

```c++
// Hand out an allocation
Fw::Buffer allocated(binBuffer.getData(), binBuffer.getSize(), binBuffer.getContext());
allocated.claim();     // the caller is answerable for this until it comes back
return allocated;

// Record what was handed out without becoming a second owner
Fw::Buffer record = allocated.alias();
```

##### Status

The setting is off by default. Every hand-written translation unit in F´ — flight code and unit tests alike —
compiles with it enabled; what does not is code emitted by `fpp-to-cpp`, which copy-assigns `Fw::Buffer` in generated
test harnesses, in serializable types with an `Fw.Buffer` member, and when constructing `Fw::DpContainer`. That list,
and the two runtime caveats around async port hops, are documented against the macro in `config/FpConfig.h`.

`Fw::Buffer`'s behavior under the setting is covered by `Fw_Buffer_strict_ownership_ut_exe`, a separate test
executable compiled with the macro on, which is always built and run.

### 2.2 The Port Fw::BufferGet

As shown in the following diagram, `Fw::BufferGet` has one argument `size` of type `U32`. It returns a value of type
`Fw::Buffer`. The returned `Fw::Buffer` must be checked for validity before using.

![`Fw::BufferGet` Diagram](img/BufferGetBDD.jpg "Fw::BufferGet Port")

### 2.3 The Port Fw::BufferSend

As shown in the following diagram, `Fw::BufferSend` has one argument `fwBuffer` of type `Fw::Buffer`.

![`Fw::BufferSend` Diagram](img/BufferSendBDD.jpg "Fw::BufferSend Port")

## 3 Usage Notes

Components allocating `Fw::Buffer` objects may use the `m_context` field at their discretion. This field is typically
used to track the origin of the buffer for eventual allocation.

When a component fails to allocate memory, it must set
the `m_bufferData` field to `nullptr` and/or set the `m_size` field to zero to indicate that the buffer is invalid.

A receiver of an `Fw::Buffer` object _B_ must check that _B_ is valid before accessing the
data stored in _B_.
To check validity, you can call the interface function `isValid()`.

### Serializing and Deserializing with `Fw::Buffer`

Users can obtain a serialization buffer, `sb`, by calling either `getSerializer()` or `getDeserializer()`. 
Note that both of these methods return a `Fw::ExternalSerializeBufferWithMemberCopy` object that is meant to be 
managed by the caller and only affects the data of the underlying buffer.

**Serializing to `Fw::Buffer`**
```c++
U32 my_data = 10001;
U8  my_byte = 2;
auto sb = my_fw_buffer.getSerializer();
// Defaults to big-endian
sb.serializeFrom(my_data);
sb.serializeFrom(my_byte);
// Or for little-endian
sb.serializeFrom(my_data, Fw::Endianness::LITTLE);
sb.serializeFrom(my_byte, Fw::Endianness::LITTLE);
```

**Deserializing from `Fw::Buffer`**
```c++
U32 my_data = 0;
U8  my_byte = 0;
auto sb = my_fw_buffer.getDeserializer();
// Defaults to big-endian
sb.deserializeTo(my_data);
sb.deserializeTo(my_byte);
// Or for little-endian
sb.deserializeTo(my_data, Fw::Endianness::LITTLE);
sb.deserializeTo(my_byte, Fw::Endianness::LITTLE);
```

The objects returned by `getSerializer()` and `getDeserializer()` implement the `Fw::SerialBufferBase` interface. This
allows them to be passed directly to `Fw::Serializable::serializeTo` and `Fw::Serializable::deserializeFrom` on
user-defined serializable types.
