# Prototype physics contract

Accepted specification for the optional CPU prototype physics module.
Implementation follows [the plan](../../development/PLAN.md).

## Purpose

This optional CPU module provides quick prototyping, like the primitive and GUI
tools. It is not a general game-physics engine and does not replace a specialized
physics library in applications that need one.

Implement the deliberately small scope within Panda. Do not add Jolt, PhysX, or
another general-purpose physics dependency to satisfy this module. Applications
remain free to integrate such libraries independently.

## Supported first-version model

| Body | Shapes | Authority |
| --- | --- | --- |
| Dynamic | Sphere | Physics advances linear motion; application uses forces, impulses, or explicit teleport |
| Static | Sphere, box, plane | Fixed collider pose |
| Kinematic | Sphere, box | Application supplies target motion/pose |

Support dynamic sphere versus sphere, box, and plane contacts, including dynamic
sphere-sphere pairs. Provide gravity, linear velocity, force, impulse, penetration
correction, basic restitution, and sliding friction.

Do not implement general dynamic rotation, torques, physical rolling, tumbling
boxes, robust stacking, joints, vehicles, mesh colliders, or continuous collision
detection. Kinematic boxes may rotate because their motion is prescribed; contact
response needs the appropriate surface velocity at the contact, not a dynamic
angular-inertia solver.

## Scene integration and stepping

Collider shape is explicit and independent from a visual mesh. A render transform
must not silently become an arbitrary supported collider shape.

Dynamic bodies publish their simulated position to the scene. Manual repositioning
uses an explicit teleport operation. Kinematic bodies receive application target
poses. Static bodies remain static unless explicitly reconfigured. Do not let GUI
and physics alternately overwrite the same authoritative state within one step.

Use a configurable fixed simulation time step, with 1/60 second as the initial
default direction, independent of rendering frequency. The application controls
stepping and catch-up policy. More substeps may reduce tunneling but are not a CCD
guarantee. Cross-platform bitwise determinism is not promised.

The first-version API below specifies scale restrictions, hierarchy interaction,
teleport velocity behavior, kinematic pose sampling, collision filtering/contact
reporting, and solver limits. A small API with explicit restrictions is preferable to
partially supported arbitrary scene transforms.

Optional debug visualization shows collider shapes through the tools/primitives
layer without making tools a physics dependency.

## Verification and learning

Test free fall, impulse/force integration, sphere-plane contact, sphere-sphere and
sphere-box response, restitution/friction limits, a translating/rotating kinematic
box, and explicit teleport. Demonstrate the documented high-speed failure case
rather than implying that substeps solve every collision problem.

Keep solver tests independent from Vulkan. Include a small visual example, explain
the authority and stepping rules, and measure CPU cost separately from rendering.

## Restricted first-version API

The public vocabulary is
`PhysicsWorld`, generation-aware `Body`, `BodyDesc`, `Contact`, `Shape`, and
`PhysicsOptions`; this module has no Vulkan or CUDA dependency.

`PhysicsWorld::create(PhysicsOptions) -> Result<PhysicsWorld>` creates a move-only
world; `add_body(BodyDesc) -> Result<Body>`, `remove_body(Body)`,
`apply_force(Body, Vec3)`, `apply_impulse(Body, Vec3)`,
`set_kinematic_target(Body, Pose)`, `teleport(Body, Pose, VelocityPolicy)`, and
`step(float fixedDt) -> Result<void>` are checked main-thread operations. `contacts()`
returns a read-only span valid until the next step/mutation. Default gravity is
(0,-9.81,0) m/s² and fixedDt is 1/60 s; the application decides how many steps to
execute. Reject nonpositive/nonfinite dt, mass, radius or half extents, and invalid
poses. No hidden frame-time accumulator or presentation-rate coupling exists.

Shape dimensions are physical meters independent of a visual mesh. Dynamic sphere
has positive mass, linear velocity and fixed orientation. Static sphere/box/plane and
kinematic sphere/box have explicit world poses; a plane is specified directly by a
world-space unit normal and signed offset. Boxes are oriented but only
kinematic/static bodies rotate. A bound scene
entity must have no parent and identity scale while attached; reject binding or
stepping after violating those restrictions. This deliberately avoids silently
approximating parent shear or treating visual scale as collider resizing. Changing
shape/size uses an explicit checked body reconfiguration at the update boundary.

`bind(Scene&, Entity, Body) -> Result<void>` is an optional adapter; a PhysicsWorld
can run without a Scene for tests. One body binds one entity and at most one Scene.
An explicit update sequence samples kinematic targets from application calls,
steps physics, then `publish(Scene&)` copies dynamic/kinematic poses to their visual
entities through checked Scene setters. Static pose is fixed until explicit
reconfiguration. The adapter detects missing/deleted entities and detaches with a
diagnostic; it does not recreate them. Scene gizmos request teleports/targets through
the PhysicsWorld rather than writing an authoritative dynamic pose directly.

Dynamic teleport clears velocity by default; `KeepVelocity` is explicit. Kinematic
target displacement and shortest-arc quaternion rotation over fixedDt determine
surface velocity at contacts. `teleport` marks a discontinuity and produces zero
kinematic velocity for that step. Reject targets with nonrepresentable orientation
or an unsupported bound entity transform. Forces accumulate for one step and then
clear; impulses affect velocity immediately. No sleeping or warm-started stable
stacks are promised.

Collision filters are uint32 layer and mask; a pair interacts only if each mask
contains the other's layer. `Contact` contains the two Body handles, world point,
normal A-to-B, penetration and normal impulse. Report current contacts after each
step in stable Body-ID pair order, with no event dispatcher or persisted contact
objects. Sphere-sphere, sphere-plane and sphere-oriented-box narrow phases use
closest-point tests. A broad AABB rejection precedes pair testing; the simple
first-version solver uses symplectic Euler, four sequential impulse iterations and
bounded positional correction. Restitution combines by maximum, friction by
geometric mean; both inputs are clamped to [0,1]. A configurable default maximum
of 1024 bodies makes quadratic worst-case work explicit; overflow is an error.

Tests after implementation cover finite-value rejection, stale Body handles,
gravity/forces/impulses, contact filters and ordering, every listed shape pair,
rotating kinematic contact velocity, teleport policies, scene deletion, unsupported
parent/scale, fixed-step catch-up chosen by the application, and a visible tunneling
counterexample.
