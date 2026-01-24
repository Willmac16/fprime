module Svc {

  @ A demonstration component that accepts an Fw::Time as a command argument
  @ and telemeters it out as a TimeValue
  queued component TimeDemo {

    # ----------------------------------------------------------------------
    # Special ports
    # ----------------------------------------------------------------------

    @ Command receive port
    command recv port CmdDisp

    @ Command registration port
    command reg port CmdReg

    @ Command response port
    command resp port CmdStatus

    @ Event port
    event port Log

    @ Text event port
    text event port LogText

    @ Time get port
    time get port Time

    @ Telemetry port
    telemetry port Tlm

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Set a time value to be telemetered
    async command SET_TIME(
      timeValue: Fw.Time @< The time value to set and telemeter
    ) \
      opcode 0x0

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ Time value was set
    event TIME_SET(
      timeValue: Fw.Time @< The time value that was set
    ) \
      severity activity low \
      id 0x0 \
      format "Time value set to {}"

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Current time value
    telemetry CurrentTimeValue: Fw.TimeValue id 0x0

  }

}
