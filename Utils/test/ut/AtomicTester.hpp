// ======================================================================
// \title  AtomicTester.hpp
// \brief  hpp file for Utils::Atomic test harness implementation class
//
// \copyright
// Copyright 2026, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#ifndef UTILS_TEST_UT_ATOMIC_TESTER_HPP
#define UTILS_TEST_UT_ATOMIC_TESTER_HPP

#include <gtest/gtest.h>
#include "Utils/Atomic.hpp"

namespace Utils {

class AtomicTester {
  public:
    AtomicTester();
    ~AtomicTester();

  public:
    //! Check that the lock-free and mutex-backed backends are selected as documented
    void testBackendSelection();

    //! Check load, store, assignment and conversion on both backends
    void testLoadStore();

    //! Check exchange and the compare-exchange operations on both backends
    void testExchangeAndCompareExchange();

    //! Check +=, -=, ++, -- and the fetch_add/fetch_sub operations on both backends
    void testArithmeticOperators();

    //! Check &=, |=, ^= and the fetch_and/fetch_or/fetch_xor operations on both backends
    void testBitwiseOperators();

    //! Check types that always select the mutex-backed backend, plus bool and pointer types
    void testNonLockFreeTypes();

    //! Check that pointer T supports real element-wise pointer arithmetic via +=, -=, ++, --, fetch_add
    //! and fetch_sub, on both backends
    void testPointerArithmetic();

    //! Check that concurrent read-modify-writes from several tasks do not lose updates
    void testConcurrentIncrement();
};

}  // namespace Utils

#endif  // UTILS_TEST_UT_ATOMIC_TESTER_HPP
