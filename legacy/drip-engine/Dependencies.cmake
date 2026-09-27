function(drip_setup_dependencies)

    CPMAddPackage(
            URI "gh:fmtlib/fmt#03cfb8645e2646fce738fe9d8e13fe7d7d947ad4"
            OPTIONS "FMT_UNICODE OFF")
    CPMAddPackage("gh:g-truc/glm#1.0.2")
    CPMAddPackage("gh:glfw/glfw#3.4")
    CPMAddPackage("gh:nlohmann/json@3.12.0")
    CPMAddPackage("gh:pboettch/json-schema-validator#2.4.0")
    CPMAddPackage("gh:doctest/doctest@2.4.12")
    CPMAddPackage(
            NAME Boost
            VERSION 1.90.0
            URL https://github.com/boostorg/boost/releases/download/boost-1.90.0/boost-1.90.0-cmake.tar.xz
            URL_HASH SHA256=aca59f889f0f32028ad88ba6764582b63c916ce5f77b31289ad19421a96c555f
            OPTIONS
            "BOOST_INCLUDE_LIBRARIES exception\\\;stacktrace\\\;thread"
    )

    set(IMGUI_BUILD_GLFW_BINDING ON)
    set(IMGUI_BUILD_VULKAN_BINDING ON)

    # CPMAddPackage downloads the imgui sources; ext/imgui-cmake wraps them into a CMake target
    CPMAddPackage("gh:ocornut/imgui@1.92.5")
    add_subdirectory(ext/imgui-cmake)

    find_package(Vulkan REQUIRED)

endfunction()
