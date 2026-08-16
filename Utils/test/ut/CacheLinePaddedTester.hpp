// ======================================================================
// \title  CacheLinePaddedTester.hpp
// \brief  hpp file for Utils::CacheLinePadded test harness implementation class
//
// \copyright
// Copyright 2026, by the California Institute of Technology.
// ALL RIGHTS RESERVED.  United States Government Sponsorship
// acknowledged.
//
// ======================================================================

#ifndef UTILS_TEST_UT_CACHE_LINE_PADDED_TESTER_HPP
#define UTILS_TEST_UT_CACHE_LINE_PADDED_TESTER_HPP

#include <gtest/gtest.h>
#include "Utils/CacheLinePadded.hpp"

namespace Utils {

class CacheLinePaddedTester {
  public:
    CacheLinePaddedTester();
    ~CacheLinePaddedTester();

  public:
    //! Check that alignment and size are exactly what the line size requires
    void testAlignmentAndSize();

    //! Check that get()/operator-> give full read/write access to the wrapped value
    void testAccess();

    //! Check that adjacent members, and adjacent array elements, land on separate lines
    void testSeparation();

    //! Check that two concurrently-hammered, adjacently-declared padded counters both end up correct
    void testConcurrentAccess();

    //! Check that copy/move construction and assignment defer to T: available and correct when T
    //! supports them, and (via type traits) absent when T does not (e.g. Utils::Atomic)
    void testCopyMoveDefersToT();
};

}  // namespace Utils

#endif  // UTILS_TEST_UT_CACHE_LINE_PADDED_TESTER_HPP
