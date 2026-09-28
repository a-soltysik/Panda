# Tests and API documentation

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add ordinary/non-unity and unity build jobs, extend CTest for new engine behavior,
compile public headers in isolation, and configure useful Doxygen warnings.
Report CPU test coverage without a pass threshold: show its percentage in the
README badge and make the annotated report downloadable from CI.
Extend the existing GoogleTest/gMock, CTest and quality setup as behavior becomes available.
The [test organization](../testing.md) defines unit, integration and system boundaries.

Non-goal: No engine feature or synthetic GPU pass claim.

Interface: CMake development presets, expanded CTest coverage, API-doc configuration
and a Linux GCC coverage report.

Paths: `tests/`, `include/`, `.github/workflows/`, `cmake/`, `CMakeLists.txt`,
`CMakePresets.json`, `codecov.yml`, `docs/api/`, `docs/development/` and `README.md`.
Specifications: [Quality](../quality.md), [learning](../learning.md).

## Acceptance

Run ordinary and unity builds, header-isolation checks and documentation generation.
Demonstrate that missing public API documentation fails the documentation job.
If analyzer wiring changes, verify its failure path through the quality build.
Run instrumented CPU tests and inspect generated production-source coverage. CI
uploads HTML and Cobertura XML as an artifact and sends the XML to Codecov for the
badge; no minimum percentage gates the build.
Record actual local/CI results and unavailable environments.

Learning: Explain which checks catch C++ errors and which require a real GPU.

## Current evidence

Local Windows MSVC 19.51 and WSL Ubuntu 26.04 GCC 16 ordinary builds passed;
both ran 38 noninteractive CTest cases. The quality configuration's CMake
`all_verify_interface_header_sets` target compiled each current public header
separately. Linux/GCC and Windows/MSVC unity builds passed for the production
libraries and window example. The GCC quality build passed its configured
warnings, clang-tidy, cppcheck and formatting checks.

WSL Doxygen 1.15 generated public API HTML with warnings treated as errors.
Temporarily removing the `version()` API comment failed the documentation build;
restoring it passed. `panda::detail` and `panda::log::detail` are excluded from
the generated API.

The Linux GCC coverage configuration passed 38 noninteractive cases. gcovr 7.2
reported 88.9% production-source lines (367/413), 94.9% functions (56/59), and
62.1% branches (261/420). This is a local CPU measurement, with native-window
system scenarios excluded. Hosted CI artifact upload and Codecov badge display
remain to be observed on a workflow run. No GPU behavior was exercised by these
checks.

The Windows `msvc-development` and WSL `gcc-development` presets reconfigured and
built with tests, CUDA, static analyzers, formatting checks and header verification
off. Each build graph has six underlying libraries or executables: GLFW, GLM,
Panda common, core and tools, and `simple_scene`. The `msvc-tests` preset still
builds and passes all 38 CPU cases; the GCC quality preset still builds its
independent public-header verification target.

The Windows Ninja CMake File API codemodel lists Panda's buildable test-profile
targets under `Panda/Libraries`, `Panda/Tests` and `Panda/Test support`.
The maintainer observed improved grouping in CLion.
`panda_cuda_adapter` is in `Panda/Libraries` in the CUDA-enabled MSVC model.
The active MinGW development profile retains public-header verification; its
CMake-generated verification targets remain at the top level of the IDE model.
