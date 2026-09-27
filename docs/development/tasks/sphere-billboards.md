# GPU sphere billboards

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Render one cloud entity from GPU center/radius and optional color streams with conservative projection and sphere camera/shadow depth.

Non-goal: No per-particle entity, CPU readback or fluid surface.

Interface: BillboardCloudDesc, Scene::add_billboards and Scene::set_billboards.

Paths: `../../../include/core/panda`, `src/renderer/billboard/`, `shaders/`, `tests/gpu/` and directly affected documentation.
Specifications: [GPU clouds](../../design/contracts/rendering.md#gpu-cloud-representation), [scene updates](../../design/contracts/scene.md#gpu-clouds-and-simulation-domains), [compute declarations](../../design/contracts/compute.md#resource-declarations).

## Acceptance

10k/100k/1m structural counts; sphere-versus-mesh depth, near/inside and all shadow-view captures.

Learning: Explain why spherical shading normal alone does not fix quad occlusion.
