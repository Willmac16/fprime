#ifndef Ref_StressTest_HPP
#define Ref_StressTest_HPP

#include "Ref/StressTest/StressTestComponentAc.hpp"

namespace Ref {

class StressTest : public StressTestComponentBase {
  public:
    StressTest(const char* const compName);
    ~StressTest();

  private:
    void schedIn_handler(FwIndexType portNum, U32 context) override;
    void START_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U32 tlmPerCycle, U32 eventsPerCycle) override;
    void STOP_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;

    bool m_running;
    U32 m_tlmPerCycle;
    U32 m_eventsPerCycle;
    U32 m_tlmSeqNum;
    U32 m_evtSeqNum;
    U32 m_cycleCount;
};

}  // namespace Ref

#endif
