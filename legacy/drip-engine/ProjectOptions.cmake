include(cmake/SystemLink.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)

macro(drip_setup_options)
    option(DRIP_ENABLE_HARDENING "Enable hardening" OFF)
    cmake_dependent_option(
            DRIP_ENABLE_GLOBAL_HARDENING
            "Attempt to push hardening options to built dependencies"
            ON
            DRIP_ENABLE_HARDENING
            OFF)

    option(DRIP_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(DRIP_ENABLE_WARNINGS "Enable warnings" OFF)
    option(DRIP_ENABLE_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(DRIP_ENABLE_USER_LINKER "Enable user-selected linker" OFF)
    option(DRIP_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(DRIP_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(DRIP_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(DRIP_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(DRIP_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(DRIP_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(DRIP_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(DRIP_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(DRIP_ENABLE_CACHE "Enable ccache" OFF)
    option(DRIP_ENABLE_COMPILE_COMMANDS "Enable support for compile_commands.json" OFF)
    option(DRIP_ENABLE_FAST_MATH "Enable fast math compilation flags" OFF)
    option(DRIP_CUDA_ENABLE_CUSTOM_ARCHITECTURES "Enable choosing CUDA architectures" OFF)
    option(DRIP_CUDA_ENABLE_DEBUG "Enable CUDA debug symbols (-G)" OFF)
    option(DRIP_CUDA_ENABLE_LINEINFO "Enable CUDA line info" OFF)


    if (NOT PROJECT_IS_TOP_LEVEL)
        mark_as_advanced(
                DRIP_ENABLE_IPO
                DRIP_ENABLE_WARNINGS
                DRIP_ENABLE_WARNINGS_AS_ERRORS
                DRIP_ENABLE_USER_LINKER
                DRIP_ENABLE_SANITIZER_ADDRESS
                DRIP_ENABLE_SANITIZER_LEAK
                DRIP_ENABLE_SANITIZER_UNDEFINED
                DRIP_ENABLE_SANITIZER_THREAD
                DRIP_ENABLE_SANITIZER_MEMORY
                DRIP_ENABLE_UNITY_BUILD
                DRIP_ENABLE_CLANG_TIDY
                DRIP_ENABLE_CPPCHECK
                DRIP_ENABLE_CACHE
                DRIP_ENABLE_COMPILE_COMMANDS
                DRIP_ENABLE_FAST_MATH
                DRIP_CUDA_ENABLE_CUSTOM_ARCHITECTURES
                DRIP_CUDA_ENABLE_DEBUG
                DRIP_CUDA_ENABLE_LINEINFO)
    endif ()

endmacro()

macro(drip_global_options)
    if (DRIP_ENABLE_IPO)
        include(cmake/InterproceduralOptimization.cmake)
        drip_enable_ipo()
    endif ()

    if (DRIP_ENABLE_HARDENING AND DRIP_ENABLE_GLOBAL_HARDENING)
        include(cmake/Hardening.cmake)
        if (DRIP_ENABLE_SANITIZER_UNDEFINED
                OR DRIP_ENABLE_SANITIZER_ADDRESS
                OR DRIP_ENABLE_SANITIZER_THREAD
                OR DRIP_ENABLE_SANITIZER_LEAK)
            set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
        else ()
            set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
        endif ()
        drip_enable_hardening(drip_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
    endif ()
endmacro()

macro(drip_local_options)
    if (PROJECT_IS_TOP_LEVEL)
        include(cmake/StandardProjectSettings.cmake)
    endif ()

    if (DRIP_ENABLE_COMPILE_COMMANDS)
        include(cmake/CompileCommands.cmake)
        drip_enable_compile_commands()
    endif ()

    add_library(drip_warnings INTERFACE)
    add_library(drip_options INTERFACE)

    if (DRIP_ENABLE_WARNINGS)
        include(cmake/CompilerWarnings.cmake)
        drip_set_project_warnings(
                drip_warnings
                ${DRIP_ENABLE_WARNINGS_AS_ERRORS})
    endif ()

    if (DRIP_ENABLE_USER_LINKER)
        include(cmake/Linker.cmake)
        drip_configure_linker(drip_options)
    endif ()

    include(cmake/Sanitizers.cmake)
    drip_enable_sanitizers(
            drip_options
            ${DRIP_ENABLE_SANITIZER_ADDRESS}
            ${DRIP_ENABLE_SANITIZER_LEAK}
            ${DRIP_ENABLE_SANITIZER_UNDEFINED}
            ${DRIP_ENABLE_SANITIZER_THREAD}
            ${DRIP_ENABLE_SANITIZER_MEMORY})

    set_target_properties(drip_options PROPERTIES UNITY_BUILD ${DRIP_ENABLE_UNITY_BUILD})

    if (DRIP_ENABLE_CACHE)
        include(cmake/Cache.cmake)
        drip_enable_cache()
    endif ()

    include(cmake/StaticAnalyzers.cmake)
    if (DRIP_ENABLE_CLANG_TIDY)
        drip_enable_clang_tidy(drip_options ${DRIP_ENABLE_WARNINGS_AS_ERRORS})
    endif ()

    if (DRIP_ENABLE_CPPCHECK)
        drip_enable_cppcheck(${DRIP_ENABLE_WARNINGS_AS_ERRORS})
    endif ()

    if (DRIP_ENABLE_FAST_MATH)
        include(cmake/Optimizations.cmake)
        drip_enable_fast_math(drip_options)
    endif ()

    if (DRIP_ENABLE_HARDENING AND NOT DRIP_ENABLE_GLOBAL_HARDENING)
        include(cmake/Hardening.cmake)
        if (NOT SUPPORTS_UBSAN
                OR DRIP_ENABLE_SANITIZER_UNDEFINED
                OR DRIP_ENABLE_SANITIZER_ADDRESS
                OR DRIP_ENABLE_SANITIZER_THREAD
                OR DRIP_ENABLE_SANITIZER_LEAK)
            set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
        else ()
            set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
        endif ()
        drip_enable_hardening(drip_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
    endif ()

endmacro()
