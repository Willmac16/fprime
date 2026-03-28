module Ref {

  # Complex types for generating heavy telemetry payloads
  array StressF32Array = [16] F32

  struct StressPayload {
    seqNum: U32
    cycle: U32
    values: StressF32Array
    tag: string size 40
  }

  array StressPayloadBatch = [4] StressPayload

  @ Component for stress-testing GDS telemetry and event throughput.
  @ Generates deterministic, sequenced telemetry and events at configurable
  @ rates to identify lag and data loss in the GDS pipeline.
  @ Supports complex types (structs, arrays, strings) to produce heavy payloads.
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
    telemetry SeqNum: U32 id 0 format "{d}"

    @ A secondary counter that increments by 1 each cycle (not per-sample).
    telemetry CycleCount: U32 id 1 format "{d}"

    @ Number of telemetry items emitted per scheduler cycle.
    telemetry TlmPerCycle: U32 id 2 format "{d}"

    @ Number of events emitted per scheduler cycle.
    telemetry EventsPerCycle: U32 id 3 format "{d}"

    @ Heavy payload: 16-element F32 array, written every cycle
    telemetry BigArray: StressF32Array id 4

    @ Heavy payload: struct with nested array and string, written every cycle
    telemetry BigStruct: StressPayload id 5

    @ Heavy payload: array of 4 structs, each with nested array and string
    telemetry BigBatch: StressPayloadBatch id 6

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ Emitted on each stress event. seqNum increases monotonically.
    event StressEvent(
      seqNum: U32 @< monotonic sequence number
    ) severity activity high id 0 format "StressEvent seq={}"

    @ Heavy event with complex payload
    event StressBigEvent(
      seqNum: U32 @< monotonic sequence number
      payload: StressPayload @< complex struct payload
    ) severity activity high id 3 format "StressBigEvent seq={} payload={}"

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
