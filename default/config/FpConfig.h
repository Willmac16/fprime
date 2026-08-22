/**
 * \file: FpConfig.h
 * \author T. Canham, mstarch
 * \brief C-compatible configuration header for fprime configuration
 *
 * \copyright
 * Copyright 2009-2015, by the California Institute of Technology.
 * ALL RIGHTS RESERVED.  United States Government Sponsorship
 * acknowledged.
 */
#ifndef FPCONFIG_H_
#define FPCONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <Fw/Types/BasicTypes.h>
#include <Platform/PlatformTypes.h>

// ----------------------------------------------------------------------
// Configuration switches
// ----------------------------------------------------------------------

// Enable strict assertions
#ifndef FW_STRICT_ASSERTIONS
#define FW_STRICT_ASSERTIONS (1)  //!< Indicates whether strict assertions are used (more checking, more instructions)
#endif

// Enable direct port calls
#ifndef FW_DIRECT_PORT_CALLS
#ifdef BUILD_UT
#define FW_DIRECT_PORT_CALLS (0)  //!< Indirect port calls are required for unit tests
#else
#define FW_DIRECT_PORT_CALLS (1)  //!< Indicates whether direct port calls are used (saves space and time)
#endif
#endif

// Allow objects to have names. Allocates storage for each instance
#ifndef FW_OBJECT_NAMES
#define FW_OBJECT_NAMES \
    (1)  //!< Indicates whether or not object names are stored (more memory, can be used for tracking objects)
#endif

// To reduce binary size, FW_OPTIONAL_NAME(<string>) can be used to substitute strings with an empty string
// when running with FW_OBJECT_NAMES disabled
#if FW_OBJECT_NAMES == 1
#define FW_OPTIONAL_NAME(name) name  // NO_CODESONAR  LANG.PREPROC.MACROSTART/END
#else
#define FW_OPTIONAL_NAME(name) ""  // NO_CODESONAR  LANG.PREPROC.MACROSTART/END
#endif

// Add methods to query an object about its name. Can be overridden by derived classes
// For FW_OBJECT_TO_STRING to work, FW_OBJECT_NAMES must be enabled
#if FW_OBJECT_NAMES == 1
#ifndef FW_OBJECT_TO_STRING
#define FW_OBJECT_TO_STRING \
    (1)  //!< Indicates whether or not generated objects have toString() methods to dump internals (more code)
#endif
#else
#define FW_OBJECT_TO_STRING (0)
#endif

// Adds the ability for all component related objects to register
// centrally.
#ifndef FW_OBJECT_REGISTRATION
#define FW_OBJECT_REGISTRATION \
    (1)  //!< Indicates whether or not objects can register themselves (more code, more object tracking)
#endif

#ifndef FW_QUEUE_REGISTRATION
#define FW_QUEUE_REGISTRATION (1)  //!< Indicates whether or not queue registration is used
#endif

// Port Facilities

// This allows tracing calls through ports for debugging
#ifndef FW_PORT_TRACING
#define FW_PORT_TRACING (1)  //!< Indicates whether port calls are traced (more code, more visibility into execution)
#endif

// This generates code to connect to serialized ports
#ifndef FW_PORT_SERIALIZATION
#define FW_PORT_SERIALIZATION \
    (1)  //!< Indicates whether there is code in ports to serialize the call (more code, but ability to serialize
         //!< calls for multi-note systems)
#endif

// Component Facilities

// Set assertion form. Options:
//   1. FW_NO_ASSERT: assertions are compiled out, side effects are kept
//   2. FW_FILEID_ASSERT: asserts report a file CRC and line number
//   3. FW_FILENAME_ASSERT: asserts report a file path (__FILE__) and line number
//   4. FW_RELATIVE_PATH_ASSERT: asserts report a relative path within F' or F' library and line number
//
// Note: users who want alternate asserts should set assert level to FW_NO_ASSERT and define FW_ASSERT in this header
#ifndef FW_ASSERT_LEVEL
#define FW_ASSERT_LEVEL (FW_FILENAME_ASSERT)  //!< Defines the type of assert used
#endif

// Decide whether the framework should force assertions to always abort.
// If enabled, allows additional compiler optimizations and prevents code from running after an assertion trips.
// If disabled (default), allows the FATAL event handler to decide whether code should continue running after an
// assertion trips.
#ifndef FW_ASSERTIONS_ALWAYS_ABORT
#define FW_ASSERTIONS_ALWAYS_ABORT 0
#endif

// Adjust various configuration parameters in the architecture. Some of the above enables may disable some of the values

// The size of the object name stored in the object base class. Larger names will be truncated.
#if FW_OBJECT_NAMES
#ifndef FW_OBJ_NAME_BUFFER_SIZE
#define FW_OBJ_NAME_BUFFER_SIZE \
    (80)  //!< Size of object name (if object names enabled). AC Limits to 80, truncation occurs above 80.
