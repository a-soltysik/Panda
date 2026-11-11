# Scene tools and safe edits

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add optional ImGui/ImGuizmo panels, input capture and update-phase intents, including scene-owned material edits and
application apply/reset hooks. Add a bounded set of transient debug visuals for scene cameras and lights: direction
arrows, point markers with wire range spheres, spot cones and camera frusta. Show these in the scene tools and `scene_demo`;
they do not create Scene entities or cast shadows. Scene editing uses `Panda::Scene`; preserve use of the existing
window wrapper without the scene module.

Non-goal: No reflection system, automatic solver save, implicit buffer mutation or general-purpose shape-authoring
framework.

Interface: Panda::Tools panels, input-capture result, update-phase intent sink and small debug-visualization helpers.

Paths: `../../../include/core/panda`, `src/tools/`, `examples/tools/`, `tests/tools/` and directly affected
documentation.
Specifications: [Tools](../../design/contracts/scene.md#optional-tools-and-safe-edits), [physics authority](../../design/contracts/physics.md#scene-integration-and-stepping), [resource replacement](../../design/contracts/resources.md#mutation-and-replacement).

## Acceptance

Input capture, failed keep-world edit, shared/clone material and reset rollback tests; camera/light visualizations match
their world transforms and do not change Scene snapshots; optional-off consumer build.

Learning: Demonstrate an edit that fails without changing the old scene or solver state, and inspect a point/spot light's
position, range and direction visualization.
