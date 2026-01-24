// ======================================================================
// \title  TimeDemoComponentImpl.cpp
// \author Auto-generated
// \brief  cpp file for TimeDemo component implementation class
//
// \copyright
// Copyright (C) 2009-2025 California Institute of Technology.
// ALL RIGHTS RESERVED. United States Government Sponsorship acknowledged.
//
// ======================================================================

#include <Svc/TimeDemo/TimeDemoComponentImpl.hpp>
#include <Fw/Types/BasicTypes.hpp>

namespace Svc {

// ----------------------------------------------------------------------
// Construction and destruction
// ----------------------------------------------------------------------

TimeDemoComponentImpl::TimeDemoComponentImpl(const char* const compName)
    : TimeDemoComponentBase(compName) {}

TimeDemoComponentImpl::~TimeDemoComponentImpl() {}

// ----------------------------------------------------------------------
// Command handler implementations
// ----------------------------------------------------------------------

void TimeDemoComponentImpl::SET_TIME_cmdHandler(const FwOpcodeType opCode,
                                                const U32 cmdSeq,
                                                const Fw::Time& timeValue) {
    // Log the event that time was set
    this->log_ACTIVITY_LO_TIME_SET(timeValue);

    // Convert Fw::Time to Fw::TimeValue for telemetry
    Fw::TimeValue telemetryValue;
    telemetryValue.timeBase = timeValue.getTimeBase();
    telemetryValue.timeContext = timeValue.getContext();
    telemetryValue.seconds = timeValue.getSeconds();
    telemetryValue.useconds = timeValue.getUSeconds();

    // Emit telemetry
    this->tlmWrite_CurrentTimeValue(telemetryValue);

    // Send command response
    this->cmdResponse_out(opCode, cmdSeq, Fw::CmdResponse::OK);
}

}  // end namespace Svc
