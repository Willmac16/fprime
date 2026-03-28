#include "Ref/StressTest/StressTest.hpp"

namespace Ref {

StressTest::StressTest(const char* const compName)
    : StressTestComponentBase(compName),
      m_running(false),
      m_tlmPerCycle(0),
      m_eventsPerCycle(0),
      m_tlmSeqNum(0),
      m_evtSeqNum(0),
      m_cycleCount(0) {}

StressTest::~StressTest() {}

void StressTest::schedIn_handler(FwIndexType portNum, U32 context) {
    if (!m_running) {
        return;
    }

    m_cycleCount++;

    // Emit telemetry: each sample gets a unique, monotonically increasing sequence number
    for (U32 i = 0; i < m_tlmPerCycle; i++) {
        m_tlmSeqNum++;
        this->tlmWrite_SeqNum(m_tlmSeqNum);
    }

    // Emit events: each event gets a unique, monotonically increasing sequence number
    for (U32 i = 0; i < m_eventsPerCycle; i++) {
        m_evtSeqNum++;
        this->log_ACTIVITY_HI_StressEvent(m_evtSeqNum);
    }

    // Emit cycle-level telemetry
    this->tlmWrite_CycleCount(m_cycleCount);
}

void StressTest::START_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 tlmPerCycle, U32 eventsPerCycle) {
    m_tlmPerCycle = tlmPerCycle;
    m_eventsPerCycle = eventsPerCycle;
    m_tlmSeqNum = 0;
    m_evtSeqNum = 0;
    m_cycleCount = 0;
    m_running = true;

    this->tlmWrite_TlmPerCycle(m_tlmPerCycle);
    this->tlmWrite_EventsPerCycle(m_eventsPerCycle);
    this->log_ACTIVITY_HI_StressStarted(tlmPerCycle, eventsPerCycle);
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

void StressTest::STOP_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) {
    m_running = false;
    this->log_ACTIVITY_HI_StressStopped();
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // namespace Ref
