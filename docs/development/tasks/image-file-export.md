# Image file export in Tools

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Let an application encode an `ImageReadback` into common image files such as PNG
and JPEG through the optional `Panda::Tools` package. Keep rendering and GPU
synchronization in Core; the Tools utility consumes already-read host pixels and
reports file/encoding errors. Define alpha handling for JPEG and expose quality
only where the selected format needs it. This task depends on
[Offscreen rendering and image readback](offscreen-rendering.md).

Image encoding does not trigger rendering or readback. Additional formats are not
implicit; add them only when a concrete consumer needs them.

Paths: `../../../include/tools/panda/`, `src/tools/`, tests, examples and directly
affected documentation. Select an encoder based on existing project dependencies
and licensing before implementation. This capability must not add image encoders
or filesystem policy to `Panda::Panda`.

## Acceptance

- Applications can write PNG and JPEG from a valid `ImageReadback` through
  `Panda::Tools`, without a window or additional GPU work.
- The API documents channel order, color-space handling, PNG alpha preservation,
  JPEG alpha compositing and JPEG quality behavior.
- Invalid image data and filesystem/encoder failures return actionable errors.
- CPU tests cover pixel conversion and error paths without requiring Vulkan hardware.

Learning: Explain the boundary between GPU readback and CPU image encoding, and
why PNG and JPEG require different alpha handling.
