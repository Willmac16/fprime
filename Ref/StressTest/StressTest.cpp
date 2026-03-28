#include "Ref/StressTest/StressTest.hpp"
#include <Fw/Types/String.hpp>
#include <cmath>
#include <cstdio>

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

Ref::StressPayload StressTest::buildPayload(U32 seqNum) {
    // Build a deterministic complex payload
    Ref::StressF32Array arr;
    for (U32 j = 0; j < 16; j++) {
        arr[j] = static_cast<F32>(seqNum) * 0.01F + static_cast<F32>(j) * 0.001F;
    }

    char tagBuf[41];
    (void)snprintf(tagBuf, sizeof(tagBuf), "stress-seq-%010u-cycle-%06u",
                   static_cast<unsigned>(seqNum),
                   static_cast<unsigned>(m_cycleCount));
    Fw::String tagStr(tagBuf);

    Ref::StressPayload payload(
        seqNum,
        m_cycleCount,
        arr,
        tagStr
    );
    return payload;
}

void StressTest::schedIn_handler(FwIndexType portNum, U32 context) {
    if (!m_running) {
        return;
    }

    m_cycleCount++;

    // === Telemetry: simple SeqNum per sample ===
    for (U32 i = 0; i < m_tlmPerCycle; i++) {
        m_tlmSeqNum++;
        this->tlmWrite_SeqNum(m_tlmSeqNum);
    }

    // === Heavy telemetry: complex types every cycle ===

    // 16-element F32 array (~64 bytes on the wire)
    {
        Ref::StressF32Array arr;
        for (U32 j = 0; j < 16; j++) {
            arr[j] = static_cast<F32>(m_cycleCount) + static_cast<F32>(j) * 0.1F;
        }
        this->tlmWrite_BigArray(arr);
    }

    // Struct with nested array + string (~120 bytes)
    {
        Ref::StressPayload payload = this->buildPayload(m_tlmSeqNum);
        this->tlmWrite_BigStruct(payload);
    }

    // Batch of 4 structs (~480 bytes)
    {
        Ref::StressPayloadBatch batch;
        for (U32 k = 0; k < 4; k++) {
            batch[k] = this->buildPayload(m_tlmSeqNum + k);
        }
        this->tlmWrite_BigBatch(batch);
    }

    // === Events: simple + complex ===
    for (U32 i = 0; i < m_eventsPerCycle; i++) {
        m_evtSeqNum++;
        // Alternate between simple and complex events
        if ((m_evtSeqNum % 2) == 0) {
            this->log_ACTIVITY_HI_StressEvent(m_evtSeqNum);
        } else {
            Ref::StressPayload payload = this->buildPayload(m_evtSeqNum);
            this->log_ACTIVITY_HI_StressBigEvent(m_evtSeqNum, payload);
        }
    }

    // Cycle-level telemetry
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
