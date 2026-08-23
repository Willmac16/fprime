// ======================================================================
// \title  LanguageHelpers.hpp
// \author lestarch
// \brief  hpp file for C++ language helper functions
//
// \copyright
// Copyright (C) 2025 California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
// ======================================================================
#ifndef FW_TYPES_LANGUAGE_HELPERS_HPP_
#define FW_TYPES_LANGUAGE_HELPERS_HPP_
#include <new>
#include <type_traits>
#include "Fw/Types/Assert.hpp"
#include "Fw/Types/ByteArray.hpp"

//! \brief mark a return value the caller must not silently discard
//!
//! Applied to anything that hands out an Fw::Buffer. Under FW_BUFFER_STRICT_OWNERSHIP a buffer that comes back from
//! a manager is the caller's to hand on, so dropping the returned value on the floor is a leak -- and unlike the
//! destructor's assertion, which can only fire once the program is running, this one is a compile error under
//! -Werror. It is the narrow part of the problem the compiler can actually see: what a caller does with a value it
//! never bound is decidable, where whether a bound buffer is still owned at the end of its scope is not.
#ifndef FW_WARN_UNUSED
#if defined(__GNUC__) || defined(__clang__)
#define FW_WARN_UNUSED __attribute__((warn_unused_result))
#else
#define FW_WARN_UNUSED
#endif
#endif

namespace Fw {
//! \brief cast a value to an rvalue reference so that it can be moved from
//!
//! F Prime's equivalent of `std::move`. It exists because `std::move` lives in `<utility>`, which is not one of the
//! headers a freestanding C++ implementation is required to provide; `<type_traits>`, used here, is. F Prime targets
//! toolchains where that distinction matters, so framework code says `Fw::move` and leaves `<utility>` alone.
//!
//! Like `std::move`, this generates no code: it is a cast that selects a move constructor or move assignment
//! operator at the call site. It does not itself move anything, and it is the callee that decides what to take. The
//! argument must be treated as emptied once the call it feeds has returned.
//!
//! \tparam T deduced type of the value being cast
//! \param value the value to cast
//! \return an rvalue reference to `value`
template <typename T>
constexpr typename std::remove_reference<T>::type&& move(T&& value) {
    return static_cast<typename std::remove_reference<T>::type&&>(value);
}

//! \brief placement new for arrays
//!
//! C++ as a language does not guaranteed that placement new for a C++ array of length N will fit within a memory
//! region of size N *sizeof(T). Moreover, there are some compilers whose implementation of placement new for arrays
//! do not guarantee this property.
//!
//! This function provides a helper for placement new for arrays that guarantees that the array will fit within the
//! provided memory region. It checks that the provided memory region is large enough to hold the array (N * sizeof(T)
//! and that the alignment of the provided memory region is sufficient for the type T. It also checks that the provided
//! memory region is non-null.
//!
//! \warning this function cannot be used for arrays of arrays (i.e. T cannot be an array type).
//!
//! \tparam T the type of the array elements
//! \param array the byte array to use for placement new (pair of bytes pointer and size)
//! \param arraySize the number of elements in the array
//! \return a pointer to the array of type T
template <typename T>
T* arrayPlacementNew(Fw::ByteArray array, FwSizeType arraySize) {
    static_assert(!std::is_array<T>::value, "Cannot use arrayPlacementNew new for arrays of arrays");
    static_assert(std::is_constructible<T>::value,
                  "Cannot use arrayPlacementNew on types without a default zero-argument constructor");
    void* base_pointer = reinterpret_cast<void*>(array.bytes);
    FW_ASSERT(base_pointer != nullptr);
    FW_ASSERT((reinterpret_cast<PlatformPointerCastType>(base_pointer) % alignof(T)) == 0);
    FW_ASSERT(array.size >= (sizeof(T) * arraySize));
    T* type_pointer = static_cast<T*>(base_pointer);
    for (FwSizeType index = 0; index < arraySize; index++) {
        new (&type_pointer[index]) T();
    }
    return type_pointer;
}

//! \brief placement delete for arrays
//!
//! This is the partner of tha above function that performs the destructor operation on every element of type T in the
//! array. This assumes that all elements have been constructed.
//!
//! \warning this function cannot be used for arrays of arrays (i.e. T cannot be an array type).
//!
//! \tparam T the type of the array elements
//! \param arrayPointer pointer to an array of type T
//! \param arraySize the number of elements in the array
template <typename T>
void arrayPlacementDestruct(T* arrayPointer, FwSizeType arraySize) {
    static_assert(!std::is_array<T>::value, "Cannot use arrayPlacementDestruct new for arrays of arrays");
    FW_ASSERT(arrayPointer != nullptr);
    for (FwSizeType index = 0; index < arraySize; index++) {
        arrayPointer[index].~T();
    }
}
}  // namespace Fw
#endif  // FW_TYPES_LANGUAGE_HELPERS_HPP_
