/*
 * EventManagerTester.cpp
 *
 *  Created on: Mar 18, 2015
 *      Author: tcanham
 */

#include <gtest/gtest.h>
#include <Fw/Com/ComBuffer.hpp>
#include <Fw/Com/ComPacket.hpp>
#include <Fw/Test/UnitTest.hpp>
#include <Os/IntervalTimer.hpp>
#include <Svc/EventManager/test/ut/EventManagerTester.hpp>

#include <cstdio>

namespace Svc {

typedef EventManager_Enabled Enabled;
typedef EventManager_FilterSeverity FilterSeverity;

EventManagerTester::EventManagerTester(Svc::EventManager& inst)
    : Svc::EventManagerGTestBase("testerbase", 100),
      m_impl(inst),
      m_receivedPacket(false),
      m_receivedPortNum(-1),
      m_receivedFatalEvent(false) {}

EventManagerTester::~EventManagerTester() {
    this->m_impl.deinit();
}

void EventManagerTester::from_PktSend_handler(const FwIndexType portNum,  //!< The port number
                                              Fw::ComBuffer& data,        //!< Buffer containing packet data
                                              U32 context                 //!< context; not used
) {
    this->m_sentPacket = data;
    this->m_receivedPacket = true;
    this->m_receivedPortNum = portNum;
}

void EventManagerTester::from_FatalAnnounce_handler(const FwIndexType portNum,  //!< The port number
                                                    FwEventIdType Id            //!< The ID of the FATAL event
) {
    this->m_receivedFatalEvent = true;
    this->m_fatalID = Id;
}

// ---------------------------------------------------------------------------
// runEventNominal — basic event round-trip; WARNING_HI must arrive on port 1
// ---------------------------------------------------------------------------
void EventManagerTester::runEventNominal() {
    REQUIREMENT("AL-001");
    this->writeEvent(29, Fw::LogSeverity::WARNING_HI, 10);
    // WARNING_HI → port index 1
    ASSERT_EQ(this->m_receivedPortNum,
              static_cast<FwIndexType>(Fw::LogSeverity::WARNING_HI - 1));
}

// ---------------------------------------------------------------------------
// runWithFilters — exercises SET_EVENT_FILTER for one severity level.
//
// FilterMode == 1 (threshold):
//   ENABLED  → minSeverity = level; the event passes and lands on its port.
//   DISABLED → minSeverity drops below level; the event is filtered.
//
// FilterMode == 0 (per-level legacy):
//   ENABLED  → that level passes.
//   DISABLED → that level is filtered.
// ---------------------------------------------------------------------------
void EventManagerTester::runWithFilters(Fw::LogSeverity filter) {
    REQUIREMENT("AL-002");

    Fw::LogBuffer buff;
    U32 val = 10;
    FwEventIdType id = 29;

    Fw::SerializeStatus stat = buff.serializeFrom(val);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    Fw::Time timeTag(TimeBase::TB_NONE, 0, 0);
    U32 cmdSeq = 21;

    FilterSeverity reportFilterLevel = FilterSeverity::WARNING_HI;

    switch (filter.e) {
        case Fw::LogSeverity::WARNING_HI:
            reportFilterLevel = FilterSeverity::WARNING_HI;
            break;
        case Fw::LogSeverity::WARNING_LO:
            reportFilterLevel = FilterSeverity::WARNING_LO;
            break;
        case Fw::LogSeverity::COMMAND:
            reportFilterLevel = FilterSeverity::COMMAND;
            break;
        case Fw::LogSeverity::ACTIVITY_HI:
            reportFilterLevel = FilterSeverity::ACTIVITY_HI;
            break;
        case Fw::LogSeverity::ACTIVITY_LO:
            reportFilterLevel = FilterSeverity::ACTIVITY_LO;
            break;
        case Fw::LogSeverity::DIAGNOSTIC:
            reportFilterLevel = FilterSeverity::DIAGNOSTIC;
            break;
        default:
            ASSERT_TRUE(false);
            break;
    }

    // --- Phase 1: ENABLE the level; the event must pass and land on its port ---
    this->clearHistory();
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, reportFilterLevel, Enabled::ENABLED);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::OK);

    this->m_receivedPacket = false;
    this->m_receivedPortNum = -1;

    this->invoke_to_LogRecv(0, id, timeTag, filter, buff);

    // not yet dispatched — packet still in queue
    ASSERT_FALSE(this->m_receivedPacket);
    this->m_impl.doDispatch();
    ASSERT_TRUE(this->m_receivedPacket);

    // verify port index matches severity
    ASSERT_EQ(this->m_receivedPortNum, static_cast<FwIndexType>(filter.e - 1));

    // verify packet contents
    FwPacketDescriptorType desc;
    stat = this->m_sentPacket.deserializeTo(desc);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(desc, static_cast<FwPacketDescriptorType>(Fw::ComPacketType::FW_PACKET_LOG));
    FwEventIdType sentId;
    stat = this->m_sentPacket.deserializeTo(sentId);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(sentId, id);
    Fw::Time recTimeTag(TimeBase::TB_NONE, 0, 0);
    stat = this->m_sentPacket.deserializeTo(recTimeTag);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_TRUE(timeTag == recTimeTag);
    U32 readVal;
    stat = this->m_sentPacket.deserializeTo(readVal);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(readVal, val);
    ASSERT_EQ(this->m_sentPacket.getDeserializeSizeLeft(), 0u);

    // --- Phase 2: DISABLE the level; the event must be dropped ---
    this->clearHistory();
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, reportFilterLevel, Enabled::DISABLED);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::OK);

    this->m_receivedPacket = false;

    this->invoke_to_LogRecv(0, id, timeTag, filter, buff);

    // should not have received a packet (dropped in LogRecv_handler)
    ASSERT_FALSE(this->m_receivedPacket);
}

