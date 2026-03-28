module Ref {

  @ Component for stress-testing GDS telemetry and event throughput.
  @ Generates deterministic, sequenced telemetry and events at configurable
  @ rates to identify lag and data loss in the GDS pipeline.
  passive component StressTest {

    # ----------------------------------------------------------------------
    # General Ports
    # ----------------------------------------------------------------------

    @ Scheduler input driven by rate group
    sync input port schedIn: Svc.Sched

    # ----------------------------------------------------------------------
    # Special ports
    # ----------------------------------------------------------------------

    @ Time get port
    time get port timeCaller

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Start generating stress telemetry and events.
    @ tlmPerCycle: number of telemetry writes per scheduler cycle
    @ eventsPerCycle: number of events emitted per scheduler cycle
    sync command START(
      tlmPerCycle: U32 @< telemetry items to emit per cycle
      eventsPerCycle: U32 @< events to emit per cycle
    ) opcode 0x00

    @ Stop generating stress telemetry and events.
    sync command STOP() opcode 0x01

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Monotonically increasing sequence number for each telemetry sample.
    @ The GDS should see every value from 1..N with no gaps.
    telemetry SeqNum: U32 id 0 format "{d}"

    @ A secondary counter that increments by 1 each cycle (not per-sample).
    @ Useful for verifying cycle-level timing.
    telemetry CycleCount: U32 id 1 format "{d}"

    @ Number of telemetry items emitted per scheduler cycle.
    telemetry TlmPerCycle: U32 id 2 format "{d}"

    @ Number of events emitted per scheduler cycle.
    telemetry EventsPerCycle: U32 id 3 format "{d}"

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ Emitted on each stress event. seqNum increases monotonically.
    event StressEvent(
      seqNum: U32 @< monotonic sequence number
    ) severity activity high id 0 format "StressEvent seq={}"

    @ Stress test started with given rates.
    event StressStarted(
      tlmPerCycle: U32 @< telemetry per cycle
      eventsPerCycle: U32 @< events per cycle
    ) severity activity high id 1 format "StressTest started: tlm/cycle={} evt/cycle={}"

    @ Stress test stopped.
    event StressStopped(
    ) severity activity high id 2 format "StressTest stopped"

    # ----------------------------------------------------------------------
    # Interfaces
    # ----------------------------------------------------------------------
    import Fw.Event
    import Fw.Command
    import Fw.Channel

  }

}
