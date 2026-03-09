#ifndef Ref_StatePersist_HPP
#define Ref_StatePersist_HPP

#include "Ref/StatePersist/StatePersistComponentAc.hpp"

namespace Ref {

class StatePersist : public StatePersistComponentBase {

  public:

    // ----------------------------------------------------------------------
    // Construction, initialization, and destruction
    // ----------------------------------------------------------------------

    StatePersist(const char* const compName);
    ~StatePersist();

  private:

    // ----------------------------------------------------------------------
    // Handler implementations for input ports
    // ----------------------------------------------------------------------

    void schedIn_handler(FwIndexType portNum, U32 context) override;

    // ----------------------------------------------------------------------
    // Command handler implementations
    // ----------------------------------------------------------------------

    void SAVE_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void LOAD_STATE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq) override;
    void SET_MODE_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, U8 mode) override;
    void SET_CALIBRATION_cmdHandler(FwOpcodeType opCode, U32 cmdSeq, F32 offset) override;

    // ----------------------------------------------------------------------
    // Data product handler
    // ----------------------------------------------------------------------

    void dpRecv_StateContainer_handler(DpContainer& container, Fw::Success::T status) override;

    // ----------------------------------------------------------------------
    // Helper methods
    // ----------------------------------------------------------------------

    //! Save current state into a data product and send it
    void saveState();

    //! Load state from a data product container using deserializeRecord
    //! \return true if state was successfully loaded
    bool loadStateFromContainer(DpContainer& container);

    // ----------------------------------------------------------------------
    // Member variables
    // ----------------------------------------------------------------------

    //! Current persistent state
    PersistentState m_state;

    //! Scheduler tick counter (uptime proxy)
    U32 m_ticks;

    //! Whether we have successfully loaded state from a prior DP
    bool m_stateLoaded;

    //! DP container for save operations
    DpContainer m_dpContainer;

    //! Periodic save interval in ticks (save every N ticks)
    static const U32 SAVE_INTERVAL = 100;
};

}  // namespace Ref

#endif