void EventManagerTester::runFilterInvalidCommands() {
    U32 cmdSeq = 21;
    this->clearHistory();
    FilterSeverity reportFilterLevel = FilterSeverity::WARNING_HI;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) intentional invalid test
    Enabled filterEnabled(static_cast<Enabled::t>(10));
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, reportFilterLevel, filterEnabled);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::FORMAT_ERROR);
    this->clearHistory();
    reportFilterLevel = FilterSeverity::WARNING_HI;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) intentional invalid test
    filterEnabled.e = static_cast<Enabled::t>(-2);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, reportFilterLevel, filterEnabled);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::FORMAT_ERROR);
    FilterSeverity eventLevel;
    this->clearHistory();
    Enabled reportEnable = Enabled::ENABLED;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) intentional invalid test
    eventLevel.e = static_cast<FilterSeverity::t>(-1);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, eventLevel, reportEnable);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::FORMAT_ERROR);

    this->clearHistory();

    reportEnable = Enabled::ENABLED;
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange) intentional invalid test
    eventLevel.e = static_cast<FilterSeverity::t>(100);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, eventLevel, reportEnable);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::FORMAT_ERROR);
}

void EventManagerTester::runFilterEventNominal() {
    for (Fw::LogSeverity::t sev = Fw::LogSeverity::WARNING_HI; sev <= Fw::LogSeverity::DIAGNOSTIC;
         sev = static_cast<Fw::LogSeverity::t>(sev + 1)) {
        this->runWithFilters(sev);
    }
}

