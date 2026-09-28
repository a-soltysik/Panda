panda_add_test(panda_unit_common unit
    SOURCES Logger.cpp Assert.cpp FunctionName.cpp StreamSink.cpp
    LIBRARIES Panda::Panda PandaTest::LogCapture PandaTest::MockHeaders)
target_include_directories(panda_unit_common PRIVATE "${PROJECT_SOURCE_DIR}/src/common")

if(PANDA_ENABLE_STACKTRACE)
    target_compile_definitions(panda_unit_common PRIVATE PANDA_TEST_STACKTRACE)
endif()
