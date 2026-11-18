# Image file export in Tools

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Let an application write the host bytes of a completed `ImageReadback` as raw RGBA,
PNG, BMP or JPEG through optional `Panda::Tools`. Keep rendering and GPU
synchronization in Core; file output consumes already-read host pixels. PNG
preserves alpha. BMP uses uncompressed 24-bit RGB, and BMP/JPEG composite alpha
over black in linear color. Only JPEG exposes quality. This task depends on
[Offscreen rendering and image readback](offscreen-rendering.md).

File output does not trigger rendering or readback. Raw RGBA contains no header or
extent metadata. Additional encodings are not implicit; add them only when a
concrete consumer needs them.

Paths: `../../../include/tools/panda/`, `src/tools/`, tests, examples and directly
affected documentation. Select an encoder based on existing project dependencies
and licensing before implementation. This capability must not add image encoders
or filesystem policy to `Panda::Panda`.

## Acceptance

- Applications can write raw RGBA, PNG, BMP and JPEG from a valid `ImageReadback`
  through `Panda::Tools`, without a window or additional GPU work.
- The API documents the raw byte layout, channel order, color-space handling, PNG
  alpha preservation, BMP/JPEG alpha compositing and JPEG quality behavior.
- Invalid image data and filesystem/encoder failures return actionable errors.
- CPU tests cover raw bytes, pixel conversion and file/validation errors without
  requiring Vulkan hardware.

Learning: Explain the boundary between GPU readback and CPU image encoding, and
why PNG preserves alpha while BMP and JPEG composite it, and why raw RGBA needs
separate extent metadata.
