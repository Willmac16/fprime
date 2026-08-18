####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
####
# The IP transports need Berkeley sockets and the serial transport needs termios, so this
# module is restricted to the platforms that supply both.
restrict_platforms(Linux Darwin)

register_fprime_module(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/UnifiedByteStreamDriverTypes.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/UnifiedByteStreamDriver.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/SerialStream.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/UnifiedByteStreamDriver.cpp"
  HEADERS
    "${CMAKE_CURRENT_LIST_DIR}/SerialStream.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/UnifiedByteStreamDriver.hpp"
  DEPENDS
    Fw_Logger
    Drv_ByteStreamDriverModel
    Drv_Ip
    Svc_Interfaces
    Os
)

### UTs ###
register_fprime_ut(
  AUTOCODER_INPUTS
    "${CMAKE_CURRENT_LIST_DIR}/UnifiedByteStreamDriverTypes.fpp"
    "${CMAKE_CURRENT_LIST_DIR}/UnifiedByteStreamDriver.fpp"
  SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/UnifiedByteStreamDriverTestMain.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/test/ut/UnifiedByteStreamDriverTester.cpp"
  DEPENDS
    STest
    SocketTestHelper
  UT_AUTO_HELPERS
)