void EventManagerTester::runFilterIdNominal() {
    U32 cmdSeq = 21;

    REQUIREMENT("AL-003");

    for (FwSizeType filterID = 1; filterID <= TELEM_ID_FILTER_SIZE; filterID++) {
        this->clearHistory();
        this->clearEvents();
        this->sendCmd_SET_ID_FILTER(0, cmdSeq, filterID, Enabled::ENABLED);
        // dispatch message
        this->m_impl.doDispatch();
        ASSERT_CMD_RESPONSE_SIZE(1);
        ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_ID_FILTER, cmdSeq, Fw::CmdResponse::OK);
        ASSERT_EVENTS_SIZE(1);
        ASSERT_EVENTS_ID_FILTER_ENABLED_SIZE(1);
        ASSERT_EVENTS_ID_FILTER_ENABLED(0, filterID);
        // send it again, to verify it will accept a second add
        this->clearHistory();
        this->clearEvents();
        this->sendCmd_SET_ID_FILTER(0, cmdSeq, filterID, Enabled::ENABLED);
        // dispatch message
        this->m_impl.doDispatch();
        ASSERT_CMD_RESPONSE_SIZE(1);
        ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_ID_FILTER, cmdSeq, Fw::CmdResponse::OK);
        ASSERT_EVENTS_SIZE(1);
        ASSERT_EVENTS_ID_FILTER_ENABLED_SIZE(1);
        ASSERT_EVENTS_ID_FILTER_ENABLED(0, filterID);
    }

    // Try to send the IDs that are filtered
    for (FwSizeType filterID = 1; filterID <= TELEM_ID_FILTER_SIZE; filterID++) {
        this->clearHistory();
        this->clearEvents();

        Fw::LogBuffer buff;
        U32 val = 10;
        FwEventIdType id = filterID;

        Fw::SerializeStatus stat = buff.serializeFrom(val);
        ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
        Fw::Time timeTag(TimeBase::TB_NONE, 0, 0);

        this->m_receivedPacket = false;

        this->invoke_to_LogRecv(0, id, timeTag, Fw::LogSeverity::ACTIVITY_HI, buff);

        // should not get a packet
        ASSERT_FALSE(this->m_receivedPacket);
    }

    // send one of the IDs as a FATAL, it should not be filtered even though the ID is in the filter
    this->clearHistory();
    this->clearEvents();

    Fw::LogBuffer buff;
    U32 val = 10;
    FwEventIdType id = 1;

    Fw::SerializeStatus stat = buff.serializeFrom(val);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    Fw::Time timeTag(TimeBase::TB_NONE, 0, 0);

    this->m_receivedPacket = false;

    this->invoke_to_LogRecv(0, id, timeTag, Fw::LogSeverity::FATAL, buff);
    this->m_impl.doDispatch();

    // should get a packet anyway
    ASSERT_TRUE(this->m_receivedPacket);
    // FATAL must be on port 0
    ASSERT_EQ(this->m_receivedPortNum, static_cast<FwIndexType>(0));

    // Try to add to the full filter. It should be rejected
    this->clearHistory();
    this->clearEvents();
    this->sendCmd_SET_ID_FILTER(0, cmdSeq, TELEM_ID_FILTER_SIZE + 1, Enabled::ENABLED);
    // dispatch message
    this->m_impl.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_ID_FILTER, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_ID_FILTER_LIST_FULL_SIZE(1);
    ASSERT_EVENTS_ID_FILTER_LIST_FULL(0, TELEM_ID_FILTER_SIZE + 1);

    // Now clear them
    for (FwSizeType filterID = 1; filterID <= TELEM_ID_FILTER_SIZE; filterID++) {
        this->clearHistory();
        this->clearEvents();
        this->sendCmd_SET_ID_FILTER(0, cmdSeq, filterID, Enabled::DISABLED);
        // dispatch message
        this->m_impl.doDispatch();
        ASSERT_CMD_RESPONSE_SIZE(1);
        ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_ID_FILTER, cmdSeq, Fw::CmdResponse::OK);
        ASSERT_EVENTS_SIZE(1);
        ASSERT_EVENTS_ID_FILTER_REMOVED_SIZE(1);
        ASSERT_EVENTS_ID_FILTER_REMOVED(0, filterID);
    }

    // Try to clear one that doesn't exist
    this->clearHistory();
    this->clearEvents();
    this->sendCmd_SET_ID_FILTER(0, cmdSeq, 10, Enabled::DISABLED);
    // dispatch message
    this->m_impl.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_ID_FILTER, cmdSeq, Fw::CmdResponse::EXECUTION_ERROR);
    ASSERT_EVENTS_SIZE(1);
    ASSERT_EVENTS_ID_FILTER_NOT_FOUND_SIZE(1);
    ASSERT_EVENTS_ID_FILTER_NOT_FOUND(0, 10);

    // Send an invalid argument
    this->clearHistory();
    this->clearEvents();
    Enabled idEnabled(static_cast<Enabled::t>(10));
    this->sendCmd_SET_ID_FILTER(0, cmdSeq, 10, idEnabled);
    // dispatch message
    this->m_impl.doDispatch();
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_ID_FILTER, cmdSeq, Fw::CmdResponse::FORMAT_ERROR);
    ASSERT_EVENTS_SIZE(0);
}

