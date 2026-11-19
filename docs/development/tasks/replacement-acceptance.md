# Benchmarks and replacement acceptance

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Finish exact version-one profiles/reports, documentation and external consumer round-trips; collect cross-platform
evidence for maintainer acceptance.

Non-goal: No unmeasured performance guarantee or automatic legacy removal.

Interface: panda-bench profile CLI and versioned JSON report schema.

Paths: `benchmarks/`, `examples/`, `docs/`, `tests/` and directly affected documentation.
Specifications: [Benchmarks](../benchmarking.md), [quality](../quality.md), [learning](../learning.md).

Collect results per supported environment and subsystem. Resolve missing evidence
before requesting replacement acceptance.

## Acceptance

- Collect three-run baseline reports, visual, interop and physics evidence,
  and ordinary and unity build results. Record skips and environment.
- Verify the complete import, edit, navigate, save and reopen workflow in
  the [Scene Composer example](scene-composer-example.md).
- Verify the external application combining graphics, assets, scene tools
  and compute.

Learning: Reproduce one baseline and explain one measured bottleneck.
