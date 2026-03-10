// ======================================================================
// PassiveTextLoggerTester.hpp
// Unit test harness for PassiveTextLogger
// ======================================================================

#ifndef PASSIVE_TEXT_LOGGER_TESTER_HPP
#define PASSIVE_TEXT_LOGGER_TESTER_HPP

#include "PassiveTextLoggerGTestBase.hpp"
#include "Svc/PassiveConsoleTextLogger/ConsoleTextLoggerImpl.hpp"

namespace Svc {

class PassiveTextLoggerTester : public PassiveTextLoggerGTestBase {
  public:
    PassiveTextLoggerTester();
    ~PassiveTextLoggerTester();

    // Tests
    void runNominalTest();
    void testStderrThreshold();

  private:
    void connectPorts();
    void initComponents();

    // Component under test
    ConsoleTextLoggerImpl component;
};

}  // namespace Svc

#endif
