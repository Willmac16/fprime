/*
 * EventManagerCfg.hpp
 *
 *  Created on: Apr 16, 2015
 *      Author: tcanham
 */

#ifndef Config_EventManagerCfg_HPP_
#define Config_EventManagerCfg_HPP_

// set default filters

// FilterMode = 0 (per-severity enable/disable) defaults
enum {
    FILTER_WARNING_HI_DEFAULT = true,   //!< WARNING HI events are filtered at input
    FILTER_WARNING_LO_DEFAULT = true,   //!< WARNING LO events are filtered at input
    FILTER_COMMAND_DEFAULT = true,      //!< COMMAND events are filtered at input
    FILTER_ACTIVITY_HI_DEFAULT = true,  //!< ACTIVITY HI events are filtered at input
    FILTER_ACTIVITY_LO_DEFAULT = true,  //!< ACTIVITY LO  events are filtered at input
    FILTER_DIAGNOSTIC_DEFAULT = false,  //!< DIAGNOSTIC events are filtered at input
};

// FilterMode = 1 (minimum-severity threshold) default.
// Matches Fw::LogSeverity values: FATAL=1, WARNING_HI=2, WARNING_LO=3,
// COMMAND=4, ACTIVITY_HI=5, ACTIVITY_LO=6, DIAGNOSTIC=7.
// Default of 6 (ACTIVITY_LO) is consistent with the per-level defaults above:
// pass everything down to ACTIVITY_LO, drop DIAGNOSTIC.
// Must stay in sync with EventManagerCfg.fpp::MinSeverityDefault.
enum {
    FILTER_MIN_SEVERITY_DEFAULT = 6,  //!< Default minimum severity (Fw::LogSeverity::ACTIVITY_LO)
};

enum {
    TELEM_ID_FILTER_SIZE = 25,  //!< Size of telemetry ID filter
};

#endif /* Config_EventManagerCfg_HPP_ */
