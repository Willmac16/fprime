/*
 * EventManager.hpp
 *
 *  Created on: Mar 28, 2014
 *      Author: tcanham
 *
 * Tweaked to route incoming events to severity-indexed PktSend ports rather
 * than a single port, and to support a minimum-severity threshold filter
 * (EventManagerCfg::FilterMode == 1) alongside the legacy per-severity
 * enable/disable mode (EventManagerCfg::FilterMode == 0).
 */

#ifndef Svc_EventManager_HPP_
#define Svc_EventManager_HPP_

#include <Fw/Log/LogPacket.hpp>
#include <Svc/EventManager/EventManagerComponentAc.hpp>
#include <config/EventManagerCfg.hpp>

namespace Svc {

class EventManager final : public EventManagerComponentBase {
  public:
    //! Number of PktSend output ports (one per Fw::LogSeverity value).
    //! Port index = severity_value - 1  (FATAL=0 … DIAGNOSTIC=6).
    static constexpr FwIndexType NUM_SEVERITY_PORTS = EventManagerCfg::NumSeverityPorts;

    EventManager(const char* compName);  //!< constructor
    virtual ~EventManager();             //!< destructor

  private:
    void LogRecv_handler(FwIndexType portNum,
                         FwEventIdType id,
                         Fw::Time& timeTag,
                         const Fw::LogSeverity& severity,
                         Fw::LogBuffer& args);

    void loqQueue_internalInterfaceHandler(FwEventIdType id,
                                           const Fw::Time& timeTag,
                                           const Fw::LogSeverity& severity,
                                           const Fw::LogBuffer& args);

    void SET_EVENT_FILTER_cmdHandler(FwOpcodeType opCode,
                                     U32 cmdSeq,
                                     EventManager_FilterSeverity filterLevel,
                                     EventManager_Enabled filterEnabled);

    void SET_ID_FILTER_cmdHandler(FwOpcodeType opCode,
                                  U32 cmdSeq,
                                  FwEventIdType ID,
                                  EventManager_Enabled idFilterEnabled);

    void DUMP_FILTER_STATE_cmdHandler(FwOpcodeType opCode,
                                      U32 cmdSeq);

    void pingIn_handler(const FwIndexType portNum, U32 key);

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    //! Map Fw::LogSeverity to the PktSend port index.
    //! Index = severity.e - 1  (FATAL=0 … DIAGNOSTIC=6).
    static FwIndexType severityToPortIndex(Fw::LogSeverity severity);

    //! Convert a FilterSeverity value to the equivalent Fw::LogSeverity.
    //! FilterSeverity omits FATAL; its enum values are offset by 2 from
    //! the corresponding Fw::LogSeverity values.
    static Fw::LogSeverity filterSevToLogSev(EventManager_FilterSeverity fs);

    //! Return true when the event should be forwarded.
    //! Applies both the severity filter and the ID filter; FATAL always passes.
    bool shouldRoute(FwEventIdType id, const Fw::LogSeverity& severity) const;

    // -----------------------------------------------------------------------
    // Filter state — FilterMode 0: per-severity enable/disable (legacy)
    // -----------------------------------------------------------------------
    struct t_filterState {
        EventManager_Enabled enabled;  //<! filter is enabled
    } m_filterState[EventManager_FilterSeverity::NUM_CONSTANTS];

    // -----------------------------------------------------------------------
    // Filter state — FilterMode 1: minimum-severity threshold (new)
    // -----------------------------------------------------------------------
    //! Least-severe severity that is still forwarded (higher enum value =
    //! less severe in fprime). Events with severity.e > m_minSeverity.e are
    //! dropped. FATAL always passes regardless of this value.
    Fw::LogSeverity m_minSeverity;

    // -----------------------------------------------------------------------
    // ID filter (both modes)
    // -----------------------------------------------------------------------
    // array of filtered event IDs; value of 0 means no entry
    FwEventIdType m_filteredIDs[TELEM_ID_FILTER_SIZE];

    // Working members
    Fw::LogPacket m_logPacket;  //!< packet buffer for assembling log packets
    Fw::ComBuffer m_comBuffer;  //!< com buffer for sending event buffers
};

}  // namespace Svc
#endif /* Svc_EventManager_HPP_ */