// ---------------------------------------------------------------------------
// runFilterDump — verifies DUMP_FILTER_STATE reflects the current threshold.
//
// FilterMode == 1: each FilterSeverity level is reported enabled iff its
//   corresponding LogSeverity value is <= m_minSeverity.
// FilterMode == 0: reports the per-level enable/disable states directly.
// ---------------------------------------------------------------------------
void EventManagerTester::runFilterDump() {
    U32 cmdSeq = 21;

    if (EventManagerCfg::FilterMode == 1) {
        // ----------------------------------------------------------------
        // Threshold mode: set threshold to COMMAND (FilterSeverity.e=2,
        // LogSeverity.e=4).  Levels at or below COMMAND (FATAL, WARNING_HI,
        // WARNING_LO, COMMAND) should report enabled=true; levels above
        // (ACTIVITY_HI, ACTIVITY_LO, DIAGNOSTIC) should report enabled=false.
        // ----------------------------------------------------------------
        this->clearHistory();
        this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::COMMAND, Enabled::ENABLED);
        ASSERT_CMD_RESPONSE_SIZE(1);
        ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::OK);

        this->sendCmd_SET_ID_FILTER(0, cmdSeq, 4, Enabled::ENABLED);
        this->m_impl.doDispatch();
        this->sendCmd_SET_ID_FILTER(0, cmdSeq, 13, Enabled::ENABLED);
        this->m_impl.doDispatch();

        this->clearHistory();
        this->clearEvents();
        this->sendCmd_DUMP_FILTER_STATE(0, cmdSeq);
        this->m_impl.doDispatch();
        ASSERT_CMD_RESPONSE_SIZE(1);
        ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_DUMP_FILTER_STATE, cmdSeq, Fw::CmdResponse::OK);

        // 6 severity events + 2 ID filter events
        ASSERT_EVENTS_SIZE(6 + 2);
        ASSERT_EVENTS_SEVERITY_FILTER_STATE_SIZE(6);
        // FilterSeverity → LogSeverity: WARNING_HI(0)→2, WARNING_LO(1)→3,
        // COMMAND(2)→4, ACTIVITY_HI(3)→5, ACTIVITY_LO(4)→6, DIAGNOSTIC(5)→7
        // threshold is COMMAND (LogSeverity=4): pass if logSev.e <= 4
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(0, FilterSeverity::WARNING_HI,  true);   // 2 <= 4
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(1, FilterSeverity::WARNING_LO,  true);   // 3 <= 4
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(2, FilterSeverity::COMMAND,     true);   // 4 <= 4
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(3, FilterSeverity::ACTIVITY_HI, false);  // 5 > 4
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(4, FilterSeverity::ACTIVITY_LO, false);  // 6 > 4
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(5, FilterSeverity::DIAGNOSTIC,  false);  // 7 > 4

        // Clean up ID filter
        this->sendCmd_SET_ID_FILTER(0, cmdSeq, 4, Enabled::DISABLED);
        this->m_impl.doDispatch();
        this->sendCmd_SET_ID_FILTER(0, cmdSeq, 13, Enabled::DISABLED);
        this->m_impl.doDispatch();

        // Restore default threshold (ACTIVITY_LO)
        this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::ACTIVITY_LO, Enabled::ENABLED);
    } else {
        // ----------------------------------------------------------------
        // Legacy per-level mode: original test logic
        // ----------------------------------------------------------------
        this->sendCmd_SET_EVENT_FILTER(0, 0, FilterSeverity::WARNING_HI, Enabled::ENABLED);
        this->sendCmd_SET_EVENT_FILTER(0, 0, FilterSeverity::WARNING_LO, Enabled::DISABLED);
        this->sendCmd_SET_EVENT_FILTER(0, 0, FilterSeverity::COMMAND, Enabled::ENABLED);
        this->sendCmd_SET_EVENT_FILTER(0, 0, FilterSeverity::ACTIVITY_HI, Enabled::DISABLED);
        this->sendCmd_SET_EVENT_FILTER(0, 0, FilterSeverity::ACTIVITY_LO, Enabled::ENABLED);
        this->sendCmd_SET_EVENT_FILTER(0, 0, FilterSeverity::DIAGNOSTIC, Enabled::ENABLED);

        this->sendCmd_SET_ID_FILTER(0, cmdSeq, 4, Enabled::ENABLED);
        this->m_impl.doDispatch();
        this->sendCmd_SET_ID_FILTER(0, cmdSeq, 13, Enabled::ENABLED);
        this->m_impl.doDispatch();
        this->sendCmd_SET_ID_FILTER(0, cmdSeq, 4000, Enabled::ENABLED);
        this->m_impl.doDispatch();

        this->clearHistory();
        this->clearEvents();
        this->sendCmd_DUMP_FILTER_STATE(0, cmdSeq);
        this->m_impl.doDispatch();
        ASSERT_CMD_RESPONSE_SIZE(1);
        ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_DUMP_FILTER_STATE, cmdSeq, Fw::CmdResponse::OK);
        ASSERT_EVENTS_SIZE(6 + 3);
        ASSERT_EVENTS_SEVERITY_FILTER_STATE_SIZE(6);
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(0, FilterSeverity::WARNING_HI, true);
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(1, FilterSeverity::WARNING_LO, false);
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(2, FilterSeverity::COMMAND, true);
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(3, FilterSeverity::ACTIVITY_HI, false);
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(4, FilterSeverity::ACTIVITY_LO, true);
        ASSERT_EVENTS_SEVERITY_FILTER_STATE(5, FilterSeverity::DIAGNOSTIC, true);
    }
}

