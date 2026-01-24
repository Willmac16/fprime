# TimeDemo Component

## Overview

TimeDemo is a demonstration component that shows how to use `Fw::Time` as a command argument and `Fw::TimeValue` as a telemetry channel type in F Prime.

## Purpose

This component demonstrates:
1. Accepting `Fw::Time` as a command parameter
2. Converting `Fw::Time` to `Fw::TimeValue`
3. Emitting `Fw::TimeValue` as telemetry

## Component Interface

### Commands

- **SET_TIME** (opcode 0x0)
  - **Parameters**: `timeValue: Fw.Time` - The time value to set and telemeter
  - **Description**: Accepts an Fw::Time value, logs an event, converts it to Fw::TimeValue, and emits it as telemetry

### Events

- **TIME_SET** (id 0x0, severity: ACTIVITY_LOW)
  - **Parameters**: `timeValue: Fw.Time` - The time value that was set
  - **Description**: Emitted when a time value is successfully set via the SET_TIME command

### Telemetry

- **CurrentTimeValue** (id 0x0, type: Fw.TimeValue)
  - **Description**: The current time value, updated when SET_TIME command is received
  - **Fields**: Contains timeBase, timeContext, seconds, and useconds

## Implementation Details

The component performs the following steps when the SET_TIME command is received:

1. Logs a TIME_SET event with the received Fw::Time value
2. Converts Fw::Time to Fw::TimeValue by extracting:
   - `timeBase` - The time base (e.g., TB_PROC_TIME, TB_WORKSTATION_TIME)
   - `timeContext` - The time context
   - `seconds` - Seconds portion of the time
   - `useconds` - Microseconds portion of the time
3. Writes the Fw::TimeValue to the CurrentTimeValue telemetry channel
4. Sends command response indicating success

## Key Insights

### Fw::Time vs Fw::TimeValue

- **Fw::Time**: A C++ class used primarily for timestamping ports, events, and telemetry
- **Fw::TimeValue**: A dictionary struct (as of Dec 2025) that can be used in commands, events, and telemetry channels

### Usage Pattern

This component demonstrates the typical pattern for working with time values:
- Accept Fw::Time in commands (the full C++ object)
- Convert to Fw::TimeValue for telemetry (the serializable struct)
- Ground systems can deserialize Fw::TimeValue and display all time components

## Files

- `TimeDemo.fpp` - FPP component definition
- `TimeDemoComponentImpl.hpp` - Component implementation header
- `TimeDemoComponentImpl.cpp` - Component implementation source
- `CMakeLists.txt` - Build configuration
