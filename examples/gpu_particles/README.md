# GPU particles

This headless example passes one CPU scalar and one GPU storage buffer to a custom
fragment shader. The buffer stores each particle's position and velocity; velocity
selects a color between blue and orange. The uniform block contains the maximum
speed used by the color ramp. C++ creates and uploads one fixed-seed fixture.

The uniform vector also carries the target aspect ratio so the screen-space
sprites stay circular. The example writes one 10,000-particle image to
`gpu_particles.png` in the working directory.

Use `panda-bench --profile gpu-particles --particle-count 10000`, `100000` or
`1000000` to measure GPU frame time at the three workload sizes. The benchmark
uses the same custom shaders and records GPU timestamps and run metadata in its
JSON report.

## Windows

```powershell
cmake --preset msvc-fast
cmake --build --preset msvc-fast --target gpu_particles --parallel
.\build-msvc-fast\examples\gpu_particles\gpu_particles.exe
```

## Linux

```sh
cmake --preset gcc-fast
cmake --build --preset gcc-fast --target gpu_particles --parallel
./build-gcc-fast-Linux/examples/gpu_particles/gpu_particles
```
