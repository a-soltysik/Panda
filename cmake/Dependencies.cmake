if(PANDA_BUILD_ENGINE OR PANDA_BUILD_TOOLS OR PANDA_BUILD_TESTS OR PANDA_BUILD_BENCHMARKS)
    if(NOT COMMAND CPMAddPackage)
        include("${CMAKE_CURRENT_LIST_DIR}/CPM.cmake")
    endif()
endif ()

macro(panda_add_dependency name version)
    message(STATUS "CPM: Adding library ${name}@${version}")
    set(panda_dependency_log_level "${CMAKE_MESSAGE_LOG_LEVEL}")
    set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
    CPMAddPackage(NAME ${name} VERSION ${version} ${ARGN})
    set(CMAKE_MESSAGE_LOG_LEVEL "${panda_dependency_log_level}")
endmacro()

if (PANDA_BUILD_TOOLS)
    # GLFW 3.4 is only needed by the optional window implementation.
    panda_add_dependency(glfw 3.4
            GIT_TAG 3.4
            GITHUB_REPOSITORY glfw/glfw
            SYSTEM YES
            OPTIONS
            "GLFW_LIBRARY_TYPE STATIC"
            "GLFW_BUILD_EXAMPLES OFF"
            "GLFW_BUILD_TESTS OFF"
            "GLFW_BUILD_DOCS OFF"
            "GLFW_INSTALL OFF"
    )
    panda_enable_sanitizers(glfw)

    # stb_image_write 1.18 is private to image export in Panda::Tools.
    panda_add_dependency(stb 1.18
            GIT_TAG 9600522de53973336fa033a289858a4354cbb04c
            GIT_SHALLOW FALSE
            GITHUB_REPOSITORY nothings/stb
            DOWNLOAD_ONLY YES)
endif ()

if(PANDA_BUILD_ENGINE)
    # GLM 1.0.3
    panda_add_dependency(glm 1.0.3
            GIT_TAG 1.0.3
            GITHUB_REPOSITORY g-truc/glm
            SYSTEM YES
            OPTIONS
            "GLM_BUILD_TESTS OFF"
            "GLM_BUILD_INSTALL OFF"
            "GLM_ENABLE_FAST_MATH OFF"
            "GLM_ENABLE_LANG_EXTENSIONS OFF"
            "GLM_DISABLE_AUTO_DETECTION OFF"
    )
    panda_enable_sanitizers(glm::glm)
endif()

if (PANDA_BUILD_TESTS)
    # GoogleTest/gMock 1.18.0
    panda_add_dependency(googletest 1.18.0
            GIT_TAG v1.18.0
            GITHUB_REPOSITORY google/googletest
            SYSTEM YES
            OPTIONS
            "BUILD_GMOCK ON"
            "INSTALL_GTEST OFF"
            "gtest_build_tests OFF"
            "gmock_build_tests OFF"
            "gtest_force_shared_crt ON"
    )
    foreach(target IN ITEMS GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main)
        panda_enable_sanitizers(${target})
    endforeach()
endif ()

if (PANDA_BUILD_BENCHMARKS)
    # argparse is private to the optional benchmark executable.
    panda_add_dependency(argparse 3.2.0
            GIT_TAG v3.2
            GITHUB_REPOSITORY p-ranav/argparse
            SYSTEM YES
            OPTIONS
            "ARGPARSE_BUILD_TESTS OFF"
            "ARGPARSE_BUILD_SAMPLES OFF"
            "ARGPARSE_INSTALL OFF"
    )
    # JSON reports are private to the optional benchmark executable.
    panda_add_dependency(nlohmann_json 3.12.0
            GIT_TAG v3.12.0
            GITHUB_REPOSITORY nlohmann/json
            SYSTEM YES
            OPTIONS
            "JSON_BuildTests OFF"
            "JSON_Install OFF"
    )
endif ()
