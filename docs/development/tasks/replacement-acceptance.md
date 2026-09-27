# Benchmarks and replacement acceptance

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Finish exact version-one profiles/reports, documentation and external consumer round-trips; collect cross-platform evidence for maintainer acceptance.

Non-goal: No unmeasured performance guarantee or automatic legacy removal.

Interface: panda-bench profile CLI and versioned JSON report schema.

Paths: `benchmarks/`, `examples/`, `docs/`, `tests/` and directly affected documentation.
Specifications: [Benchmarks](../benchmarking.md), [quality](../quality.md), [learning](../learning.md).

Collect results per supported environment and subsystem. Resolve missing evidence
before requesting replacement acceptance.

## Acceptance

Three-run baseline reports, visual/interop/physics evidence, ordinary+unity builds and full external consumer. Record skips and environment.

Learning: Reproduce one baseline and explain one measured bottleneck.
