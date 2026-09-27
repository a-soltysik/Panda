include(cmake/SystemLink.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)

macro(PD_supports_sanitizers)
    if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND NOT WIN32)
        set(SUPPORTS_UBSAN OFF)
    else()
        set(SUPPORTS_UBSAN OFF)
    endif()

    if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND WIN32)
        set(SUPPORTS_ASAN OFF)
    else()
        set(SUPPORTS_ASAN OFF)
    endif()
endmacro()

macro(PD_setup_options)
    option(PD_ENABLE_HARDENING "Enable hardening" OFF)
    cmake_dependent_option(
        PD_ENABLE_GLOBAL_HARDENING
        "Attempt to push hardening options to built dependencies"
        OFF
        PD_ENABLE_HARDENING
        OFF)

    pd_supports_sanitizers()

    if(NOT PROJECT_IS_TOP_LEVEL)
        option(PD_BUILD_APP "Build app" ON)
        option(PD_ENABLE_IPO "Enable IPO/LTO" OFF)
        option(PD_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
        option(PD_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
        option(PD_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
        option(PD_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
        option(PD_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
        option(PD_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
        option(PD_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
        option(PD_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
        option(PD_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
        option(PD_ENABLE_IWYU "Enable include-what-you-use analysis" OFF)
        option(PD_ENABLE_CACHE "Enable ccache" OFF)
        option(PD_ENABLE_PCH "Enable precompiled headers" OFF)
    else()
        option(PD_BUILD_APP "Build app" OFF)
        option(PD_ENABLE_IPO "Enable IPO/LTO" OFF)
        option(PD_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
        option(PD_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
        option(PD_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${SUPPORTS_ASAN})
        option(PD_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
        option(PD_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${SUPPORTS_UBSAN})
        option(PD_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
        option(PD_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
        option(PD_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
        option(PD_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
        option(PD_ENABLE_IWYU "Enable include-what-you-use analysis" OFF)
        option(PD_ENABLE_CACHE "Enable ccache" OFF)
        option(PD_ENABLE_PCH "Enable precompiled headers" OFF)
    endif()

    if(NOT PROJECT_IS_TOP_LEVEL)
        mark_as_advanced(
            PD_ENABLE_IPO
            PD_WARNINGS_AS_ERRORS
            PD_ENABLE_USER_LINKER
            PD_ENABLE_SANITIZER_ADDRESS
            PD_ENABLE_SANITIZER_LEAK
            PD_ENABLE_SANITIZER_UNDEFINED
            PD_ENABLE_SANITIZER_THREAD
            PD_ENABLE_SANITIZER_MEMORY
            PD_ENABLE_CLANG_TIDY
            PD_ENABLE_CPPCHECK
            PD_ENABLE_IWYU
            PD_ENABLE_CACHE
            PD_ENABLE_PCH)
    endif()
endmacro()

macro(PD_global_options)
    if(PD_ENABLE_IPO)
        include(cmake/InterproceduralOptimization.cmake)
        pd_enable_ipo()
    endif()

    pd_supports_sanitizers()

    if(PD_ENABLE_HARDENING AND PD_ENABLE_GLOBAL_HARDENING)
        include(cmake/Hardening.cmake)
        if(NOT SUPPORTS_UBSAN
           OR PD_ENABLE_SANITIZER_UNDEFINED
           OR PD_ENABLE_SANITIZER_ADDRESS
           OR PD_ENABLE_SANITIZER_THREAD
           OR PD_ENABLE_SANITIZER_LEAK)
            set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
        else()
            set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
        endif()
        message("${PD_ENABLE_HARDENING} ${ENABLE_UBSAN_MINIMAL_RUNTIME} ${PD_ENABLE_SANITIZER_UNDEFINED}")
        pd_enable_hardening(PD_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
    endif()
endmacro()

macro(PD_local_options)
    if(PROJECT_IS_TOP_LEVEL)
        include(cmake/StandardSettings.cmake)
    endif()

    add_library(PD_warnings INTERFACE)
    add_library(PD_options INTERFACE)

    include(cmake/CompilerWarnings.cmake)
    pd_set_project_warnings(
        PD_warnings
        ${PD_WARNINGS_AS_ERRORS}
        ""
        ""
        ""
        "")

    if(PD_ENABLE_USER_LINKER)
        include(cmake/Linker.cmake)
        pd_configure_linker(PD_options)
    endif()

    include(cmake/Sanitizers.cmake)
    pd_enable_sanitizers(
        PD_options
        ${PD_ENABLE_SANITIZER_ADDRESS}
        ${PD_ENABLE_SANITIZER_LEAK}
        ${PD_ENABLE_SANITIZER_UNDEFINED}
        ${PD_ENABLE_SANITIZER_THREAD}
        ${PD_ENABLE_SANITIZER_MEMORY})

    if(PD_ENABLE_CACHE)
        include(cmake/Cache.cmake)
        pd_enable_cache()
    endif()

    include(cmake/StaticAnalyzers.cmake)
    if(PD_ENABLE_CLANG_TIDY)
        pd_enable_clang_tidy(PD_options ${PD_WARNINGS_AS_ERRORS})
    endif()

    if(PD_ENABLE_CPPCHECK)
        pd_enable_cppcheck(${PD_WARNINGS_AS_ERRORS} "" # override cppcheck options
        )
    endif()
    if(PD_ENABLE_IWYU)
        pd_enable_include_what_you_use()
    endif()

    if(PD_WARNINGS_AS_ERRORS)
        check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
        if(LINKER_FATAL_WARNINGS)
            # This is not working consistently, so disabling for now
            # target_link_options(PD_options INTERFACE -Wl,--fatal-warnings)
        endif()
    endif()

    if(PD_ENABLE_HARDENING AND NOT PD_ENABLE_GLOBAL_HARDENING)
        include(cmake/Hardening.cmake)
        if(NOT SUPPORTS_UBSAN
           OR PD_ENABLE_SANITIZER_UNDEFINED
           OR PD_ENABLE_SANITIZER_ADDRESS
           OR PD_ENABLE_SANITIZER_THREAD
           OR PD_ENABLE_SANITIZER_LEAK)
            set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
        else()
            set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
        endif()
        pd_enable_hardening(PD_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
    endif()

endmacro()
