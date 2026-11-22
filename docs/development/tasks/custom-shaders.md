# Custom shaders and safe reload

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Build validated embedded GLSL, reflected application resource bindings, explicit shadow variants,
warmup/cache and transactional reload. Let applications submit procedural
instanced draws directly with a custom program, explicit vertex and instance
counts and GPU buffer bindings.

Non-goal: No runtime shader DSL, descriptor arrays or implicit migration.

Interface: shader build integration, programs, resource bindings, procedural
draws and explicit shadow variants.

Paths: `cmake/`, `../../../include/core/panda`, `src/shader/`, `shaders/`, `tests/shader/` and directly affected
documentation.
Specifications: [Shader integration](../../design/contracts/rendering.md#shader-integration),
[procedural instanced draws](../../design/contracts/rendering.md#procedural-instanced-draws),
[shader deployment](../../design/package-and-toolchain.md#shader-and-asset-deployment).

Establish build-time shader validation and layout rejection before adding live
replacement and cache behavior.

## Acceptance

- Verify SPIR-V/host offsets, wrong-layout and foreign-context rejection,
  missing shadow variant, and camera and shadow reload rollback.
- Provide a runnable graphics-only example with application-owned opaque
  sphere impostor shaders, a uniform buffer and instance storage buffers.
  Compare sphere depth with a mesh reference in the camera and all shadow
  view types, including the off-axis, near-plane and inside cases. Verify
  uniform scaling of the visualization and independence from the domain pose.
- Check 10,000, 100,000 and 1,000,000 instances. Each valid batch produces
  one draw per applicable pass and no per-instance CPU object or per-frame
  CPU copy of instance data.
  Verify zero-count behavior, changing bindings and retention through GPU
  completion.

Learning: Explain the difference between SPIR-V compilation and device pipeline creation.

## Current implementation

The CMake helper compiles application GLSL with `glslc`, validates each SPIR-V
output, and tracks GLSL includes. A Python standard-library script embeds the
validated words using a readable C++ header template. The generated lookup uses
source-relative keys and returns `std::optional<ShaderResource>`. Runtime programs,
reflected layout rejection, resource bindings, shadow variants, pipeline
warmup/cache and transactional reload remain to be implemented. Procedural
instancing and its sphere-impostor rendering and scale measurements also remain.

## Verification

Windows used CMake 4.4.2, MSVC toolset 14.51.36231, Python 3.14.6, Vulkan SDK
1.4.341.1 (`glslc` shaderc v2026.1 and `spirv-val` SPIRV-Tools v2026.1), and
clang-format 22.1.8.

- `cmake --preset msvc-fast`, `cmake --build --preset msvc-fast --parallel`, and
  `ctest --preset msvc-fast` passed; 729 tests registered, 724 passed and five
  were skipped.
- `cmake --preset msvc-quality` and
  `cmake --build --preset msvc-quality --parallel` passed.
- `cmake --build --preset msvc-quality --target all_verify_interface_header_sets
  panda_format_checks --parallel` passed.
- `cmake --preset clang-cuda-analysis-windows` and its build passed. Clang warned
  that the installed CUDA version is newer than its partially supported version
  12.9.
- `cmake --preset docs-checks` and its build passed; `check_docs.py` checked the
  Markdown files and task queue without structural errors.

The WSL mirror passed `gcc-quality`, the interface-header and formatting targets,
`clang-cuda-analysis`, `gcc-fast` and its full 729-test suite (721 passed, eight
hardware/capability cases skipped), `docs-checks`, and `gcc-docs`. CUDA analysis
reported the same CUDA-version warning.

The test checks generated SPIR-V bytes, stage and source-relative lookup, including
two files with the same basename. It does not create a custom Vulkan pipeline or
verify custom shader rendering on a GPU.
