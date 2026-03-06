####
# F prime CMakeLists.txt:
#
# SOURCE_FILES: combined list of source and autocoding files
# MOD_DEPS: (optional) module dependencies
#
# Note: using PROJECT_NAME as EXECUTABLE_NAME
####
set(SOURCE_FILES
  "${CMAKE_CURRENT_LIST_DIR}/PassiveConsoleTextLogger.fpp"
  "${CMAKE_CURRENT_LIST_DIR}/ConsoleTextLoggerImplCommon.cpp"
)
set(MOD_DEPS
  Fw/Logger
)

register_fprime_module()
### UTs ###
set(UT_SOURCE_FILES
  "${FPRIME_FRAMEWORK_PATH}/Svc/PassiveConsoleTextLogger/PassiveConsoleTextLogger.fpp"
  "${CMAKE_CURRENT_LIST_DIR}/test/ut/PassiveTextLoggerTester.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/test/ut/PassiveTextLoggerTestMain.cpp"
)
set(UT_MOD_DEPS
  Fw/Logger
)
register_fprime_ut()
