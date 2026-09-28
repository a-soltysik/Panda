add_library(panda_test_support INTERFACE)
add_library(PandaTest::Support ALIAS panda_test_support)
target_link_libraries(panda_test_support INTERFACE Panda::Panda GTest::gmock)
target_include_directories(panda_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

add_subdirectory(mocks)

add_library(panda_test_log_capture STATIC LogCapture.cpp)
add_library(PandaTest::LogCapture ALIAS panda_test_log_capture)
target_link_libraries(panda_test_log_capture PUBLIC PandaTest::Support PRIVATE PandaTest::MockHeaders)
panda_configure_test(panda_test_log_capture)
