# Panda

Panda is a C++23 graphics engine being developed around Vulkan, with planned CUDA
interoperability. The current implementation provides the foundation for applications:
shared logging and diagnostics, an optional GLFW window module, an optional CUDA
availability query, and examples. Rendering is not implemented yet.

## Features

- Static libraries with separate core, optional window tools and optional CUDA availability.
- Formatted logging with timestamps, source locations, and stream or file sinks.
- Diagnostics, optional standard stack traces, and recoverable window errors through `std::expected`.
- CMake/Ninja builds and GoogleTest tests on Windows and Linux.

## Requirements

- CMake 3.28 or newer and Ninja.
- A C++23 compiler and standard library supporting `std::format` and `std::expected`.
- clang-tidy, clang-format and cppcheck for the development presets.

On Windows, install Visual Studio or Build Tools with the **Desktop development with
C++** workload and a Windows SDK. MSVC builds work from ordinary PowerShell; Panda
loads the compiler environment automatically. GCC through MSYS2 UCRT64 is also supported.

On Ubuntu, install the window-system development packages:

```sh
sudo apt-get install xorg-dev libwayland-dev libxkbcommon-dev
```

CMake downloads GLFW, GLM and, when tests are enabled, GoogleTest through CPM.
An internet connection is needed for the first configuration.
The CUDA module needs the CUDA Toolkit; its example also needs NVCC and a
compatible host compiler. Ordinary builds with CUDA off do not discover either.

## Build and run

After cloning or downloading the repository, open a terminal in its root directory.
Follow the [empty-window example](examples/simple_scene/README.md) to configure,
build and run on Windows or Linux. The core library can be used without a window
or display.

For a CUDA build and a small application-owned kernel, follow the
[CUDA connection example](examples/cuda_connection/README.md). It can start and
report an unavailable driver or device without disabling Panda core use.

Development presets run static analysis and formatting checks. To build without
these tools, append the following options to the configure command:

```sh
-DPANDA_ENABLE_CLANG_TIDY=OFF -DPANDA_ENABLE_CPPCHECK=OFF -DPANDA_ENABLE_CLANG_FORMAT=OFF
```

Downloaded dependency sources are cached in `.cache/cpm`, outside build directories.
Deleting a build directory preserves the downloads. Set `CPM_SOURCE_CACHE` in your
environment or pass `-DCPM_SOURCE_CACHE=<path>` to choose another location.
Dependency configuration prints concise library names and retains warnings and errors;
add `--log-level=STATUS` to see the full configuration messages.

Stack traces are enabled by default for fatal diagnostics. Explicit traced logging
is available without adding traces to ordinary entries:

```cpp
panda::log::writeWithStacktrace(panda::log::Level::Error, "Operation failed: {}", reason);
```

The build checks `std::stacktrace` support and links `stdc++exp` when required by
libstdc++. If your standard library does not support it, configure with
`-DPANDA_ENABLE_STACKTRACE=OFF`. Traces show the current call stack; useful names
and source lines require debug symbols.

## Tests

The CPU test presets build core and Tools without requiring CUDA or analysis tools.
Enable `PANDA_BUILD_CUDA` in a test configuration to add CUDA availability unit
and integration cases; the CUDA connection example remains a separate program.
Use `msvc-tests` on Windows, `gcc-tests` on Linux or Windows/MSYS2, or `clang-tests`
on Linux. For example:

```sh
cmake --preset gcc-tests
cmake --build --preset gcc-tests
ctest --preset gcc-tests
```

These presets run unit and integration tests without opening a native window.
For runtime diagnostics, use `gcc-sanitizers` on Linux (ASan + UBSan) or
`msvc-sanitizers` on Windows (ASan) with the same command sequence.

## Use Panda in your application

Panda currently supports consumption directly from source through `add_subdirectory`
or CPM. Link the core target, and add the Tools target when your application needs a window:

```cmake
add_subdirectory(path/to/Panda)
target_link_libraries(my_application PRIVATE Panda::Panda)
```

To include the window module, enable `PANDA_BUILD_TOOLS` before adding Panda and link
`Panda::Tools`. To query CUDA availability, enable `PANDA_BUILD_CUDA` and link
`Panda::Cuda`. Dependencies are fetched automatically. Public targets propagate
their C++23 and include-directory requirements.

See [the documentation](docs/README.md) for API contracts and build configuration,
and [the example](examples/simple_scene/main.cpp) for window creation and event handling.
