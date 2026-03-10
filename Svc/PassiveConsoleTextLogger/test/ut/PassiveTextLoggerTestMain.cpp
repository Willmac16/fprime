// ======================================================================
// PassiveTextLoggerTestMain.cpp
// Test main for PassiveTextLogger unit tests
// ======================================================================

#include "PassiveTextLoggerTester.hpp"

TEST(Nominal, Logging) {
    Svc::PassiveTextLoggerTester tester;
    tester.runNominalTest();
}

TEST(Nominal, StderrThreshold) {
    Svc::PassiveTextLoggerTester tester;
    tester.testStderrThreshold();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
