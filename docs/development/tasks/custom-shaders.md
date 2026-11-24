# Custom shaders and safe reload

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Build validated embedded GLSL, application resources addressed by reflected GLSL instance names, explicit shadow variants,
pre-record pipeline caching and transactional reload. Let applications submit procedural
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
- Keep the `gpu_particles` example focused on passing one CPU scalar and one
  instance storage buffer to a custom fragment shader; color each particle from
  its GPU velocity. The uniform also carries the target aspect ratio for circular
  screen-space sprites. The example renders a fixed 10,000-particle image and does
  not collect timing samples.
- Demonstrate a custom mesh shader casting a shadow in a separate
  `custom_shader_shadows` example, with a built-in Lit surface receiving it.
- Measure 10,000, 100,000 and 1,000,000 particle instances in `panda-bench`.
  Each valid batch produces
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
specialization IDs. Graphics reflection rejects storage buffers unless each
stage that uses them declares the whole binding read-only. It accepts the
baseline graphics SPIR-V capabilities and rejects capabilities outside that set.
Shared sampled-image bindings must agree on image dimension, scalar sample type,
depth, arrayness and multisampling across stages.
`ShaderInterface.inc` shares the light-type tags.
Frame descriptor layout creation, frame buffer updates, and material texture
bindings have separate owners. The renderer coordinates them through
`FrameBindings`.

`ResourceManager::createShaderProgram` validates and owns the camera stages
and any explicit shadow stages. It checks shared application bindings by
reflected slot, verifies their block and texture layouts, and takes the renderer
bindings from built-in shader reflection. `Material::shaderProgram` selects a
same-Engine program for an individual standard mesh draw. The camera pipeline
uses the renderer descriptor layout and the program's reflected vertex inputs.
The [custom shader example](../../../examples/custom_shaders/README.md) renders
a cube with a procedural gyroid pattern alongside a built-in Lit cube in one
offscreen view. Two frames update `params.elapsedSeconds` in a named uniform block that
the fragment shader reads to rotate the pattern. A `ShaderResources` snapshot
can supply multiple CPU uniform blocks and read-only GPU buffer ranges. Panda
resolves names and builds the layout from the reflected descriptor numbers.
`RenderView::proceduralDraws` also supports instanced draws with
procedural vertex input, explicit vertex and instance counts, standard draw
constants, and the same named uniform/storage bindings. The offscreen system
test generates two colored triangles from vertex and instance IDs, reads their
colors from an application storage buffer, and checks the result after releasing
the view. Zero counts skip descriptor preparation and the draw command. Explicit
shadow variants now work for standard meshes and procedural draws. A shadow
variant may use a subset of camera application bindings; the frame descriptor
layout merges their reflected shader-stage visibility. GPU tests check both
paths with the Khronos validation layer. Application buffer owners stay in the
frame slot through completion.

Named uniform blocks, read-only storage ranges and sampled 2D/cube textures can
share one resource snapshot. Sampled textures must be uploaded same-Engine
float images; arrays, depth images and multisampled images are unsupported.
Their sampler uses the Engine filtering preset and the texture's address mode
and mip count. `ResourceManager::reloadShaderProgram` publishes only a validated
revision with the same camera and shadow ABI. The renderer prepares each needed
pipeline before recording the next frame, and a pipeline creation error is
returned from render without rolling back the published revision. There is no
separate public warmup API.
`examples/gpu_particles` draws screen-space circles with a custom fragment shader.
Each packed instance record contains position and velocity; one uniform scalar
sets the maximum speed for the color ramp. The same uniform vector carries the
target aspect ratio so sprites remain circular. The example writes one
10,000-instance image to `gpu_particles.png` and contains no timing loop. `panda-bench` reuses its
two shader files for GPU measurements at 10,000, 100,000 and 1,000,000 instances.
`examples/custom_shader_shadows` separately shows a custom-shader mesh casting a
shadow onto a built-in Lit floor.

## Verification

- Windows with MSVC 14.51 built `msvc-quality` and `msvc-fast`; its full CTest
  run had 795 cases, 790 passed and 5 skipped for output-device and noncoherent
  memory availability. Ubuntu under WSL with GCC 16.0.1 built `gcc-quality` and
  `gcc-fast`; its 795-case CTest run had 787 passed and 8 skipped for CUDA and
  memory-type availability.
- Both quality builds passed public-header isolation and formatting checks.
  Windows and Linux CUDA host/device analysis passed; Clang reported that the
  installed CUDA version is newer than its latest partially supported version,
  12.9. Ccache was enabled on both platforms.
- Windows ran `gpu_particles` and `custom_shader_shadows` on Vulkan. Their
  output images were inspected: particles are aspect-corrected circles, and the
  shadow is visible beside the custom mesh without floor self-shadowing. A
  Release `panda-bench` run used immediate presentation, 1280x720, one second of
  warmup and three seconds of measurement for each particle count. All three
  runs completed and exited cleanly after teardown on an NVIDIA GeForce RTX
  4070 Ti with MSVC 19.51:

  | Instances | Frame samples | CPU p50 / p95 (µs) | GPU p50 / p95 (µs) |
  |----------:|--------------:|-------------------:|-------------------:|
  | 10,000    |        13,843 | 105.6 / 821.2      | 56.224 / 72.992    |
  | 100,000   |        10,547 | 118.8 / 1,301.7    | 115.872 / 907.456  |
  | 1,000,000 |         2,364 | 861.4 / 3,486.9    | 863.488 / 3,436.992 |
- The offscreen system test checked one uniform and two storage buffers across
  sets, missing bindings, readback after dropping the draw, a foreign-Engine
  program and a missing shadow variant. Earlier `custom_shaders` validation-layer
  runs produced two PNGs on both platforms.
- The CLion development profile regenerated `ShaderResources.hpp` with the
  existing output open after the generator learned to publish a replacement.
- The Python maintenance checker found no structural errors in 92 Markdown
  files and the task queue. `docs-checks` validated 45 Mermaid diagrams in 99
  files, and `gcc-docs` generated the public API documentation. The coverage
  preset remains unverified for these changes.
