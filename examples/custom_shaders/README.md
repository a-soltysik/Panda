# Custom shaders on one mesh

This headless example renders two draws using the same cube mesh. The left
cube's `Material::shaderProgram` selects the application-owned gyroid shaders.
The right cube uses Panda's built-in Lit material. The example samples its
animation at 0 and 1 seconds and writes `custom_shaders.png` and
`custom_shaders_next.png` to the working directory.

For a `Scene` entity, set the same field on its `MeshInstance::material`.
`Scene` copies the material into that entity's mesh draw when it builds a view.

`panda_add_shaders` compiles and validates the GLSL at build time. The resulting
SPIR-V is embedded in the example. The vertex stage reads the renderer's
camera transform and draw data. Its `vertexPosition` input declares location 4
in GLSL, and Panda reflects that location when building the mesh pipeline.
The fragment stage draws a bright contour where a 3D gyroid field approaches
zero. The field uses local position, so the pattern flows across cube faces
without texture coordinates. `fwidth` softens the contour based on its screen
coverage. The shader writes linear HDR color for Panda's tone mapping.
The development presets also build this target with CUDA disabled.

Before each render, C++ writes elapsed time into `GyroidParameters::elapsedSeconds`
and replaces the draw's `ShaderResources` snapshot. The binding name `params`
matches the GLSL uniform instance. Panda obtains its descriptor set and binding
from SPIR-V. The fragment shader reads `params.elapsedSeconds` to rotate the local
coordinates of the gyroid. Both frames use the same mesh and shader program.

The C++ structure includes padding for the GLSL `std140` block. Other bindings
can hold a `ShaderBufferRange` with a same-Engine GPU buffer and byte range, for
example an array of velocities produced by a simulation. Order the simulation
writes before shader reads.

## Windows

```powershell
cmake --preset msvc-fast
cmake --build --preset msvc-fast --target custom_shaders --parallel
.\build-msvc-fast\examples\custom_shaders\custom_shaders.exe
```

## Linux

```sh
cmake --preset gcc-fast
cmake --build --preset gcc-fast --target custom_shaders --parallel
./build-gcc-fast-Linux/examples/custom_shaders/custom_shaders
```

See the [program creation flow](../../docs/use-cases/create-shader-program.md)
and the [shader contract](../../docs/design/contracts/rendering.md#shader-integration).
