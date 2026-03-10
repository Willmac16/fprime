// ======================================================================
// PassiveTextLoggerTester.cpp
// Unit test implementation for PassiveTextLogger
// ======================================================================

#include "PassiveTextLoggerTester.hpp"
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include "Fw/Logger/test/ut/FakeLogger.hpp"

#define INSTANCE 0
#define MAX_HISTORY_SIZE 10

namespace Svc {

PassiveTextLoggerTester::PassiveTextLoggerTester()
    : PassiveTextLoggerGTestBase("Tester", MAX_HISTORY_SIZE), component("PassiveTextLogger") {
    this->initComponents();
    this->connectPorts();
}

PassiveTextLoggerTester::~PassiveTextLoggerTester() {}

// ----------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------

void PassiveTextLoggerTester::runNominalTest() {
    printf("Testing passive logger writes to stdout logger\n");

    MockLogging::FakeLogger fakeLogger;
    Fw::Logger::registerLogger(&fakeLogger);

    FwEventIdType id = 1;
    Fw::Time timeTag(TimeBase::TB_NONE, 3, 6);
    Fw::LogSeverity severity = Fw::LogSeverity::ACTIVITY_HI;
    Fw::TextLogString text("Passive logger nominal test");
    this->invoke_to_TextLogger(0, id, timeTag, severity, text);

    ASSERT_NE(std::string(""), fakeLogger.m_last)
        << "Expected stdout logger to receive ACTIVITY_HI message";
    ASSERT_NE(std::string::npos, fakeLogger.m_last.find("ACTIVITY_HI"))
        << "Expected severity string in logged message";
    ASSERT_NE(std::string::npos, fakeLogger.m_last.find(text.toChar()))
        << "Expected message text in logged output";

    Fw::Logger::registerLogger(nullptr);
}

void PassiveTextLoggerTester::testStderrThreshold() {
    printf("Testing passive logger stderr threshold routing\n");

    // Configure threshold: events at WARNING_HI (2) or more severe go to stderr
    this->component.configure(nullptr, 0, Fw::LogSeverity::WARNING_HI);

    // Register a fake stdout logger to detect which messages go to stdout
    MockLogging::FakeLogger fakeLogger;
    Fw::Logger::registerLogger(&fakeLogger);

    // Redirect stderr to a temp file so we can inspect it
    int saved_stderr = dup(STDERR_FILENO);
    ASSERT_NE(-1, saved_stderr);
    int tmp_fd = open("test_passive_stderr_out.txt", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    ASSERT_NE(-1, tmp_fd);
    dup2(tmp_fd, STDERR_FILENO);
    close(tmp_fd);

    // --- Message BELOW threshold: should go to stdout (fake logger), not stderr ---
    fakeLogger.reset();
    FwEventIdType id = 10;
    Fw::Time timeTag(TimeBase::TB_NONE, 1, 2);
    // ACTIVITY_HI (enum value 5) is less severe than WARNING_HI (enum value 2)
    // With threshold at WARNING_HI, ACTIVITY_HI should go to stdout
    Fw::LogSeverity lowSeverity = Fw::LogSeverity::ACTIVITY_HI;
    Fw::TextLogString lowText("This should go to stdout");
    this->invoke_to_TextLogger(0, id, timeTag, lowSeverity, lowText);

    ASSERT_NE(std::string(""), fakeLogger.m_last)
        << "Expected stdout logger to receive ACTIVITY_HI message when threshold is WARNING_HI";

    // --- Message AT/ABOVE threshold: should go to stderr, not stdout ---
    fakeLogger.reset();
    Fw::LogSeverity highSeverity = Fw::LogSeverity::FATAL;
    Fw::TextLogString highText("This should go to stderr");
    this->invoke_to_TextLogger(0, id, timeTag, highSeverity, highText);

    ASSERT_EQ(std::string(""), fakeLogger.m_last)
        << "Expected FATAL message to bypass stdout logger when threshold is WARNING_HI";

    // Flush and restore stderr
    fflush(stderr);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);

    // Verify the FATAL message appeared on stderr
    std::ifstream stderrStream("test_passive_stderr_out.txt");
    std::string stderrContent((std::istreambuf_iterator<char>(stderrStream)),
                               std::istreambuf_iterator<char>());
    stderrStream.close();

    ASSERT_NE(std::string::npos, stderrContent.find("FATAL"))
        << "Expected FATAL message in stderr output";
    ASSERT_NE(std::string::npos, stderrContent.find(highText.toChar()))
        << "Expected message text in stderr output";

    // Cleanup
    remove("test_passive_stderr_out.txt");
    Fw::Logger::registerLogger(nullptr);
}

// ----------------------------------------------------------------------
// Helper methods
// ----------------------------------------------------------------------

void PassiveTextLoggerTester::connectPorts() {
    this->connect_to_TextLogger(0, this->component.get_TextLogger_InputPort(0));
}

void PassiveTextLoggerTester::initComponents() {
    this->init();
    this->component.init(INSTANCE);
}

}  // namespace Svc
