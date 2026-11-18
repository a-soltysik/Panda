# Panda

[![Quality](https://github.com/a-soltysik/Panda/actions/workflows/quality.yml/badge.svg?branch=master)](https://github.com/a-soltysik/Panda/actions/workflows/quality.yml)
[![Coverage](https://codecov.io/gh/a-soltysik/Panda/branch/master/graph/badge.svg)](https://codecov.io/gh/a-soltysik/Panda)

Panda is a C++23 Vulkan engine for application-owned rendering. Core provides
mesh data, primitive generators, transforms and projection matrices; scene,
window and CUDA are optional modules.

## Libraries

| Target            | What it provides                                              | Selection                              |
|-------------------|---------------------------------------------------------------|----------------------------------------|
| `Panda::Panda`    | Vulkan engine, mesh data, shape generators and CPU math      | `PANDA_BUILD_ENGINE` (ON by default)   |
| `Panda::Scene`    | Scene objects and render-view preparation                     | `PANDA_BUILD_SCENE` (OFF by default)   |
| `Panda::Tools`    | GLFW windows and camera helpers                               | `PANDA_BUILD_TOOLS` (OFF by default)   |
| `Panda::Cuda`     | CUDA device matching, buffer import and GPU handoff           | `PANDA_BUILD_CUDA` (OFF by default)    |
| `Panda::All`      | Interface target for every production library enabled above   | Always available; no separate switch   |

`Panda::All` is convenient while prototyping. As the application narrows its needs,
link specific targets; add-ons bring their requirements transitively. The Core
target owns the shared mesh contract, primitive generators and CPU math.

The engine uses GLM and requires Vulkan 1.3 headers and loader
plus `glslc` and `spirv-val` for built-in shaders. GLFW is fetched only with Tools
enabled. CUDA is discovered only when its module is enabled. Common error and
diagnostic support is included through the targets that use it. CPM fetches GLM,
GLFW and GoogleTest as needed; first-time setup requires network access.

## Quick start

Full engine builds need CMake 3.28+, Ninja and a C++23 toolchain. On Windows, install
Visual Studio or Build Tools with the **Desktop development with C++** workload and
the Vulkan SDK. On Ubuntu, install the Vulkan, window-system and shader tools:

```sh
sudo apt-get install libvulkan-dev xorg-dev libwayland-dev libxkbcommon-dev glslc spirv-tools
```

Build the window example on Windows:

```powershell
cmake --preset msvc-development
cmake --build --preset msvc-development --target simple_scene --parallel
./build-msvc-development/examples/simple_scene/simple_scene.exe
```

Build it on Linux:

```sh
cmake --preset gcc-development
cmake --build --preset gcc-development --target simple_scene --parallel
./build-gcc-development-Linux/examples/simple_scene/simple_scene
```

The development presets enable Tools and Scene. `simple_scene` renders
application-provided mesh and camera data through a GLFW window. Other examples cover
headless allocation, offscreen rendering, error handling, scene rendering and CUDA
connection.

## Use Panda in an application

Add Panda from source and link all libraries selected by the configuration:

```cmake
add_subdirectory(path/to/Panda)
target_link_libraries(my_application PRIVATE Panda::All)
```

Or link only the targets the application uses:

```cmake
target_link_libraries(my_application PRIVATE Panda::Scene Panda::Tools)
```

## Tests and quality

`PANDA_BUILD_TESTS=ON` builds tests for selected targets; it does not enable the
engine or Tools. Full test presets explicitly select the modules they cover. Use
`msvc-tests` on Windows or `gcc-tests` on Linux for noninteractive tests, and
`msvc-fast` or `gcc-fast` for the full suite including system tests:

```sh
cmake --preset gcc-tests
cmake --build --preset gcc-tests --parallel
ctest --preset gcc-tests
```

The `*-quality` presets add static analysis, formatting and public-header checks.
The [development plan](docs/development/PLAN.md) and [documentation index](docs/README.md)
describe current work and API contracts. See the [examples](examples/) for runnable
programs and the [CUDA connection guide](examples/cuda_connection/README.md) for
application-owned kernels.
