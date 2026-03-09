module Ref {

  @ Configuration state that persists across reboots via data products
  struct PersistentState {
    bootCount: U32
    lastMode: U8
    calibrationOffset: F32 format "{f}"
    uptimeAtShutdown: U32
  }

  @ A component that demonstrates using DP deserializeRecord to save and
  @ load state into a data product, persisting across reboots.
  @
  @ On startup, it reads its last-saved DP from disk (via DpCatalog/DpWriter),
  @ deserializes the state record, and resumes from where it left off.
  @ Periodically (or on command), it serializes current state into a DP
  @ and sends it through the DP pipeline to be written to disk.
  queued component StatePersist {

    # ----------------------------------------------------------------------
    # General Ports
    # ----------------------------------------------------------------------

    @ Schedule input - called by rate group to do periodic work
    sync input port schedIn: Svc.Sched

    # ----------------------------------------------------------------------
    # Data Product Ports
    # ----------------------------------------------------------------------

    @ Synchronous DP buffer get
    product get port productGetOut

    @ Asynchronous DP buffer request
    product request port productRequestOut

    @ Asynchronous DP buffer receive
    async product recv port productRecvIn

    @ DP send port
    product send port productSendOut

    # ----------------------------------------------------------------------
    # Special Ports
    # ----------------------------------------------------------------------

    @ Time get port
    time get port timeCaller

    # ----------------------------------------------------------------------
    # Data Product Records and Containers
    # ----------------------------------------------------------------------

    @ State record - holds the full persistent state struct
    product record StateRecord: PersistentState id 0

    @ Container for state snapshots
    product container StateContainer id 0 default priority 10

    # ----------------------------------------------------------------------
    # Commands
    # ----------------------------------------------------------------------

    @ Save current state to a data product
    async command SAVE_STATE opcode 0

    @ Load state from a previously saved data product (manual trigger)
    async command LOAD_STATE opcode 1

    @ Set the operating mode
    async command SET_MODE(mode: U8) opcode 2

    @ Set calibration offset
    async command SET_CALIBRATION(offset: F32) opcode 3

    # ----------------------------------------------------------------------
    # Telemetry
    # ----------------------------------------------------------------------

    @ Current boot count
    telemetry BootCount: U32 id 0

    @ Current operating mode
    telemetry Mode: U8 id 1

    @ Calibration offset
    telemetry CalibrationOffset: F32 id 2 format "{f}"

    @ Uptime in scheduler ticks
    telemetry Uptime: U32 id 3

    @ Whether state has been loaded from disk
    telemetry StateLoaded: bool id 4

    # ----------------------------------------------------------------------
    # Events
    # ----------------------------------------------------------------------

    @ State saved to data product
    event StateSaved(bootCount: U32, mode: U8, uptime: U32) \
      severity activity low \
      format "State saved: bootCount={} mode={} uptime={}"

    @ State loaded from data product
    event StateLoaded(bootCount: U32, mode: U8, calibration: F32, prevUptime: U32) \
      severity activity high \
      format "State restored: bootCount={} mode={} calibration={f} prevUptime={}"

    @ State load failed
    event StateLoadFailed(reason: string size 80) \
      severity warning high \
      format "Failed to load state: {}"

    @ State save failed
    event StateSaveFailed(reason: string size 80) \
      severity warning high \
      format "Failed to save state: {}"

    @ Fresh boot - no prior state found
    event FreshBoot \
      severity activity high \
      format "No prior state found, starting fresh"

    # ----------------------------------------------------------------------
    # Interfaces
    # ----------------------------------------------------------------------

    import Fw.Event
    import Fw.Command
    import Fw.Channel

  }

}
