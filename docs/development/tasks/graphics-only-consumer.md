# Graphics-only consumer integration

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Demonstrate Panda's supported source-package workflow in an external graphics
application. The consumer uses `Panda::Panda` for mesh creation, uploads and direct
rendering, without requiring CUDA, Scene, Import or Physics.

Non-goal: No prebuilt SDK, install/export workflow, compute, persistence, asset
import, editor, physics or legacy replacement.

Interface: The existing `Panda::Panda` CMake target and public direct-renderer API;
use `Panda::Tools` only for its optional window helper.

Paths: consumer CMake project, `examples/`, package documentation and directly
affected use cases.

Specifications: [source package](../../design/package-and-toolchain.md#source-package-first),
[rendering](../../design/contracts/rendering.md),
[resources](../../design/contracts/resources.md).

## Acceptance

An external `add_subdirectory` consumer configures and builds on Windows and Linux
with CUDA, Scene, Import and Physics disabled. It renders a lit mesh through the
public API and documents the setup, upload lifetime and render loop. This consumer
must work before compute integration or replacement acceptance.

Learning: Trace mesh ownership from the consumer through upload, rendering and
Engine shutdown.
