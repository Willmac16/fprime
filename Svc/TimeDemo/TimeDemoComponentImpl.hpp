// ======================================================================
// \title  TimeDemoComponentImpl.hpp
// \author Auto-generated
// \brief  hpp file for TimeDemo component implementation class
//
// \copyright
// Copyright (C) 2009-2025 California Institute of Technology.
// ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
//
// ======================================================================

#ifndef Svc_TimeDemoComponentImpl_HPP
#define Svc_TimeDemoComponentImpl_HPP

#include "Svc/TimeDemo/TimeDemoComponentAc.hpp"

namespace Svc {

class TimeDemoComponentImpl : public TimeDemoComponentBase {
  public:
    // ----------------------------------------------------------------------
    // Construction, initialization, and destruction
    // ----------------------------------------------------------------------

    //! Construct object TimeDemo
    //!
    TimeDemoComponentImpl(const char* const compName  //!< The component name
    );

    //! Destroy object TimeDemo
    //!
    ~TimeDemoComponentImpl();

  PRIVATE:
    // ----------------------------------------------------------------------
    // Command handler implementations
    // ----------------------------------------------------------------------

    //! Implementation for SET_TIME command handler
    //! Set a time value to be telemetered
    void SET_TIME_cmdHandler(const FwOpcodeType opCode,  //!< The opcode
                             const U32 cmdSeq,           //!< The command sequence number
                             const Fw::Time& timeValue   //!< The time value to set and telemeter
    );
};

}  // end namespace Svc

#endif
