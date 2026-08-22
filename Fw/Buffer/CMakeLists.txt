####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####


register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/Buffer.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Buffer.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/Buffer.hpp"
  DEPENDS
    Fw_Types
)

register_fprime_ut(
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/TestBuffer.cpp"
)

# FW_BUFFER_STRICT_OWNERSHIP changes Fw::Buffer at compile time, so it gets its own executable. Buffer.cpp is
# compiled into this target rather than linked from Fw_Buffer so that the strict destructor is the one under test.
register_fprime_ut(
    Fw_Buffer_strict_ownership_ut_exe
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Buffer.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/TestBufferStrictOwnership.cpp"
  DEPENDS
    Fw_Types
)
if (TARGET Fw_Buffer_strict_ownership_ut_exe)
    target_compile_definitions(Fw_Buffer_strict_ownership_ut_exe PRIVATE FW_BUFFER_STRICT_OWNERSHIP=1)
endif()
