// ======================================================================
// StatePersist.cpp
// Demonstrates using DP deserializeRecord to save and load component
// state across reboots via data products.
//
// Flow:
//   1. On boot, LOAD_STATE command (or automatic) reads a previously
//      written DP file through the DpManager pipeline
//   2. The dpRecv handler gets the container, calls setUpForDeserialization(),
//      then deserializeRecord_StateRecord() to recover the PersistentState
//   3. During operation, SAVE_STATE (or periodic auto-save) serializes
//      current state via serializeRecord_StateRecord() and sends it
//      through the DP pipeline to DpWriter, which persists to disk
// ======================================================================

#include "Ref/StatePersist/StatePersist.hpp"

namespace Ref {

// ----------------------------------------------------------------------
// Construction, initialization, and destruction
// ----------------------------------------------------------------------

StatePersist::StatePersist(const char* const compName)
    : StatePersistComponentBase(compName),
      m_state(),
      m_ticks(0),
      m_stateLoaded(false),
      m_dpContainer()
{
    // Initialize default state
    m_state.set_bootCount(0);
    m_state.set_lastMode(0);
    m_state.set_calibrationOffset(0.0f);
    m_state.set_uptimeAtShutdown(0);
}

StatePersist::~StatePersist() {}

// ----------------------------------------------------------------------
// Handler implementations for input ports
// ----------------------------------------------------------------------

void StatePersist::schedIn_handler(FwIndexType portNum, U32 context) {
    // Process queued messages (commands, DP responses)
    this->doDispatch();

    m_ticks++;

    // Periodic auto-save
    if (m_ticks % SAVE_INTERVAL == 0 && m_ticks > 0) {
        this->saveState();
    }

    // Update telemetry
    this->tlmWrite_BootCount(m_state.get_bootCount());
    this->tlmWrite_Mode(m_state.get_lastMode());
    this->tlmWrite_CalibrationOffset(m_state.get_calibrationOffset());
    this->tlmWrite_Uptime(m_ticks);
    this->tlmWrite_StateLoaded(m_stateLoaded);
}

// ----------------------------------------------------------------------
// Command handler implementations
// ----------------------------------------------------------------------

void StatePersist::SAVE_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    this->saveState();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StatePersist::LOAD_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    // Request a DP buffer via async path - when the buffer arrives with
    // a previously-written DP, dpRecv_StateContainer_handler will
    // deserialize the state. In a real system, the DpCatalog would
    // route the stored DP file content back through the recv port.
    //
    // For this demo, we show the full deserialization flow assuming
    // the container arrives populated from a prior save.
    this->dpRequest_StateContainer(PersistentState::SERIALIZED_SIZE + sizeof(FwDpIdType));
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StatePersist::SET_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 mode) {
    m_state.set_lastMode(mode);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StatePersist::SET_CALIBRATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, F32 offset) {
    m_state.set_calibrationOffset(offset);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ----------------------------------------------------------------------
// Data product handler
// ----------------------------------------------------------------------

void StatePersist::dpRecv_StateContainer_handler(
    DpContainer& container,
    Fw::Success::T status)
{
    if (status != Fw::Success::SUCCESS) {
        this->log_WARNING_HI_StateLoadFailed("DP buffer allocation failed");
        return;
    }

    // Try to load state from the received container.
    // In the "load from disk" case, the DpManager/DpCatalog has populated
    // this container with previously-written DP data.
    if (this->loadStateFromContainer(container)) {
        // Increment boot count on successful restore
        m_state.set_bootCount(m_state.get_bootCount() + 1);
        m_stateLoaded = true;

        this->log_ACTIVITY_HI_StateLoaded(
            m_state.get_bootCount(),
            m_state.get_lastMode(),
            m_state.get_calibrationOffset(),
            m_state.get_uptimeAtShutdown()
        );
    } else {
        // Fresh boot - no valid prior state
        m_state.set_bootCount(1);
        m_stateLoaded = false;
        this->log_ACTIVITY_HI_FreshBoot();
    }
}

// ----------------------------------------------------------------------
// Helper methods
// ----------------------------------------------------------------------

void StatePersist::saveState() {
    // Snapshot current uptime into state before saving
    m_state.set_uptimeAtShutdown(m_ticks);

    // Get a DP buffer synchronously
    const FwSizeType dpSize = PersistentState::SERIALIZED_SIZE + sizeof(FwDpIdType);
    Fw::Success stat = this->dpGet_StateContainer(dpSize, m_dpContainer);

    if (stat != Fw::Success::SUCCESS) {
        this->log_WARNING_HI_StateSaveFailed("Failed to get DP buffer");
        return;
    }

    // Serialize the state record into the container
    // This calls the auto-generated serializeRecord_StateRecord which writes:
    //   [record ID (FwDpIdType)] [PersistentState serialized data]
    Fw::SerializeStatus serStat = m_dpContainer.serializeRecord_StateRecord(m_state);

    if (serStat != Fw::FW_SERIALIZE_OK) {
        this->log_WARNING_HI_StateSaveFailed("Serialization failed");
        return;
    }

    // Send the DP through the pipeline (DpManager -> DpWriter -> disk)
    this->dpSend(m_dpContainer);

    this->log_ACTIVITY_LO_StateSaved(
        m_state.get_bootCount(),
        m_state.get_lastMode(),
        m_ticks
    );
}

bool StatePersist::loadStateFromContainer(DpContainer& container) {
    // -----------------------------------------------------------------
    // THIS IS THE KEY PART: using deserializeRecord to read back state
    // -----------------------------------------------------------------
    //
    // The container has arrived from disk (via DpWriter/DpCatalog).
    // It contains a serialized packet with header + data that was
    // previously written by saveState().
    //
    // Step 1: Deserialize the header to validate the container
    Fw::SerializeStatus status = container.deserializeHeader();
    if (status != Fw::FW_SERIALIZE_OK) {
        this->log_WARNING_HI_StateLoadFailed("Header deserialization failed");
        return false;
    }

    // Step 2: Set up the data buffer for record deserialization.
    // This positions the internal buffer pointer at the start of the
    // data region, after the header and header hash.
    container.setUpForDeserialization();

    // Step 3: Deserialize the state record.
    // This calls the auto-generated deserializeRecord_StateRecord which:
    //   1. Reads and validates the record ID
    //   2. Deserializes the PersistentState struct from the buffer
    PersistentState loadedState;
    status = container.deserializeRecord_StateRecord(loadedState);

    if (status != Fw::FW_SERIALIZE_OK) {
        this->log_WARNING_HI_StateLoadFailed("Record deserialization failed");
        return false;
    }

    // Successfully recovered state!
    m_state = loadedState;
    return true;
}

}  // namespace Ref
