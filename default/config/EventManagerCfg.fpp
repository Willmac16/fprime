# ======================================================================
# EventManager FPP configuration
# ======================================================================

module Svc {

  module EventManagerCfg {

    @ Number of PktSend output ports — one per Fw::LogSeverity value.
    @ Port index = severity_value - 1  (FATAL=0 ... DIAGNOSTIC=6).
    @ Must equal the number of defined Fw::LogSeverity values.
    constant NumSeverityPorts = 7

    @ Severity filter behaviour mode.
    @ 0 = per-severity enable/disable (legacy, original EventManager behaviour).
    @ 1 = minimum-severity threshold (new): SET_EVENT_FILTER(level, ENABLED)
    @     sets the threshold so that events at `level` and more severe are
    @     forwarded; SET_EVENT_FILTER(level, DISABLED) raises the threshold
    @     so that events at `level` and less severe are dropped.
    @     FATAL always passes in both modes.
    constant FilterMode = 1

    @ Default minimum severity for FilterMode = 1.
    @ Matches Fw::LogSeverity values: FATAL=1 ... DIAGNOSTIC=7.
    @ Default of 6 (ACTIVITY_LO) mirrors the legacy EventManager default
    @ that passes everything except DIAGNOSTIC events.
    constant MinSeverityDefault = 6

  }

}
