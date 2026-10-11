# Buffer allocation example

`buffer_allocation` creates a headless Context and a 4096-byte device-local storage
buffer, reports its payload size and selected device, then closes through RAII.
The buffer's scope ends before the Context's scope. Requires
Vulkan 1.3 and the Khronos validation layer; no display or CUDA toolkit is needed.

The buffer initially contains undefined bytes. This allocation example does not upload data
or submit GPU work. Uploads and completion-based retention are subsequent resource
capabilities. `BufferMemory::Upload` and `Readback` select host-visible memory;
they do not expose a mapper or grant permission to access memory used by the GPU.

Windows:

```powershell
cmake --preset msvc-development
cmake --build --preset msvc-development --target buffer_allocation
./build-msvc-development/examples/buffer_allocation/buffer_allocation.exe
```

Linux:

```sh
cmake --preset gcc-development
cmake --build --preset gcc-development --target buffer_allocation
./build-gcc-development-Linux/examples/buffer_allocation/buffer_allocation
```