void EventManagerTester::runEventFatal() {
    Fw::LogBuffer buff;
    U32 val = 10;
    FwEventIdType id = 29;
    U32 cmdSeq = 21;
    REQUIREMENT("AL-004");

    Fw::SerializeStatus stat = buff.serializeFrom(val);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    Fw::Time timeTag(TimeBase::TB_NONE, 0, 0);

    this->m_receivedPacket = false;
    this->m_receivedPortNum = -1;

    this->invoke_to_LogRecv(0, id, timeTag, Fw::LogSeverity::FATAL, buff);

    // should not have received packet
    ASSERT_FALSE(this->m_receivedPacket);
    // should have seen FATAL announce synchronously
    ASSERT_TRUE(this->m_receivedFatalEvent);
    ASSERT_EQ(this->m_fatalID, id);
    // dispatch message
    this->m_impl.doDispatch();
    // should have received packet on port 0 (FATAL → port 0)
    ASSERT_TRUE(this->m_receivedPacket);
    ASSERT_EQ(this->m_receivedPortNum, static_cast<FwIndexType>(0));
    // verify contents
    FwPacketDescriptorType desc;
    stat = this->m_sentPacket.deserializeTo(desc);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(desc, static_cast<FwPacketDescriptorType>(Fw::ComPacketType::FW_PACKET_LOG));
    FwEventIdType sentId;
    stat = this->m_sentPacket.deserializeTo(sentId);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(sentId, id);
    Fw::Time recTimeTag(TimeBase::TB_NONE, 0, 0);
    stat = this->m_sentPacket.deserializeTo(recTimeTag);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_TRUE(timeTag == recTimeTag);
    U32 readVal;
    stat = this->m_sentPacket.deserializeTo(readVal);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(readVal, val);
    ASSERT_EQ(this->m_sentPacket.getDeserializeSizeLeft(), 0u);

    // Turn on all filters and make sure FATAL still gets through
    this->clearHistory();
    this->clearEvents();
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::WARNING_HI, Enabled::DISABLED);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::OK);

    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::WARNING_LO, Enabled::DISABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::COMMAND, Enabled::DISABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::ACTIVITY_HI, Enabled::DISABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::ACTIVITY_LO, Enabled::DISABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::DIAGNOSTIC, Enabled::DISABLED);

    this->m_receivedPacket = false;
    this->m_receivedPortNum = -1;

    this->invoke_to_LogRecv(0, id, timeTag, Fw::LogSeverity::FATAL, buff);

    ASSERT_FALSE(this->m_receivedPacket);
    this->m_impl.doDispatch();
    // FATAL must still arrive on port 0
    ASSERT_TRUE(this->m_receivedPacket);
    ASSERT_EQ(this->m_receivedPortNum, static_cast<FwIndexType>(0));
    stat = this->m_sentPacket.deserializeTo(desc);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(desc, static_cast<FwPacketDescriptorType>(Fw::ComPacketType::FW_PACKET_LOG));
    stat = this->m_sentPacket.deserializeTo(sentId);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(sentId, id);
    stat = this->m_sentPacket.deserializeTo(recTimeTag);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_TRUE(timeTag == recTimeTag);
    stat = this->m_sentPacket.deserializeTo(readVal);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(readVal, val);
    ASSERT_EQ(this->m_sentPacket.getDeserializeSizeLeft(), 0u);

    // Restore filters to default (all enabled / threshold = ACTIVITY_LO)
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::WARNING_HI, Enabled::ENABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::WARNING_LO, Enabled::ENABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::COMMAND, Enabled::ENABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::ACTIVITY_HI, Enabled::ENABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::ACTIVITY_LO, Enabled::ENABLED);
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::DIAGNOSTIC, Enabled::ENABLED);
}

