# Custom shader shadows

This headless example shows one custom-shader mesh casting a directional shadow
onto a floor using Panda's built-in Lit material. The custom program supplies
camera and shadow shader stages; the shadow fragment stage writes depth.
The rendered image is saved as `custom_shader_shadows.png` in the working
directory.

## Windows

```powershell
cmake --preset msvc-fast
cmake --build --preset msvc-fast --target custom_shader_shadows --parallel
.\build-msvc-fast\examples\custom_shader_shadows\custom_shader_shadows.exe
```

## Linux

```sh
cmake --preset gcc-fast
cmake --build --preset gcc-fast --target custom_shader_shadows --parallel
./build-gcc-fast-Linux/examples/custom_shader_shadows/custom_shader_shadows
```
