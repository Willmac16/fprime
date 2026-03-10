#ifndef SVC_TEXT_LOGGER_IMPL_HPP
#define SVC_TEXT_LOGGER_IMPL_HPP

#include <Fw/Log/LogSeverityEnumAc.hpp>
#include <Os/Console.hpp>
#include <Svc/PassiveConsoleTextLogger/PassiveTextLoggerComponentAc.hpp>
#include <config/PassiveTextLoggerCfg.hpp>

namespace Svc {

class ConsoleTextLoggerImpl final : public PassiveTextLoggerComponentBase {
  public:
    // Only called by derived class
    ConsoleTextLoggerImpl(const char* compName);
    ~ConsoleTextLoggerImpl();

    //! Configure component with event ID filters and optional stderr threshold.
    //!
    //! \param filteredIds  Array of event IDs to suppress (may be nullptr when count == 0).
    //! \param count        Number of IDs in filteredIds.
    //! \param stderrThreshold  Events whose severity enum value is <= this value are emitted to
    //!                         stderr instead of stdout.  Use Fw::LogSeverity::T values, e.g.
    //!                         Fw::LogSeverity::WARNING_HI.  Defaults to the compile-time
    //!                         PASSIVE_TEXT_LOGGER_STDERR_THRESHOLD (0 = all to stdout).
    void configure(const FwEventIdType* filteredIds,
                   FwSizeType count,
                   Fw::LogSeverity::T stderrThreshold =
                       static_cast<Fw::LogSeverity::T>(PASSIVE_TEXT_LOGGER_STDERR_THRESHOLD));

  private:
    // downcalls for input ports
    void TextLogger_handler(FwIndexType portNum,
                            FwEventIdType id,
                            Fw::Time& timeTag,
                            const Fw::LogSeverity& severity,
                            Fw::TextLogString& text);

    // Event ID filters
    FwSizeType m_numFilteredIDs;
    FwEventIdType m_filteredIDs[PASSIVE_TEXT_LOGGER_ID_FILTER_SIZE];

    //! Severity threshold: events with severity.e <= m_stderrThreshold go to stderr.
    //! Initialised from PASSIVE_TEXT_LOGGER_STDERR_THRESHOLD (0 = disabled).
    Fw::LogSeverity::T m_stderrThreshold;

    //! Console instance configured for standard error output.
    Os::Console m_stderrConsole;
};

}  // namespace Svc

#endif
