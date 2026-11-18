panda_add_test(panda_unit_base unit
    SOURCES Assert.cpp Error.cpp FunctionName.cpp HostInfo.cpp Logger.cpp StreamSink.cpp
    LIBRARIES panda_base panda_base_internal PandaTest::LogCapture PandaTest::MockHeaders)

if(PANDA_ENABLE_STACKTRACE)
    target_compile_definitions(panda_unit_base PRIVATE PANDA_TEST_STACKTRACE)
endif()
