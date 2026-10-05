# Learning from implementation

The contributor is responsible for helping the maintainer understand Panda well
enough to explain, debug and extend it. For each delivery, explain the purpose of
the change, how one important invariant works, why the chosen approach was used
and what happens in a representative failure case. Use the smallest complete
example or experiment that demonstrates these points; avoid a transcript of
implementation steps or a list of unexplained links.

Check the maintainer's understanding through a guided conversation, not an isolated
quiz. Start with the change's purpose and the main ownership or data flow. Walk one
normal path from beginning to end, then explain the important invariant where it
matters and trace a representative failure or recovery path. Pause at natural steps
so the maintainer can question or restate what is happening. After that context,
ask them to predict or explain the next relevant step, and say why that step matters.
Use their answer to find the next unclear connection and explain it before checking
again. A sudden question detached from the walkthrough, a one-way explanation or
silence does not establish understanding. Keep the conversation proportionate to
the change; it is part of the learning handoff, not a request to approve routine coding.

Before coding a new concept, identify the question and a relevant authoritative
source section. After coding, connect the answer to actual code and observed results.
A proposed experiment is not completed evidence. The contributor reports when
understanding has not yet been checked; only the maintainer can confirm that the
learning handoff is clear and accept the task.

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
Examples evolve with the project: when a task changes the available capabilities
or the normal way to use them, update a relevant runnable example or add one in
that task. An old example that still compiles but teaches an obsolete workflow is
not a completed learning handoff.

Primary references: [Vulkan Guide](https://docs.vulkan.org/guide/latest/),
[synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html),
[shader memory layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html),
[CUDA interoperability](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html).
Read the section answering the current question, not an entire specification for
every small change.
