// ----------------------------------------------------------------------
// Main.cpp
// ----------------------------------------------------------------------

#include "AtomicTester.hpp"
#include "CacheLinePaddedTester.hpp"
#include "RateLimiterTester.hpp"
#include "TokenBucketTester.hpp"

TEST(RateLimiterTest, TestCounterTriggering) {
    Utils::RateLimiterTester tester;
    tester.testCounterTriggering();
}

TEST(RateLimiterTest, TestTimeTriggering) {
    Utils::RateLimiterTester tester;
    tester.testTimeTriggering();
}

TEST(RateLimiterTest, TestCounterAndTimeTriggering) {
    Utils::RateLimiterTester tester;
    tester.testCounterAndTimeTriggering();
}

TEST(RateLimiterTest, TestDefaultConstructorAndSetters) {
    Utils::RateLimiterTester tester;
    tester.testDefaultConstructorAndSetters();
}

TEST(TokenBucketTest, TestTriggering) {
    Utils::TokenBucketTester tester;
    tester.testTriggering();
}

TEST(TokenBucketTest, TestReconfiguring) {
    Utils::TokenBucketTester tester;
    tester.testReconfiguring();
}

TEST(TokenBucketTest, TestInitialSettings) {
    Utils::TokenBucketTester tester;
    tester.testInitialSettings();
}

TEST(TokenBucketTest, TestReplenishAndEdgeCases) {
    Utils::TokenBucketTester tester;
    tester.testReplenishAndEdgeCases();
}

TEST(AtomicTest, TestBackendSelection) {
    Utils::AtomicTester tester;
    tester.testBackendSelection();
}

TEST(AtomicTest, TestLoadStore) {
    Utils::AtomicTester tester;
    tester.testLoadStore();
}

TEST(AtomicTest, TestExchangeAndCompareExchange) {
    Utils::AtomicTester tester;
    tester.testExchangeAndCompareExchange();
}

TEST(AtomicTest, TestArithmeticOperators) {
    Utils::AtomicTester tester;
    tester.testArithmeticOperators();
}

TEST(AtomicTest, TestBitwiseOperators) {
    Utils::AtomicTester tester;
    tester.testBitwiseOperators();
}

TEST(AtomicTest, TestNonLockFreeTypes) {
    Utils::AtomicTester tester;
    tester.testNonLockFreeTypes();
}

TEST(AtomicTest, TestPointerArithmetic) {
    Utils::AtomicTester tester;
    tester.testPointerArithmetic();
}

TEST(AtomicTest, TestConcurrentIncrement) {
    Utils::AtomicTester tester;
    tester.testConcurrentIncrement();
}

TEST(CacheLinePaddedTest, TestAlignmentAndSize) {
    Utils::CacheLinePaddedTester tester;
    tester.testAlignmentAndSize();
}

TEST(CacheLinePaddedTest, TestAccess) {
    Utils::CacheLinePaddedTester tester;
    tester.testAccess();
}

TEST(CacheLinePaddedTest, TestSeparation) {
    Utils::CacheLinePaddedTester tester;
    tester.testSeparation();
}

TEST(CacheLinePaddedTest, TestConcurrentAccess) {
    Utils::CacheLinePaddedTester tester;
    tester.testConcurrentAccess();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
