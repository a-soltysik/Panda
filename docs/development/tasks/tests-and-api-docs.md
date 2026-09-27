# Tests and API documentation

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add ordinary/non-unity and unity build jobs, extend CTest for new engine behavior,
compile public headers in isolation, and configure useful Doxygen warnings.
Extend the existing GoogleTest/gMock, CTest and quality setup as behavior becomes available.
The [test organization](../testing.md) defines unit, integration and system boundaries.

Non-goal: No engine feature or synthetic GPU pass claim.

Interface: CMake development presets, expanded CTest coverage and API-doc configuration.

Paths: `tests/`, `.github/workflows/`, `cmake/`, `docs/development/` and directly affected documentation.
Specifications: [Quality](../quality.md), [learning](../learning.md).

## Acceptance

Run ordinary and unity builds, header-isolation checks and documentation generation.
Demonstrate that missing public API documentation fails the documentation job.
If analyzer wiring changes, verify its failure path through the quality build.
Record actual local/CI results and unavailable environments.

Learning: Explain which checks catch C++ errors and which require a real GPU.
