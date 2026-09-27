# Package and toolchain contract

Accepted package specification. Toolchains are candidates until the required probes
pass; [the plan](../development/PLAN.md) records implementation progress.

## Source package first

Start with CPM and ordinary `add_subdirectory` consumption of static libraries.
Defer a prebuilt SDK, stable binary ABI, engine DLL/plugin distribution, and an
install/export package. Source consumption satisfies the initial integration goal
without adding a second distribution workflow. Static Panda libraries do not imply
static linking of every OS/driver/dependency library.

| Target | Public entry point | Build switch |
| --- | --- | --- |
| `Panda::Panda` | `panda/Panda.hpp` and focused core/scene/resource headers | Always built |
| `Panda::Import` | `panda/Import.hpp` | `PANDA_BUILD_IMPORT` |
| `Panda::Tools` | `panda/Tools.hpp` | `PANDA_BUILD_TOOLS` |
| `Panda::Cuda` | `panda/Cuda.hpp` | `PANDA_BUILD_CUDA` |
| `Panda::Physics` | `panda/Physics.hpp` | `PANDA_BUILD_PHYSICS` |

This table describes the intended package. Introduce each optional CMake switch
with its implementation; do not expose switches for unavailable modules. The
umbrella includes only core headers. Add-ons publicly depend on the core;
disabled add-ons expose no usable target and trigger no discovery of their exclusive
dependencies. Internal compilation boundaries need not become extra public targets.