// ---------------------------------------------------------------------------
// writeEvent — helper used by runEventNominal.
//   Sends an event, dispatches, then verifies the received packet and that
//   the packet arrived on the port corresponding to the severity.
// ---------------------------------------------------------------------------
void EventManagerTester::writeEvent(FwEventIdType id, Fw::LogSeverity severity, U32 value) {
    Fw::LogBuffer buff;

    Fw::SerializeStatus stat = buff.serializeFrom(value);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    Fw::Time timeTag(TimeBase::TB_NONE, 1, 2);

    this->m_receivedPacket = false;
    this->m_receivedPortNum = -1;

    this->invoke_to_LogRecv(0, id, timeTag, severity, buff);

    ASSERT_FALSE(this->m_receivedPacket);
    this->m_impl.doDispatch();
    ASSERT_TRUE(this->m_receivedPacket);

    // verify packet arrives on the correct severity port
    ASSERT_EQ(this->m_receivedPortNum, static_cast<FwIndexType>(severity.e - 1));

    FwPacketDescriptorType desc;
    stat = this->m_sentPacket.deserializeTo(desc);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(desc, static_cast<FwPacketDescriptorType>(Fw::ComPacketType::FW_PACKET_LOG));
    FwEventIdType sentId;
    stat = this->m_sentPacket.deserializeTo(sentId);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(sentId, id);
    Fw::Time recTimeTag(TimeBase::TB_NONE, 1, 2);
    stat = this->m_sentPacket.deserializeTo(recTimeTag);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_TRUE(timeTag == recTimeTag);
    U32 readVal;
    stat = this->m_sentPacket.deserializeTo(readVal);
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, stat);
    ASSERT_EQ(readVal, value);
    ASSERT_EQ(this->m_sentPacket.getDeserializeSizeLeft(), 0u);
}

void EventManagerTester::readEvent(FwEventIdType id, Fw::LogSeverity severity, U32 value, Os::File& file) {
    static const BYTE delimiter = 0xA5;

    BYTE de;
    FwSizeType readSize = static_cast<FwSizeType>(sizeof(de));

    ASSERT_EQ(file.read(&de, readSize, Os::File::WaitType::WAIT), Os::File::OP_OK);
    ASSERT_EQ(delimiter, de);
    Fw::ComBuffer comBuff;
    readSize = sizeof(FwPacketDescriptorType) + sizeof(FwEventIdType) + Fw::Time::SERIALIZED_SIZE + sizeof(U32);
    ASSERT_EQ(file.read(comBuff.getBuffAddr(), readSize, Os::File::WaitType::WAIT), Os::File::OP_OK);
    comBuff.setBuffLen(readSize);

    Fw::LogPacket packet;
    Fw::Time time(TimeBase::TB_NONE, 1, 2);
    Fw::LogBuffer logBuff;
    ASSERT_EQ(comBuff.deserializeTo(packet), Fw::FW_SERIALIZE_OK);

    ASSERT_EQ(id, packet.getId());
    ASSERT_EQ(time, packet.getTimeTag());
    logBuff = packet.getLogBuffer();
    U32 readValue;
    ASSERT_EQ(logBuff.deserializeTo(readValue), Fw::FW_SERIALIZE_OK);
    ASSERT_EQ(value, readValue);
}

// ---------------------------------------------------------------------------
// runSeverityPortRouting (new Red-phase test)
//   Verifies that every Fw::LogSeverity value is routed to port (severity-1).
// ---------------------------------------------------------------------------
void EventManagerTester::runSeverityPortRouting() {
    REQUIREMENT("AL-005");

    // Ensure all severities pass the filter: set threshold to DIAGNOSTIC
    U32 cmdSeq = 99;
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::DIAGNOSTIC, Enabled::ENABLED);

    struct TestCase {
        Fw::LogSeverity severity;
        FwIndexType expectedPort;
    };

    // Fw::LogSeverity: FATAL=1, WARNING_HI=2, WARNING_LO=3, COMMAND=4,
    //                  ACTIVITY_HI=5, ACTIVITY_LO=6, DIAGNOSTIC=7
    const TestCase cases[] = {
        {Fw::LogSeverity::FATAL,       0},
        {Fw::LogSeverity::WARNING_HI,  1},
        {Fw::LogSeverity::WARNING_LO,  2},
        {Fw::LogSeverity::COMMAND,     3},
        {Fw::LogSeverity::ACTIVITY_HI, 4},
        {Fw::LogSeverity::ACTIVITY_LO, 5},
        {Fw::LogSeverity::DIAGNOSTIC,  6},
    };

    for (const auto& tc : cases) {
        this->clearHistory();
        FwEventIdType id = 42;

        Fw::LogBuffer buff;
        U32 val = 5;
        ASSERT_EQ(Fw::FW_SERIALIZE_OK, buff.serializeFrom(val));
        Fw::Time timeTag(TimeBase::TB_NONE, 0, 0);

        this->m_receivedPacket = false;
        this->m_receivedPortNum = -1;

        this->invoke_to_LogRecv(0, id, timeTag, tc.severity, buff);
        this->m_impl.doDispatch();

        ASSERT_TRUE(this->m_receivedPacket)
            << "No packet for severity " << tc.severity.e;
        ASSERT_EQ(this->m_receivedPortNum, tc.expectedPort)
            << "Wrong port for severity " << tc.severity.e
            << ": expected " << tc.expectedPort
            << ", got " << this->m_receivedPortNum;
    }

    // Restore default threshold
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::ACTIVITY_LO, Enabled::ENABLED);
}

