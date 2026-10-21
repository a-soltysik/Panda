# Options are loaded before project() so documentation checks can skip compilers.
set(panda_is_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(panda_is_top_level ON)
endif()

if(panda_is_top_level AND NOT DEFINED CPM_SOURCE_CACHE AND NOT DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_SOURCE_CACHE "${CMAKE_CURRENT_SOURCE_DIR}/.cache/cpm" CACHE PATH "Persistent CPM source cache")
endif()

option(PANDA_DOCS_ONLY "Configure documentation checks without engine dependencies" OFF)
option(PANDA_ENABLE_STACKTRACE "Include standard stack traces in requested diagnostics" ON)

option(PANDA_BUILD_TESTS "Build Panda tests" OFF)
option(PANDA_BUILD_EXAMPLES "Build Panda examples" OFF)
option(PANDA_BUILD_BENCHMARKS "Build Panda benchmark tools" OFF)
option(PANDA_BUILD_DOCS "Build Panda public API documentation" OFF)
option(PANDA_BUILD_DOC_CHECKS "Add functional documentation syntax checks" OFF)
option(PANDA_ENABLE_UNITY "Use unity builds for Panda targets" OFF)
option(PANDA_ENABLE_COVERAGE "Instrument Panda targets for GCC coverage reporting" OFF)
option(PANDA_VERIFY_PUBLIC_HEADERS "Add CMake targets that compile public headers independently" OFF)
option(PANDA_WARNINGS_AS_ERRORS "Treat Panda target warnings as errors" ${panda_is_top_level})
option(PANDA_ENABLE_CLANG_TIDY "Analyze Panda targets with clang-tidy" ${panda_is_top_level})
option(PANDA_ENABLE_CPPCHECK "Analyze Panda targets with cppcheck" ${panda_is_top_level})
option(PANDA_ENABLE_CLANG_FORMAT "Check Panda target formatting with clang-format" ${panda_is_top_level})
option(PANDA_EXPERIMENT_MSVC_SYNTAX_ONLY
    "EXPERIMENT: run MSVC analysis objects with syntax checking only; use the dedicated CI script" OFF)
option(PANDA_ENABLE_SANITIZER_ADDRESS "Detect invalid memory accesses with AddressSanitizer" OFF)
option(PANDA_ENABLE_SANITIZER_UNDEFINED_BEHAVIOR "Detect undefined behavior with UndefinedBehaviorSanitizer" OFF)
option(PANDA_ENABLE_SANITIZER_THREAD "Detect data races with ThreadSanitizer (Linux)" OFF)
option(PANDA_ENABLE_SANITIZER_MEMORY "Detect uninitialized reads with MemorySanitizer (instrumented Clang toolchain)" OFF)
option(PANDA_ENABLE_SANITIZER_LEAK "Detect memory leaks with standalone LeakSanitizer (Linux)" OFF)

option(PANDA_BUILD_TOOLS "Build Panda::Tools (always enabled with tests)" OFF)
option(PANDA_BUILD_CUDA "Build Panda::Cuda" OFF)

if(PANDA_BUILD_TESTS)
    set(PANDA_BUILD_TOOLS ON)
endif()