`Panda::Tools` is a static module library containing Window and its GLFW session.
Production libraries follow module responsibilities, not unit-test boundaries.
Tests can supply mock object files for individual implementations while linking
the same module archives as applications; see [controlled dependencies](../development/testing.md#controlled-dependencies).

Keep module headers under `include/<module>/panda` and implementations under
`src/<module>`, plus `shaders/` and `cmake/`. Shared diagnostics live in
`include/common/panda` and `src/common`. The internal `panda_common` object library
has no public alias or separate archive. Core embeds its objects and propagates
its header requirements; add-ons use the same definitions through their public
core dependency. Do not embed these objects again in add-on archives.
Introduce examples, engine tests, and
benchmark directories when their contents exist, not as empty design scaffolding.
Never include `legacy/` in new targets. `.agents/` is maintenance infrastructure,
not part of the runtime package.

## Build configuration

Module selection switches use `PANDA_BUILD_<MODULE>`. When introduced, optional
module switches default to OFF. Keep build switches
for available features only. Planned defaults are:

- `PANDA_BUILD_EXAMPLES`, `PANDA_BUILD_TESTS`, `PANDA_BUILD_BENCHMARKS`, `PANDA_BUILD_DOCS`.
- `PANDA_ENABLE_SHADER_RELOAD`, `PANDA_ENABLE_UNITY`.

Warnings-as-errors, clang-tidy, cppcheck and clang-format checks are independently
selectable. They default to ON for top-level Panda development and OFF when Panda
is included by an application. The quality build enables all four. An example
requested with a disabled required module fails configuration clearly.

`PANDA_BUILD_TESTS=ON` enables every implemented Panda module, currently including
Tools, and builds all test levels. `PANDA_BUILD_TOOLS` defaults to OFF and controls
Tools when tests are disabled. The test override is scoped to the configuration;
it does not rewrite the cached module preference. CTest labels select execution
levels independently of which suites are built.

Use target-scoped language requirements, warnings, and definitions. Do not force
global `CMAKE_CXX_STANDARD`, `BUILD_SHARED_LIBS`, exception flags, dependency test
settings, or consumer warnings. MSAA, anisotropy, and benchmark durations remain
runtime settings, not CMake switches. No source copying or application-specific
changes to Panda are required for a consumer.

### Component declarations

`cmake/Targets.cmake` provides `panda_add_component`. Components default to
`STATIC`; `TYPE OBJECT` is reserved for embedded code such as common diagnostics.
These are the only supported component types.

```cmake
panda_add_component(panda_tools
    ALIAS Panda::Tools
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/tools"
    SOURCES gui/Window.cpp gui/GlfwSession.cpp
    LIBRARIES PUBLIC Panda::Panda PRIVATE glm::glm glfw)
```

`ALIAS` optionally declares a public target name. The required `INCLUDE_DIRECTORY`
exposes the stable public headers; relative paths are
resolved from the declaring source directory. `LIBRARIES` uses ordinary CMake
visibility groups.

The helper supplies C++23, the project's unity setting, module-scan configuration
and private quality checks for compiled components. Formatting discovers `.hpp`
files under the declaring source directory and public include directory; source
lists contain implementation files. Component-specific archive names, definitions
and exceptional settings remain ordinary target commands next to the declaration.
Tests and examples reuse `panda_configure_target` for the same quality setup;
their own helpers retain responsibility for test registration and header discovery.
When tests are enabled, component sources stay in separate translation units even
if unity is requested. Archive-member substitution needs that separation; unity
builds without tests still combine sources normally. Analysis continues to require
unity OFF.

Windows Ninja configuration prepares x64 MSVC before `project()` when `cl` is
selected, or when no compiler was selected. It reuses an existing developer
environment or finds the latest C++ desktop installation through `vswhere` and
calls `VsDevCmd.bat`. The build directory stores the selected include/library/tool
paths; compiler, linker and analyzer launchers restore them in later processes.
An ordinary PowerShell can therefore configure, build, or invoke Ninja directly.
The installed C++ workload, Windows SDK and quality tools remain prerequisites.
An explicit GCC or Clang selection bypasses this setup. Source consumers whose
parent project already selected its compiler keep the parent's setup. Recreate
the build directory when replacing its recorded toolchain/SDK installation.

## Dependency boundary

### Examples

`PANDA_BUILD_EXAMPLES` (OFF by default) adds the `simple_scene` executable and
requires Tools, enabled by `PANDA_BUILD_TOOLS=ON` or `PANDA_BUILD_TESTS=ON`.
The example creates a local Window with no
OpenGL context and owns a plain loop in `main`. The empty-window loop waits for
events instead of polling continuously. Closing the window ends the loop; RAII
releases the window on exit, including failure or exception unwinding. Creation
and event errors are logged and return a failing process exit status.

Core has no dependency on Tools or a display. GLFW is discovered and linked only
when Tools is enabled, so a core-only build needs no window-system packages.
Rendering and image encoding are not implemented; future offscreen applications
need not create a window. No common application-lifetime interface is imposed.

### Window wrapper

The implemented `Panda::Tools` target is selected by `PANDA_BUILD_TOOLS` (OFF by
default), and is always enabled when building tests. Its window API is in
[`panda/tools/gui/Window.hpp`](../../include/tools/panda/tools/gui/Window.hpp).
`Window::create` returns a move-only owner through `std::expected`; recoverable
errors own the operation, description and native GLFW status. Size/minimization
queries and event processing also return expected results. Ordinary absence is
not used to hide failures.

Creation, use, movement and destruction require the main thread and must not occur
inside callbacks. A moved-from object permits only destruction and `isValid()`.
Panda exclusively owns GLFW initialization/termination and its error callback
while windows are alive; the callback that preceded the session is restored at
the end. Multiple windows share that session, so releasing one never destroys
another. Do not independently manage GLFW or replace its error callback during
this lifetime. Window creation resets window hints and requests no OpenGL context.

Window sizes are current content dimensions in screen coordinates; minimization
uses framebuffer extent and iconification. A platform error remains distinct from
a zero-sized framebuffer. Negative dimensions from GLFW are returned as operation
errors rather than fatal diagnostics. `waitForInput` requires a live window and can block
indefinitely. Native window identities are borrowed and may be reused after
destruction. This wrapper does not create Vulkan surfaces or implement rendering.

### Accepted dependencies

| Role | Accepted choice | Exposure |
| --- | --- | --- |
| Vulkan binding | Vulkan-Hpp | Private implementation; native public API uses Vulkan C handles |
| Shader metadata | SPIRV-Reflect | Private shader-load validation |
| Windowing | GLFW | Private window/input implementation |
| Math | GLM | Public value types with a documented layout/configuration contract |
| ECS | EnTT | Registry/storage; opt-in `panda/Ecs.hpp` as defined by the [scene contract](contracts/scene.md#native-ecs-extension) |
| Scene parser | nlohmann/json | Private; schema and integration follow the [scene specification](contracts/scene.md#persistence-import-and-tools) |
| Image codec | `stb_image` | Private adapter with a restricted, tested feature set |
| Model import | Assimp | Only `Panda::Import`; selected static importers |
| GUI/gizmos | Dear ImGui and ImGuizmo | Only `Panda::Tools`; direct application ImGui panels remain possible |
| Physics | Panda code | No external physics engine |
| GPU allocation | Panda code | No VMA; limited allocation design in the resource contract |

EnTT storage does not imply adopting its reflection, scheduler, resource manager,
or event framework. A JSON parser does not imply automatic serialization of C++
objects. Support libraries do not implement lighting, shadows, AA, or simulation.
Do not carry Boost or other legacy dependencies forward automatically; prefer the
standard library for ordinary utilities without inventing replacement frameworks.

Dependency origins: [EnTT](https://github.com/skypjack/entt),
[nlohmann/json](https://github.com/nlohmann/json), [stb](https://github.com/nothings/stb),
[Assimp](https://github.com/assimp/assimp), [Dear ImGui](https://github.com/ocornut/imgui),
[ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo).

### Native graphics API without macro leakage

Keep Hpp types out of public signatures. A native compute callback receives a
borrowed `VkCommandBuffer`; the application may use Vulkan C or wrap the handle in
its own non-owning Hpp object. The native extension header is separate from the
core umbrella; its required SDK headers/link dependencies are supplied by the target.

Configure internal Hpp consistently in the private namespace `panda_vk`,
through `VULKAN_HPP_NAMESPACE`. This avoids sharing an application's differently
configured `vk` definitions or imposing Panda's exception/dispatcher macros on it.
Test coexistence; this is namespace isolation, not another renderer abstraction.
The namespace setting is supported by
[Vulkan-Hpp](https://github.com/KhronosGroup/Vulkan-Hpp/blob/main/vulkan/vulkan_hpp_macros.hpp).

GLM is consciously exposed. Use explicit projection conventions rather than forcing
global handedness/depth macros. The accepted
[math contract](contracts/scene.md#transforms-and-camera-conventions) defines packed
types and compatible configuration; implementation probes must check those layouts.
Do not silently accept ABI-changing consumer macros.

The source build pins GLFW `3.4` (when Tools is enabled), GLM `1.0.3`, and
GoogleTest/gMock `1.18.0` (when tests are enabled) through CPM. GLFW is static
independently of the parent's `BUILD_SHARED_LIBS`; GLM keeps its upstream default
library build enabled.
Dependency tests, examples, documentation and installation are disabled where
available. Native GLFW backends keep upstream defaults (Win32 on Windows, X11
and Wayland on Linux). GLM retains automatic detection, with fast math and
language extensions disabled; no Panda-wide SIMD or layout macros are imposed.
GLM usage requirements are public and GLFW's are private to Panda targets.
Preserve GLFW's zlib/libpng and GLM's MIT notices in the fetched source trees.

## Shader and asset deployment

Compile built-in GLSL to SPIR-V during the build and embed the bytes in the library.
The first cube needs no external shader directory or special working directory.

Provide one CMake helper, `panda_add_shaders(TARGET ... SOURCES ...
NAMESPACE ...)`, to compile application shaders and generate target-local embedded
resources. Track include dependencies and avoid basename collisions. The
[shader specification](contracts/rendering.md#build-loading-and-reload-boundary) defines
the generated C++ lookup interface.

Building GLSL needs `glslc`, normally supplied by the Vulkan SDK. Running a release
application needs neither the GLSL compiler nor the development SDK. Optional reload
explicitly supplies source/compiler paths and keeps the last valid program on failure.
Driver pipeline creation and caching remain runtime responsibilities.

Models/textures use an explicit application-selected asset root and portable relative
references. Panda does not silently copy arbitrary project assets or discover them
through process working-directory assumptions. Examples show a simple application-owned
asset deployment rule; no engine edits are needed.

## C++23 and CUDA20 with one host toolchain

Keep the engine-facing facade and native adapter separate:

- `Panda::Cuda` exposes C++23 APIs and converts native statuses into Panda errors.
- A private C++20 adapter uses plain structures, native handles, and status values.
  It does not include renderer/scene/ECS headers or `std::expected`.
- The adapter does not link a target that transitively requires `cxx_std_23`;
  linking it against the core would defeat this compilation boundary.
- Runtime API interop calls can live in ordinary host `.cpp` files. Panda does not
  need `.cu` files merely to import memory, manage a stream, or synchronize it.
- Only actual kernel targets enable the CUDA language/NVCC. An application's `.cu`
  target uses CUDA20 and the same supported host compiler family, through narrow
  application-owned declarations shared with its C++23 callback.

No owning STL containers, `std::function`, engine object graph, or exceptions cross
the native adapter boundary. This is a source-level boundary using a compatible host
compiler/runtime, not a universal C ABI or plugin interface.

Use `CUDA::cudart_static` behind the optional adapter, explicit initialization,
and no direct eager link to `CUDA::cuda_driver`. Do not initialize CUDA from global
constructors. Avoiding a shared cudart dependency **does not itself prove** startup
without an NVIDIA driver: the unavailable-driver probe must establish that behavior.
Do not silently introduce a plugin loader if the simple approach fails.

CUDA OFF skips toolkit discovery entirely. CUDA ON can use `FindCUDAToolkit` without
enabling the CUDA language, as documented by
[CMake](https://cmake.org/cmake/help/latest/module/FindCUDAToolkit.html).
Application kernel targets must use compatible toolkit/runtime linkage. Panda cannot
guarantee startup for arbitrary additional CUDA-dependent libraries linked by an app.

## Candidate toolchains

These are **probe candidates**, not minimum supported versions or successful builds.
Use one exact host toolchain per build and reuse it as NVCC's host compiler.

| Job | Initial candidate | Language modes |
| --- | --- | --- |
| Windows x64 | Windows 11; VS 2022 Build Tools, MSVC 19.38/STL; CUDA 13.4 when enabled | Engine C++23 feature set; native adapter and `.cu` C++20 |
| Linux x64 | Ubuntu 24.04; GCC 16/libstdc++ 14; CUDA 13.4 when enabled | Engine C++23; adapter/NVCC C++20 |
| Additional Linux analysis | Clang 22 with the same libstdc++ 14; CUDA OFF initially | Non-unity C++23 analysis/build |

CMake 3.28 or newer and Ninja are the initial build-tool candidates. A newer Vulkan
SDK may build a Vulkan 1.3-targeted engine; unchecked newer API use must not silently
raise the runtime requirement. Pin exact SDK/toolchain patches in foundation evidence,
not a moving "latest" CI download.

Before freezing a toolchain, check NVIDIA host-compiler support and standard-library
requirements against the exact toolkit release, then compile/link/run the probe.
[Linux support](https://docs.nvidia.com/cuda/cuda-installation-guide-linux/index.html#host-compiler-support-policy),
[Windows support](https://docs.nvidia.com/cuda/cuda-installation-guide-microsoft-windows/index.html#system-requirements).

Older MSVC toolsets may need `/std:c++latest` for C++23 facilities. Check the actual
CMake-generated flags/feature macros without propagating that mode to NVCC. Probe
`std::expected` and its used operations and the other library facilities Panda uses;
do not assume complete C++23 support or require modules/C++26 features.
[MSVC STL history](https://github.com/microsoft/STL/wiki/VS-2022-Changelog),
[libstdc++ status](https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html).

## Dependency pins and notices

When support libraries are first needed, check current upstream releases and
centralize their declarations in `cmake/Dependencies.cmake`. Apply the same version
review to CPM itself. Pin an explicit release tag and record the version; retain
the hash supplied by the upstream CPM bootstrap. Keep Vulkan
headers/Hpp compatible. Reuse a compatible parent-provided target; reject incompatible
versions/configurations clearly instead of creating another conflicting copy.

The task introducing a dependency records its version, license/notices, enabled
features and relevant probe results. Selecting a patch revision within an approved
dependency set is implementation maintenance, not permission to choose a different
library or major API.
Review transitive importer/codec dependencies too. Preserve notices; an old
reference pin is not evidence that a version is appropriate today.

No Panda license is selected by this contract. The maintainer chooses that before
public release, separately from preserving third-party notices.

## Package verification

Run each check in the task that introduces the relevant capability; foundation
work does not require an unimplemented renderer.

1. Build the core with add-ons OFF and CUDA absent: no CUDA discovery or legacy
   includes. Verify package use in a real application during integration.
2. Compile the used C++23 facilities and C++20 adapter with one host toolchain;
   compile/link a minimal NVCC CUDA20 producer across a plain-data boundary.
3. Inspect actual compiler commands, host identity, STL/runtime settings, and
   Debug/Release linkage; no unsupported-compiler bypass flags.
4. Start a CUDA-enabled sample without an available CUDA driver/device and keep
   rendering when CUDA is optional. Test both OS paths and process library dependencies;
   an explicitly required CUDA case fails clearly.
5. Build a consumer using its own Hpp configuration without macro/dispatcher collisions.
6. Render embedded built-in/application shaders from another working directory with
   the compiler absent at runtime; verify include-triggered shader rebuilding.
7. Check optional-module isolation, ordinary/unity builds, and target-scoped diagnostics.

These probes do not replace the later GPU interop proof. Failure reopens the affected
candidate before dependent expansion; it does not authorize silently lowering all
of Panda to C++20 or requiring a second host compiler.
