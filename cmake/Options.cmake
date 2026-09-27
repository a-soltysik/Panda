if(PROJECT_IS_TOP_LEVEL AND NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_SOURCE_CACHE "${PROJECT_SOURCE_DIR}/.cache/cpm" CACHE PATH "Persistent CPM source cache")
endif()

option(PANDA_ENABLE_STACKTRACE "Include standard stack traces in requested diagnostics" ON)

option(PANDA_BUILD_TESTS "Build Panda tests" OFF)
option(PANDA_BUILD_EXAMPLES "Build Panda examples" OFF)
option(PANDA_ENABLE_UNITY "Use unity builds for Panda targets" OFF)
option(PANDA_WARNINGS_AS_ERRORS "Treat Panda target warnings as errors" ${PROJECT_IS_TOP_LEVEL})
option(PANDA_ENABLE_CLANG_TIDY "Analyze Panda targets with clang-tidy" ${PROJECT_IS_TOP_LEVEL})
option(PANDA_ENABLE_CPPCHECK "Analyze Panda targets with cppcheck" ${PROJECT_IS_TOP_LEVEL})
option(PANDA_ENABLE_CLANG_FORMAT "Check Panda target formatting with clang-format" ${PROJECT_IS_TOP_LEVEL})
option(PANDA_ENABLE_SANITIZER_ADDRESS "Detect invalid memory accesses with AddressSanitizer" OFF)
option(PANDA_ENABLE_SANITIZER_UNDEFINED_BEHAVIOR "Detect undefined behavior with UndefinedBehaviorSanitizer" OFF)
option(PANDA_ENABLE_SANITIZER_THREAD "Detect data races with ThreadSanitizer (Linux)" OFF)
option(PANDA_ENABLE_SANITIZER_MEMORY "Detect uninitialized reads with MemorySanitizer (instrumented Clang toolchain)" OFF)
option(PANDA_ENABLE_SANITIZER_LEAK "Detect memory leaks with standalone LeakSanitizer (Linux)" OFF)

option(PANDA_BUILD_TOOLS "Build Panda::Tools (always enabled with tests)" OFF)

if(PANDA_BUILD_TESTS)
    set(PANDA_BUILD_TOOLS ON)
endif()
