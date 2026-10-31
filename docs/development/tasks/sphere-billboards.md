# GPU sphere billboards

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Render one cloud from application-supplied GPU center/radius and optional color streams with conservative projection and sphere camera/shadow depth, without requiring a scene.

Non-goal: No per-particle entity, CPU readback or fluid surface.

Interface: BillboardCloudDesc and the direct renderer's draw interface; an optional scene may manage cloud descriptions.

Paths: `../../../include/core/panda`, `src/renderer/billboard/`, `shaders/`, `tests/gpu/` and directly affected documentation.
Specifications: [GPU clouds](../../design/contracts/rendering.md#gpu-cloud-representation), [scene updates](../../design/contracts/scene.md#gpu-clouds-and-simulation-domains), [external-buffer handoff requirements](../../design/contracts/compute.md#required-synchronization-contract).

## Acceptance

10k/100k/1m structural counts; sphere-versus-mesh depth, near/inside and all shadow-view captures.

Learning: Explain why spherical shading normal alone does not fix quad occlusion.