// ---------------------------------------------------------------------------
// runMinSeverityFilter (new Red-phase test)
//   Verifies threshold-mode: events below the threshold are dropped; events
//   at or above the threshold are forwarded to the correct port.
// ---------------------------------------------------------------------------
void EventManagerTester::runMinSeverityFilter() {
    REQUIREMENT("AL-006");

    U32 cmdSeq = 98;

    // Set threshold to WARNING_LO (FilterSeverity.e=1, LogSeverity.e=3).
    // FATAL (1) and WARNING_HI (2) and WARNING_LO (3) should pass.
    // COMMAND (4) and less severe should be dropped.
    this->clearHistory();
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::WARNING_LO, Enabled::ENABLED);
    ASSERT_CMD_RESPONSE_SIZE(1);
    ASSERT_CMD_RESPONSE(0, EventManager::OPCODE_SET_EVENT_FILTER, cmdSeq, Fw::CmdResponse::OK);

    Fw::LogBuffer buff;
    U32 val = 7;
    FwEventIdType id = 55;
    ASSERT_EQ(Fw::FW_SERIALIZE_OK, buff.serializeFrom(val));
    Fw::Time timeTag(TimeBase::TB_NONE, 0, 0);

    // These must pass (severity.e <= 3)
    const Fw::LogSeverity passing[] = {
        Fw::LogSeverity::FATAL,
        Fw::LogSeverity::WARNING_HI,
        Fw::LogSeverity::WARNING_LO,
    };
    for (const auto& sev : passing) {
        this->m_receivedPacket = false;
        this->invoke_to_LogRecv(0, id, timeTag, sev, buff);
        this->m_impl.doDispatch();
        ASSERT_TRUE(this->m_receivedPacket)
            << "Expected forwarded packet for severity " << sev.e;
        ASSERT_EQ(this->m_receivedPortNum, static_cast<FwIndexType>(sev.e - 1));
    }

    // These must be dropped (severity.e > 3); FATAL still passes but isn't in this list
    const Fw::LogSeverity dropping[] = {
        Fw::LogSeverity::COMMAND,
        Fw::LogSeverity::ACTIVITY_HI,
        Fw::LogSeverity::ACTIVITY_LO,
        Fw::LogSeverity::DIAGNOSTIC,
    };
    for (const auto& sev : dropping) {
        this->m_receivedPacket = false;
        this->invoke_to_LogRecv(0, id, timeTag, sev, buff);
        // No dispatch needed: dropped synchronously in LogRecv_handler
        ASSERT_FALSE(this->m_receivedPacket)
            << "Unexpected forwarded packet for severity " << sev.e;
    }

    // Restore default
    this->sendCmd_SET_EVENT_FILTER(0, cmdSeq, FilterSeverity::ACTIVITY_LO, Enabled::ENABLED);
}

void EventManagerTester::textLogIn(const FwEventIdType id,
                                   const Fw::Time& timeTag,
                                   const Fw::LogSeverity severity,
                                   const Fw::TextLogString& text) {
    TextLogEntry e = {id, timeTag, severity, text};
    printTextLogHistoryEntry(e, stdout);
}

void EventManagerTester ::from_pingOut_handler(const FwIndexType portNum, U32 key) {
    this->pushFromPortEntry_pingOut(key);
}

}  // namespace Svc
