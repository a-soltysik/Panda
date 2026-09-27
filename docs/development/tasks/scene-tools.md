# Scene tools and safe edits

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add optional ImGui/ImGuizmo panels, input capture and update-phase intents, including scene-owned material edits and application apply/reset hooks.

Non-goal: No reflection system, automatic solver save or implicit buffer mutation.

Interface: Panda::Tools panels, input-capture result and update-phase intent sink.

Paths: `../../../include/core/panda`, `src/tools/`, `examples/tools/`, `tests/tools/` and directly affected documentation.
Specifications: [Tools](../../design/contracts/scene.md#optional-tools-and-safe-edits), [physics authority](../../design/contracts/physics.md#scene-integration-and-stepping), [resource replacement](../../design/contracts/resources.md#mutation-and-replacement).

## Acceptance

Input capture, failed keep-world edit, shared/clone material and reset rollback tests; optional-off consumer build.

Learning: Demonstrate an edit that fails without changing the old scene or solver state.
