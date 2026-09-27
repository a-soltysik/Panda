# Graphics code from drip

Source: [a-soltysik/drip](https://github.com/a-soltysik/drip/tree/1af09aed1288b0d40dd7d3339f1b970f846925fd).

The snapshot includes `src/gfx/`, `src/common/`, the `ExternalMemory` and
`CudaExternalMemory` adapters in `src/simulation/`, and their build/style context.
It is useful for C++ conventions, Vulkan resources, billboards and external memory.

The application, simulation solver and kernels are absent. Root build files retain
references to that application, so this is not a standalone build. Shader/config
headers are represented by their generation inputs. Follow the
[reference policy](../README.md).
