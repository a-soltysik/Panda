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
push-constant metadata. Mock-backed unit tests cover API failures and metadata
copying without linking the SPIRV-Reflect library; integration tests use compiled
GLSL and the real library to verify `std140` offsets. Built-in graphics programs
use reflected descriptor set and binding numbers, vertex input locations, and
specialization IDs. `ShaderInterface.inc` shares the light-type tags.
Frame descriptor layout creation, frame buffer updates, and material texture
bindings have separate owners. The renderer coordinates them through
`FrameBindings`.

Application programs, resource bindings, image dimension and sample type
checks, capability checks, explicit shadow variants, pipeline warmup and
transactional reload remain. Procedural instancing and its sphere impostor
rendering and scale measurements also remain.

## Verification

The Windows MSVC 14.51 and Vulkan SDK 1.4.341.1 `msvc-quality` build passed,
including clang-tidy and cppcheck. Public-header isolation, formatting and the
Clang CUDA-analysis preset passed. The full `msvc-fast` build and
`ctest --preset msvc-fast` run passed 754 tests; 5 were skipped because of
environment-specific output and memory-type requirements.

The Linux `gcc-quality` build, public-header isolation, formatting and Clang
CUDA-analysis preset passed. The full `gcc-fast` build and `ctest --preset gcc-fast`
run passed 754 tests; 8 were skipped because of CUDA and memory-type availability.
`gcc-docs` generated the API documentation. The Python maintenance checker found
no structural errors in 90 Markdown files, and `docs-checks` validated 43 diagrams.
The coverage preset was not run for this verification.

The tests do not visually verify application-supplied shaders on physical GPU
hardware.
