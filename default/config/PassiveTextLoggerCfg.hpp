#ifndef Config_PassiveTextLoggerCfg_HPP_
#define Config_PassiveTextLoggerCfg_HPP_

enum {
    PASSIVE_TEXT_LOGGER_ID_FILTER_SIZE = 25,  //!< Size of event ID filter

    //! Severity threshold for stderr routing.
    //! Events with Fw::LogSeverity enum value <= this threshold are emitted to
    //! stderr instead of stdout. Corresponds to Fw::LogSeverity values:
    //!   FATAL=1, WARNING_HI=2, WARNING_LO=3, COMMAND=4,
    //!   ACTIVITY_HI=5, ACTIVITY_LO=6, DIAGNOSTIC=7
    //! Set to 0 (default) to send all events to stdout.
    PASSIVE_TEXT_LOGGER_STDERR_THRESHOLD = 0,
};

#endif /* Config_PassiveTextLoggerCfg_HPP_ */
