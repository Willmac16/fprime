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

Whether a buffer is answerable for the memory it wraps is tracked on the buffer itself, and only the component
managing that memory can change it. See the next section.

#### 2.1.3 Strict Ownership (`FW_BUFFER_STRICT_OWNERSHIP`)

By default a copy of an `Fw::Buffer` is perfectly legal, and so is letting a buffer holding an allocation go out of
scope. Both are how buffer leaks and double-returns happen, and neither leaves any trace. The
`FW_BUFFER_STRICT_OWNERSHIP` setting in `config/FpConfig.h` turns both into failures.

##### Ownership is a property of the buffer, not of the data

`Fw::Buffer` carries an `OwnershipState`: a buffer is `NOT_OWNED` unless something calls `claim()` on it. At most one
buffer referring to a given allocation should be `OWNED`, and that one is answerable for returning it.

| Operation | Effect on ownership |
|---|---|
| `Fw::BufferOwner::claimBuffer()` | Marks the buffer `OWNED`. Whoever hands out an allocation calls it on what it hands out. |
| `Fw::BufferOwner::releaseBuffer()` | Marks the buffer `NOT_OWNED`, leaving the data pointer, offset, size, capacity, and context alone. States that the allocation has been disposed of, not that it has been freed. |
| move | Carries the state to the destination; the source is emptied and left `NOT_OWNED`. |
| copy / `alias()` | Result is always `NOT_OWNED`. Another reference is not another owner. |
| serialization | Carries the state, so a buffer queued for an async port arrives owned on the far side. |

##### Only the manager may grant or revoke it

`Fw::Buffer`'s claim and release are private. The only way to reach them is `Fw::BufferOwner`, a mixin that a
component derives from to declare itself answerable for a pool of buffer memory:

```c++
class MyBufferPool final : public MyBufferPoolComponentBase, public Fw::BufferOwner { ... };
```

This is deliberate. If any component could release a buffer, releasing it would be the obvious way to quiet an
assertion — and quieting that assertion is exactly what a leak looks like. Keeping both ends of ownership with the
manager leaves a component holding a buffer it owns exactly one way to be rid of it: hand it to someone else. A
component cannot mint ownership either, so it cannot declare itself the owner of memory it did not allocate.

Deriving from `Fw::BufferOwner` does not make the transitions correct on its own — a manager can still release a
buffer it never handed out — but it puts them somewhere they get reviewed, in the handful of components that manage
memory rather than scattered across every component that touches a buffer.

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

`Fw::BufferOwner`, `alias()`, and `getOwnershipState()` are available in both configurations, so components can be
written once and built either way. `Svc::BufferManager` already derives from `Fw::BufferOwner`: it claims the buffer
it returns from `bufferGetCallee`, and releases the one handed back on `bufferSendIn`.

```c++
// Hand out an allocation (inside a Fw::BufferOwner)
Fw::Buffer allocated(binBuffer.getData(), binBuffer.getSize(), binBuffer.getContext());
this->claimBuffer(allocated);   // the caller is answerable for this until it comes back
return allocated;

// Record what was handed out without becoming a second owner (anywhere)
Fw::Buffer record = allocated.alias();
```

##### Sync port calls retain ownership; async port calls transfer it

The two kinds of port call are not the same, and the difference is the point.

A **sync** call passes `Fw::Buffer` by reference — it is the same object on both sides. The caller keeps ownership by
default, and a callee that means to keep the buffer moves out of the reference it was given, which empties the
caller's:

```c++
void MyComponent::bufferIn_handler(FwIndexType portNum, Fw::Buffer& fwBuffer) {
    this->m_held = Fw::move(fwBuffer);  // the caller is no longer an owner, and no longer has a buffer
}
```

An **async** call serializes the buffer into a message queue rather than passing it, so it has to transfer ownership:
the sender gives the buffer up and the far side becomes answerable for it. That is why the ownership state is
serialized alongside the rest of the descriptor — it is the extra byte in `SERIALIZED_SIZE` — and why a buffer
arrives at async dispatch already owned. Generated dispatch then deserializes into a local, hands it to the handler,
and destroys it, so a handler that neither moves the buffer on nor returns it is reported rather than quietly losing
the allocation.

The sending half of that is not in place yet: see item 4 below.

##### Use after free

Taking a buffer back does not merely clear its claim, it empties the handle. `Fw::BufferOwner::releaseBuffer()`
resets the buffer to the default-constructed state, so a component that hands a buffer back and then reaches through
its own handle finds nothing there rather than memory that has gone back into the pool and may already belong to
someone else:

```c++
this->deallocate_out(0, buffer);   // the manager takes it back and empties this handle
buffer.getData();                  // nullptr, not a dangling pointer into the pool
```

This works because a sync port call passes the same `Fw::Buffer` object on both sides. It holds in the default
configuration too — the emptying is not conditional on `FW_BUFFER_STRICT_OWNERSHIP`.

What it does not cover is an **alias taken before the buffer went back**. An alias is a separate object, so nothing
empties it, and it still points into the pool. Closing that would need a reference count, and a count cannot survive
being serialized into a message queue on an async hop — nor is shared mutable state in a value type that passes
through every port in the system a trade worth making. This is the reason `alias()` is spelled out rather than
implicit: the places that hold a second reference are the places to look when a buffer is reused underneath one, and
they are greppable.

##### Status

The setting is off by default. Every hand-written translation unit in F´ — flight code and unit tests alike —
compiles with it enabled, and so does the code `fpp-to-cpp` emits, which follows the same rules:

1. Generated test harnesses alias an `Fw::Buffer` into a port-history entry rather than copying it, and an entry
   holding one carries a copy assignment operator that does the same. A history entry records what the tester saw;
   the component under test keeps its handle.
2. Generated serializable types with an `Fw.Buffer` member alias it wherever they would have copied it, so copying
   such a type yields a further reference rather than a second owner.
3. Generated component code hands `dpGet`'s buffer to the container it builds, using the `Fw::DpContainer`
   constructor and `setBuffer` overload that take a buffer rather than aliasing one.
4. Generated async dispatch empties the caller's handle once the message is on the queue, so a buffer that crosses an
   async hop is owned on exactly one side of it. This happens after the queue-full handling: on the drop and hook
   paths nothing was queued, so the buffer is still the caller's to deal with.

What remains is a consequence rather than a defect. Async dispatch on the far side deserializes into a local
`Fw::Buffer` — an owner — and destroys it after the handler returns, so a handler that neither moves the buffer on
nor returns it asserts. That is the leak this setting exists to find, and handlers written to forward a buffer by
reference need revisiting before a whole system runs clean with it enabled.

The rules, and the four autocoder changes that follow them, are documented against the macro in
`config/FpConfig.h`.

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
