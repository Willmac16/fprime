/*
 * EventManager.cpp
 *
 *  Created on: Mar 28, 2014
 *      Author: tcanham
 */

#include <cstdio>
#include <limits>

#include <Fw/Types/Assert.hpp>
#include <Os/File.hpp>
#include <Svc/EventManager/EventManager.hpp>
#include <cstring>

namespace Svc {

static_assert(std::numeric_limits<FwSizeType>::max() >= TELEM_ID_FILTER_SIZE,
              "TELEM_ID_FILTER_SIZE must fit within range of FwSizeType");

typedef EventManager_Enabled Enabled;
typedef EventManager_FilterSeverity FilterSeverity;

// ---------------------------------------------------------------------------
// Helper: map Fw::LogSeverity → PktSend port index
// ---------------------------------------------------------------------------

FwIndexType EventManager::severityToPortIndex(Fw::LogSeverity severity) {
    // Fw::LogSeverity values: FATAL=1 … DIAGNOSTIC=7.
    // Port index = value - 1  (FATAL→0, WARNING_HI→1, …, DIAGNOSTIC→6).
    FW_ASSERT(severity.e >= static_cast<Fw::LogSeverity::t>(1) &&
                  severity.e <= static_cast<Fw::LogSeverity::t>(NUM_SEVERITY_PORTS),
              static_cast<FwAssertArgType>(severity.e));
    return static_cast<FwIndexType>(severity.e - 1);
}

// ---------------------------------------------------------------------------
// Helper: FilterSeverity → Fw::LogSeverity
// FilterSeverity omits FATAL; its ordinals are offset by 2 from LogSeverity:
//   FilterSeverity::WARNING_HI (0) <-> Fw::LogSeverity::WARNING_HI (2)
// ---------------------------------------------------------------------------

Fw::LogSeverity EventManager::filterSevToLogSev(EventManager_FilterSeverity fs) {
    return Fw::LogSeverity(static_cast<Fw::LogSeverity::t>(fs.e + 2));
}

// ---------------------------------------------------------------------------
// Helper: (FilterMode 1) rewrite every m_filterState entry so that a level
// is ENABLED iff its LogSeverity <= threshold.
// ---------------------------------------------------------------------------

void EventManager::applyThreshold(Fw::LogSeverity threshold) {
    for (FwEnumStoreType i = 0; i < FilterSeverity::NUM_CONSTANTS; i++) {
        Fw::LogSeverity logSev = filterSevToLogSev(FilterSeverity(static_cast<FilterSeverity::t>(i)));
        m_filterState[i].enabled = (logSev.e <= threshold.e) ? Enabled::ENABLED : Enabled::DISABLED;
    }
}

// ---------------------------------------------------------------------------
// Helper: decide whether to forward this event.
// FATAL always passes.  All other events are checked against m_filterState
// (unified for both modes) and then the ID filter.
// ---------------------------------------------------------------------------

bool EventManager::shouldRoute(FwEventIdType id, const Fw::LogSeverity& severity) const {
    // FATAL is unconditional
    if (severity.e == Fw::LogSeverity::FATAL) {
        return true;
    }

    // Severity filter — same lookup for both modes; only the write path differs
    auto filterSevE = static_cast<FilterSeverity::t>(severity.e - 2);
    if (m_filterState[filterSevE].enabled == Enabled::DISABLED) {
        return false;
    }

    // ID filter
    for (FwSizeType entry = 0; entry < TELEM_ID_FILTER_SIZE; entry++) {
        if (m_filteredIDs[entry] == id) {
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

EventManager::EventManager(const char* name) : EventManagerComponentBase(name) {
    if (EventManagerCfg::FilterMode == 1) {
        // Threshold mode: derive initial m_filterState from the default threshold
        applyThreshold(Fw::LogSeverity(
            static_cast<Fw::LogSeverity::t>(FILTER_MIN_SEVERITY_DEFAULT)));
    } else {
        // Per-level mode: set each entry from its individual HPP default
        m_filterState[FilterSeverity::WARNING_HI].enabled =
            FILTER_WARNING_HI_DEFAULT ? Enabled::ENABLED : Enabled::DISABLED;
        m_filterState[FilterSeverity::WARNING_LO].enabled =
            FILTER_WARNING_LO_DEFAULT ? Enabled::ENABLED : Enabled::DISABLED;
        m_filterState[FilterSeverity::COMMAND].enabled =
            FILTER_COMMAND_DEFAULT ? Enabled::ENABLED : Enabled::DISABLED;
        m_filterState[FilterSeverity::ACTIVITY_HI].enabled =
            FILTER_ACTIVITY_HI_DEFAULT ? Enabled::ENABLED : Enabled::DISABLED;
        m_filterState[FilterSeverity::ACTIVITY_LO].enabled =
            FILTER_ACTIVITY_LO_DEFAULT ? Enabled::ENABLED : Enabled::DISABLED;
        m_filterState[FilterSeverity::DIAGNOSTIC].enabled =
            FILTER_DIAGNOSTIC_DEFAULT ? Enabled::ENABLED : Enabled::DISABLED;
    }

    memset(m_filteredIDs, 0, sizeof(m_filteredIDs));
}

EventManager::~EventManager() {}

// ---------------------------------------------------------------------------
// LogRecv_handler — synchronous path; runs on the caller's thread.
// ---------------------------------------------------------------------------

void EventManager::LogRecv_handler(FwIndexType portNum,
                                   FwEventIdType id,
                                   Fw::Time& timeTag,
                                   const Fw::LogSeverity& severity,
                                   Fw::LogBuffer& args) {
    FW_ASSERT(id != 0);

    if (!shouldRoute(id, severity)) {
        return;
    }

    this->loqQueue_internalInterfaceInvoke(id, timeTag, severity, args);

    if (severity.e == Fw::LogSeverity::FATAL) {
        if (this->isConnected_FatalAnnounce_OutputPort(0)) {
            this->FatalAnnounce_out(0, id);
        }
    }
}

// ---------------------------------------------------------------------------
// loqQueue_internalInterfaceHandler — component thread; serialise and send.
// ---------------------------------------------------------------------------

void EventManager::loqQueue_internalInterfaceHandler(FwEventIdType id,
                                                     const Fw::Time& timeTag,
                                                     const Fw::LogSeverity& severity,
                                                     const Fw::LogBuffer& args) {
    this->m_logPacket.setId(id);
    this->m_logPacket.setTimeTag(timeTag);
    this->m_logPacket.setLogBuffer(args);
    this->m_comBuffer.resetSer();
    Fw::SerializeStatus stat = this->m_logPacket.serializeTo(this->m_comBuffer);
    FW_ASSERT(Fw::FW_SERIALIZE_OK == stat, static_cast<FwAssertArgType>(stat));

    FwIndexType portIdx = severityToPortIndex(severity);
    if (this->isConnected_PktSend_OutputPort(portIdx)) {
        this->PktSend_out(portIdx, this->m_comBuffer, 0);
    }
}

// ---------------------------------------------------------------------------
// SET_EVENT_FILTER_cmdHandler
//   FilterMode 0 (per-level): update just m_filterState[filterLevel].
//   FilterMode 1 (threshold): recompute all of m_filterState via applyThreshold.
//     ENABLED  → threshold = filterLevel's LogSeverity (include it and above)
//     DISABLED → threshold = one step more severe (exclude filterLevel and below)
// ---------------------------------------------------------------------------

void EventManager::SET_EVENT_FILTER_cmdHandler(FwOpcodeType opCode,
                                               U32 cmdSeq,
                                               FilterSeverity filterLevel,
                                               Enabled filterEnabled) {
    if (EventManagerCfg::FilterMode == 1) {
        Fw::LogSeverity logSev = filterSevToLogSev(filterLevel);
        if (filterEnabled == Enabled::ENABLED) {
            applyThreshold(logSev);
        } else {
            // Raise threshold one step; clamp at FATAL so only FATAL slips through
            Fw::LogSeverity::t newThresh =
                static_cast<Fw::LogSeverity::t>(logSev.e - 1);
            if (newThresh < static_cast<Fw::LogSeverity::t>(Fw::LogSeverity::FATAL)) {
                newThresh = static_cast<Fw::LogSeverity::t>(Fw::LogSeverity::FATAL);
            }
            applyThreshold(Fw::LogSeverity(newThresh));
        }
    } else {
        m_filterState[filterLevel.e].enabled = filterEnabled;
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ---------------------------------------------------------------------------
// SET_ID_FILTER_cmdHandler (unchanged from original EventManager)
// ---------------------------------------------------------------------------

void EventManager::SET_ID_FILTER_cmdHandler(FwOpcodeType opCode,
                                            U32 cmdSeq,
                                            FwEventIdType ID,
                                            Enabled idEnabled) {
    if (Enabled::ENABLED == idEnabled.e) {  // add ID
        for (FwSizeType entry = 0; entry < TELEM_ID_FILTER_SIZE; entry++) {
            if (this->m_filteredIDs[entry] == ID) {
                this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
                this->log_ACTIVITY_HI_ID_FILTER_ENABLED(ID);
                return;
            }
        }
        for (FwSizeType entry = 0; entry < TELEM_ID_FILTER_SIZE; entry++) {
            if (this->m_filteredIDs[entry] == 0) {
                this->m_filteredIDs[entry] = ID;
                this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
                this->log_ACTIVITY_HI_ID_FILTER_ENABLED(ID);
                return;
            }
        }
        this->log_WARNING_LO_ID_FILTER_LIST_FULL(ID);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    } else {  // remove ID
        for (FwSizeType entry = 0; entry < TELEM_ID_FILTER_SIZE; entry++) {
            if (this->m_filteredIDs[entry] == ID) {
                this->m_filteredIDs[entry] = 0;
                this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
                this->log_ACTIVITY_HI_ID_FILTER_REMOVED(ID);
                return;
            }
        }
        this->log_WARNING_LO_ID_FILTER_NOT_FOUND(ID);
        this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    }
}

// ---------------------------------------------------------------------------
// DUMP_FILTER_STATE_cmdHandler — reads m_filterState directly (no branching)
// ---------------------------------------------------------------------------

void EventManager::DUMP_FILTER_STATE_cmdHandler(FwOpcodeType opCode,
                                                U32 cmdSeq) {
    for (FwEnumStoreType filter = 0; filter < FilterSeverity::NUM_CONSTANTS; filter++) {
        FilterSeverity filterState(static_cast<FilterSeverity::t>(filter));
        this->log_ACTIVITY_LO_SEVERITY_FILTER_STATE(
            filterState,
            Enabled::ENABLED == m_filterState[filter].enabled.e);
    }

    for (FwSizeType entry = 0; entry < TELEM_ID_FILTER_SIZE; entry++) {
        if (this->m_filteredIDs[entry] != 0) {
            this->log_ACTIVITY_HI_ID_FILTER_ENABLED(this->m_filteredIDs[entry]);
        }
    }

    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

// ---------------------------------------------------------------------------
// pingIn_handler
// ---------------------------------------------------------------------------

void EventManager::pingIn_handler(const FwIndexType portNum, U32 key) {
    this->pingOut_out(0, key);
}

}  // namespace Svc
