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

Paths: `cmake/`, `../../../include/core/panda/rendering/`, `../../../src/core/vulkan/renderer/`, `../../../shaders/`,
`../../../tests/cases/unit/core/`, `../../../examples/` and directly affected documentation.
Specifications: [Shader integration](../../design/contracts/rendering.md#shader-integration),
[procedural instanced draws](../../design/contracts/rendering.md#procedural-instanced-draws),
[shader deployment](../../design/package-and-toolchain.md#shader-and-asset-deployment).

Establish build-time shader validation and layout rejection before adding live
replacement and cache behavior.

## Acceptance

- Verify SPIR-V/host offsets, wrong-layout and foreign-context rejection,
  missing shadow variant, and camera and shadow reload rollback.
- Declare descriptor set/binding numbers and vertex input locations in GLSL;
  build and validate their C++ layouts from reflection, removing their duplicate
  macros from `ShaderInterface.inc`. Retain shared definitions only for values
  that SPIR-V reflection cannot recover, such as light-type tags.
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
source-relative keys and returns `std::optional<ShaderResource>`. SPIRV-Reflect
is linked privately through its upstream static-library CMake option. A private
adapter checks the `main` stage and returns owning descriptor, block-member and
push-constant metadata; tests verify `std140` offsets. Image dimension/sample
type and capability checks remain. The adapter is not yet connected to program
or pipeline creation, so the GLSL single-source layout rule and removal of
duplicate binding macros remain unimplemented. Runtime programs, layout
rejection, resource bindings, shadow variants, pipeline warmup/cache and
transactional reload remain. Procedural instancing and its sphere-impostor
rendering and scale measurements also remain.

## Verification

Windows used CMake 4.4.2, MSVC toolset 14.51.36231, Vulkan SDK 1.4.341.1 and
clang-format 22.1.8. The full `msvc-quality` build, interface-header and format
targets, and `clang-cuda-analysis-windows` passed. CUDA analysis warned that the
installed CUDA version is newer than Clang's partially supported 12.9. The full
`msvc-fast` build and CTest suite passed: 731 tests, 726 passed and five were
skipped. The `docs-checks` preset passed.

The synchronized WSL mirror passed `gcc-quality`, the interface-header and
format targets, and `clang-cuda-analysis`. CUDA analysis reported the same
version warning. The full `gcc-fast` build and CTest suite passed: 731 tests,
723 passed and eight were skipped for local hardware or capability reasons.
Both C and C++ builds used ccache. The `docs-checks` preset validated 43 Mermaid
diagrams in 94 files, and `gcc-docs` passed. The project-maintenance checker
checked 90 Markdown files and the task queue with no structural errors.

The new tests check reflected descriptor and push-constant metadata, `std140`
member offsets, stage mismatch and invalid headers. They do not create a custom
Vulkan pipeline or verify custom shader rendering on a GPU. Reflected layouts
are not yet consumed by program creation, so GLSL is not yet the implemented
single source for descriptor bindings and vertex locations. Runtime programs,
layout rejection, resource bindings, shadow variants, pipeline warmup/cache,
transactional reload and procedural-instancing checks remain.
