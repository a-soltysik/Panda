# Buffer allocation example

`buffer_allocation` creates a headless Context and a 4096-byte device-local storage
buffer, copies four integer values into an upload batch, submits the copy and
waits for its completion point before closing through RAII.
The buffer's scope ends before the Context's scope. Requires
Vulkan 1.3 and the Khronos validation layer; no display or CUDA toolkit is needed.

`BufferMemory::Upload` and `Readback` select host-visible memory; they do not
expose a mapper or grant permission to access memory used by the GPU.

Windows:

```powershell
cmake --preset msvc-development
cmake --build --preset msvc-development --target buffer_allocation
.\build-msvc-development\examples\buffer_allocation\buffer_allocation.exe
```

Linux WSL:

```powershell
.\scripts\build-wsl.ps1 -Preset gcc-development -Target buffer_allocation
wsl --distribution Ubuntu --exec bash -lc '~/src/Panda-wsl-build/build-gcc-development-Linux/examples/buffer_allocation/buffer_allocation'
```