#endif
#endif

// Normally when a command is deserialized, the handler checks to see if there are any leftover
// bytes in the buffer. If there are, it assumes that the command was corrupted somehow since
// the serialized size should match the serialized size of the argument list. In some cases,
// command buffers are padded so the data can be larger than the serialized size of the command.
// Setting the below to zero will disable the check at the cost of not detecting commands that
// are too large.
#ifndef FW_CMD_CHECK_RESIDUAL
#define FW_CMD_CHECK_RESIDUAL (1)  //!< Check for leftover command bytes
#endif

// Enables text logging of events as well as data logging. Adds a second logging port for text output.
// In order to set this to 0, FPRIME_ENABLE_TEXT_LOGGERS must be set to OFF.
#ifndef FW_ENABLE_TEXT_LOGGING
#define FW_ENABLE_TEXT_LOGGING (1)  //!< Indicates whether text logging is turned on
#endif

// Define if serializables have toString() method. Turning off will save code space and
// string constants. Must be enabled if text logging enabled
#ifndef FW_SERIALIZABLE_TO_STRING
#define FW_SERIALIZABLE_TO_STRING (1)  //!< Indicates if autocoded serializables have toString() methods
#endif

// Some settings to enable AMPCS compatibility. This breaks regular ISF GUI compatibility
#ifndef FW_AMPCS_COMPATIBLE
#define FW_AMPCS_COMPATIBLE (0)  //!< Whether or not JPL AMPCS ground system support is enabled.
#endif

// Enforce single-ownership semantics on Fw::Buffer.
//
// When enabled, Fw::Buffer becomes move-only -- its copy constructor and copy assignment operator are deleted, so the
// only way to hand a buffer on is std::move -- and its destructor asserts unless the buffer has been emptied first,
// either by being moved from or by an explicit Fw::Buffer::release(). Together these turn two silent buffer-ownership
// mistakes into build errors and assertions: keeping a second reference to a buffer that was handed off, and dropping
// a buffer without returning it to its manager.
//
// This is off by default, and turning it on today does not yield a working build. It is the switch for a migration
// that is not finished. What still has to change first:
//
//   1. `fpp-to-cpp` emits copies of Fw::Buffer that this repository cannot edit. Generated unit-test harnesses
//      (`*TesterBase.cpp`, `*GTestBase.cpp`) copy port arguments into history entries; generated serializable types
//      with an Fw.Buffer member (Svc::ComDataContextPair) copy in their constructors and assignment operators; and
//      generated component code constructs Fw::DpContainer from an lvalue Fw::Buffer.
//   2. Generated async port dispatch deserializes into a local Fw::Buffer, passes it to the handler by reference and
//      then destroys it. Unless the handler moves out of its argument, that local is destroyed still holding the
//      buffer and the destructor assertion trips, so buffers cannot cross an async port hop until the autocoder
//      releases the dispatch local after the handler returns.
//   3. Some generated handler signatures take `const Fw::Buffer&`, which cannot be forwarded to an output port
//      without a copy (see Svc::DpManager::productSendIn_handler).
//   4. Within F Prime itself, Fw::DpContainer and its users (Svc::DpWriter, Svc::DpCatalog, Svc::DpCompressProc,
//      Svc::FileManager) and Svc::ComRetry still copy buffers and need an ownership decision of their own.
//
// The rest of F Prime's non-generated flight code builds cleanly with this enabled. Fw::Buffer's own behavior under
// this setting is covered by Fw_Buffer_strict_ownership_ut_exe, which is always built and run.
#ifndef FW_BUFFER_STRICT_OWNERSHIP
#define FW_BUFFER_STRICT_OWNERSHIP (0)  //!< Make Fw::Buffer move-only and assert when a non-empty buffer is destroyed
#endif

// Posix thread names are limited to 16 characters, this can lead to collisions. In the event of a
// collision, set this to 0.
#ifndef POSIX_THREADS_ENABLE_NAMES
#define POSIX_THREADS_ENABLE_NAMES (1)  //!< Enable/Disable assigning names to threads
#endif

// Hint to the compiler to always inline LinearBufferBase serialization &
// deserialization methods
#define FW_SERIALIZE_FORCE_INLINE_LBB
// NOTE: To encourage inlining, uncomment below
// #if defined(__GNUC__) || defined(__clang__)
// #define FW_SERIALIZE_FORCE_INLINE_LBB __attribute__((always_inline)) inline
// #else
// #define FW_SERIALIZE_FORCE_INLINE_LBB
// #endif

// *** NOTE configuration checks are in Fw/Cfg/ConfigCheck.cpp in order to have
// the type definitions in Fw/Types/BasicTypes available.
#ifdef __cplusplus
}
#endif

#endif
