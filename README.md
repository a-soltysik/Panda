# Panda

[![Quality](https://github.com/a-soltysik/Panda/actions/workflows/quality.yml/badge.svg?branch=master)](https://github.com/a-soltysik/Panda/actions/workflows/quality.yml)
[![Coverage](https://codecov.io/gh/a-soltysik/Panda/branch/master/graph/badge.svg)](https://codecov.io/gh/a-soltysik/Panda)

Panda is a C++23 graphics engine being developed around Vulkan, with planned CUDA
interoperability. The current implementation provides the foundation for applications:
shared logging and diagnostics, an optional GLFW window module, an optional CUDA
availability query, a Vulkan clear-and-present path, and examples. Scene rendering
is not implemented yet.

## Features

- Static libraries with separate core, optional window tools and optional CUDA availability.
- Formatted logging with timestamps, source locations, and stream or file sinks.
- Diagnostics, optional standard stack traces, and recoverable window errors through `std::expected`.
- Vulkan 1.3 context creation, device capability checks, and windowed clear-and-present frames.
- CMake/Ninja builds and GoogleTest tests on Windows and Linux.

## Requirements

- CMake 3.28 or newer and Ninja.
- A C++23 compiler and standard library supporting `std::format` and `std::expected`.
- clang-tidy, clang-format and cppcheck for the quality presets.

On Windows, install Visual Studio or Build Tools with the **Desktop development with
C++** workload, a Windows SDK and a Vulkan SDK. MSVC builds work from ordinary PowerShell; Panda
loads the compiler environment automatically. GCC through MSYS2 UCRT64 is also supported.

On Ubuntu, install the window-system development packages:

```sh
sudo apt-get install libvulkan-dev xorg-dev libwayland-dev libxkbcommon-dev
```

CMake downloads GLFW, GLM and, when tests are enabled, GoogleTest through CPM.
An internet connection is needed for the first configuration.
The CUDA module needs the CUDA Toolkit; its example also needs NVCC and a
compatible host compiler. Ordinary builds with CUDA off do not discover either.

## Build and run

After cloning or downloading the repository, open a terminal in its root directory.
Follow the [Vulkan window example](examples/simple_scene/README.md) to configure,
build and run on Windows or Linux. The core library can be used without a window
or display.

For a CUDA build and a small application-owned kernel, follow the
[CUDA connection example](examples/cuda_connection/README.md). It can start and
report an unavailable driver or device without disabling Panda core use.

The `gcc-development` and `msvc-development` presets build the libraries and window
example with compiler warnings but without tests, static analyzers or formatting
checks. Use a `*-tests` preset when working on tests and a `*-quality` preset for
public-header verification and the full analysis build. CMake also labels declared targets in the IDE model under
`Panda/Libraries`, `Panda/Tests`, `Panda/Test support`, `Panda/Examples`,
`Panda/Documentation`.

Downloaded dependency sources are cached in `.cache/cpm`, outside build directories.
Deleting a build directory preserves the downloads. Set `CPM_SOURCE_CACHE` in your
environment or pass `-DCPM_SOURCE_CACHE=<path>` to choose another location.

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

These presets include native-window system tests when a display and Vulkan driver
are available; unsupported environments are reported as skips.
For runtime diagnostics, use `gcc-sanitizers` on Linux (ASan + UBSan) or
`msvc-sanitizers` on Windows (ASan) with the same command sequence.
The `gcc-coverage` preset instruments Linux CPU tests. The Tests workflow attaches
an HTML and XML report named `panda-coverage`; the badge above reflects that run.
No minimum percentage is required. Generated coverage files stay in the build
directory.

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
and [the example](examples/simple_scene/main.cpp) for window and Vulkan context lifetimes.
The Quality workflow also provides generated API HTML as the `panda-api-docs`
downloadable artifact. Locally, run `cmake --preset gcc-docs` and
`cmake --build --preset gcc-docs`, then open
`build-gcc-docs-Linux/docs/api/generated/html/index.html`.
