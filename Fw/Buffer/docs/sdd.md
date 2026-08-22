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

Whether a reference is answerable for the memory it refers to is decided by its *type*, not by any state on it: an
`Fw::Buffer` is, an `Fw::BufferView` is not. See the next section.

#### 2.1.3 Strict Ownership (`FW_BUFFER_STRICT_OWNERSHIP`)

By default a copy of an `Fw::Buffer` is perfectly legal, and so is letting a buffer holding an allocation go out of
scope. Both are how buffer leaks and double-returns happen, and neither leaves any trace. The
`FW_BUFFER_STRICT_OWNERSHIP` setting in `config/FpConfig.h` turns both into failures.

##### Ownership is the type

There is no ownership flag on `Fw::Buffer` and nothing to consult at runtime. **Holding an `Fw::Buffer` is holding
responsibility for an allocation.** A reference that carries no such responsibility is an
[`Fw::BufferView`](../Buffer.hpp), which is a different type.

| | `Fw::Buffer` | `Fw::BufferView` |
|---|---|---|
| What it means | You must return this allocation | You are looking at memory someone else is answerable for |
| Copyable | No — move-only under strict ownership | Yes, freely |
| Destroying it | **Asserts** if it still refers to an allocation | Always silent |
| Where it comes from | `Fw::BufferOwner::allocateBuffer()`, or a move | `Fw::Buffer::alias()`, or constructed over any memory |
| Can be sent on a port | Yes | No — a view is not the thing that has to come back |

That split is what lets the compiler do the checking. A view cannot be moved into a member that wants a buffer,
cannot be handed to a port that carries one, and cannot be mistaken for the thing that has to be returned. The
destructor check needs no flag, because a reference that is not an owner is not an `Fw::Buffer` in the first place —
so the check never fires on a reference and never has to be silenced.

A view does not keep memory alive and does not know when it goes away. It refers to whatever the buffer it came from
referred to, for as long as that allocation lasts. Using one after the allocation has gone back to its manager is the
same mistake as using a raw pointer after a free, and the type does not prevent it. What it does is make every place
that holds such a reference visible in the source.

##### Only the manager creates and reclaims

`Fw::Buffer` has no way to give an allocation up, and the way to produce one is `Fw::BufferOwner`, a mixin a
component derives from to declare itself answerable for a pool of memory:

```c++
class MyBufferPool final : public MyBufferPoolComponentBase, public Fw::BufferOwner {
    Fw::Buffer allocate(FwSizeType size) {
        return this->allocateBuffer(this->m_storage, size);  // the caller is answerable from here
    }
    void handBack(Fw::Buffer& buffer) {
        this->releaseBuffer(buffer);  // back in the pool, and the caller's handle is emptied
    }
};
```

There is no separate act of claiming: an `Fw::Buffer` *is* the claim, so producing one is how a manager says the
recipient is now answerable. Reclaiming is genuinely restricted — `Fw::Buffer::release()` is private and reachable
only through `Fw::BufferOwner` — because if any component could give a buffer up, doing so would be the obvious way
to quiet an assertion, and quieting that assertion is exactly what a leak looks like. A component holding a buffer
has one way to be rid of it: hand it to someone else.

Creation is not yet restricted the same way. `Fw::Buffer`'s memory-taking constructor is still public, so
`allocateBuffer()` is at present a statement of intent rather than a gate. Making it private is the remaining step,
and it is a large one: 351 sites across the tree construct a buffer over memory, and each has to be reclassified as
an owner minting a handle or — far more often — as a view. The destructor check already catches the cases that
matter, since a handle minted outside a manager still has to be disposed of, so this tightening can follow the
migration rather than lead it.

##### Use after free

`releaseBuffer()` empties the handle rather than merely marking it. A sync port call passes the same `Fw::Buffer`
object on both sides, so a component that hands a buffer back and then reaches through its own handle finds nothing:

```c++
this->deallocate_out(0, buffer);   // the manager takes it back and empties this handle
buffer.getData();                  // nullptr, not a dangling pointer into the pool
```

This is unconditional — it applies with `FW_BUFFER_STRICT_OWNERSHIP` off as well. It does not cover a view taken
before the buffer went back; closing that would need a reference count, and a count cannot survive being serialized
into a message queue on an async hop.

##### Sync port calls retain ownership; async port calls transfer it

A **sync** call passes `Fw::Buffer` by reference — the same object on both sides. The caller keeps ownership, and a
callee that means to keep the buffer moves out of the reference it was given, which empties the caller's:

```c++
void MyComponent::bufferIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    this->m_held = Fw::move(fwBuffer);  // the caller is no longer an owner, and no longer has a buffer
}
```

An **async** call serializes the buffer into a message queue rather than passing it, so it must transfer ownership.
Generated dispatch deserializes into a local `Fw::Buffer` — an owning handle — hands it to the handler, and destroys
it, so a handler that neither moves the buffer on nor returns it is reported rather than quietly losing the
allocation. The sending half of that is not in place yet: see item 4 below.

##### Status

The setting is off by default. Every hand-written translation unit in F´ — flight code and unit tests alike —
compiles with it enabled; what does not is code emitted by `fpp-to-cpp`:

1. Generated test harnesses copy-assign `Fw::Buffer` into port-history entries. Those entries want `Fw::BufferView`.
2. Generated serializable types with an `Fw.Buffer` member copy it in their copy constructor and assignment operator.
3. Generated component code constructs `Fw::DpContainer` from an lvalue `Fw::Buffer`, so the container has to mint a
   second owning handle over the same memory. `Fw::DpContainer` and `Svc::DpManager` derive from `Fw::BufferOwner`
   only to stand in for this, and both should stop once the buffer is passed by move.
4. Generated async `invoke()` serializes the caller's buffer into the queue and leaves it untouched, so the caller is
   still an owner when its buffer goes out of scope. It needs to take the buffer by rvalue reference, or reset it
   once serialized. Nothing in F´ can stand in for this: a component cannot give a buffer up on its own, by design.

On the F´ side, the remaining step is the one described above: reclassify the 351 construction sites and then make
`Fw::Buffer`'s memory-taking constructor private.

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
