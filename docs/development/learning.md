# Learning from implementation

The maintainer should be able to explain, debug and extend Panda. Each delivery
includes a short explanation of one important invariant, the chosen tradeoff and
one failure case. Use the smallest complete example or experiment that demonstrates
it; avoid a transcript of implementation steps or a list of unexplained links.

Before coding a new concept, identify the question and a relevant authoritative
source section. After coding, connect the answer to actual code and observed results.
A proposed experiment is not completed evidence; maintainer acceptance remains
separate from supplying an explanation.

| Area | Useful experiment or question |
| --- | --- |
| Build and toolchain | Which properties reach consumers? Why does CUDA avoid C++23 engine headers? |
| GPU lifetime | Release the last application handle while a frame still uses its resource. |
| Vulkan/CUDA | Trace every producer and wait for two frames writing one shared buffer. |
| Transforms | Reparent a rotated child under nonuniform scale and explain shear rejection. |
| Materials and shadows | Inspect color space, face orientation and shadow depth in a capture. |
| Shader ABI | Reject an incompatible block layout before publishing a program. |
| Persistence and tools | Reload scene edits and identify application state that must be reconstructed. |
| Physics | Demonstrate pose authority and a high-speed tunneling limitation. |
| Performance | Reproduce a baseline and explain one measured bottleneck. |

API comments belong at declarations, usage examples in compiled examples, and
rationale in the owning specification. Follow the [quality policy](quality.md#public-api-and-learning).

Primary references: [Vulkan Guide](https://docs.vulkan.org/guide/latest/),
[synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html),
[shader memory layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html),
[CUDA interoperability](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html).
Read the section answering the current question, not an entire specification for
every small change.
