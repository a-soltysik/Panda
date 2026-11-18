panda_add_test(panda_unit_common unit
    SOURCES Logger.cpp Assert.cpp FunctionName.cpp HostInfo.cpp StreamSink.cpp
    LIBRARIES panda_base panda_base_internal PandaTest::LogCapture PandaTest::MockHeaders)

if(PANDA_ENABLE_STACKTRACE)
    target_compile_definitions(panda_unit_common PRIVATE PANDA_TEST_STACKTRACE)
endif()
