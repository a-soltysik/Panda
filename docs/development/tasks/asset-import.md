# Static asset import

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Add optional Assimp adapter for accepted static glTF/GLB, OBJ, FBX, STL and PLY subsets with bounded paths and warnings.

Non-goal: No animation, streaming or complete Assimp-format guarantee.

Interface: import_asset, instantiate and structured import warnings.

Paths: `../../../include/core/panda`, `src/import/`, `tests/import/` and directly affected documentation.
Specifications: [Import adapter](../../design/contracts/scene.md#import-adapter), [materials](../../design/contracts/rendering.md#materials-faces-and-output), [dependencies](../../design/package-and-toolchain.md#dependency-boundary).

## Acceptance

Representative fixture imports, strict warning failures, material/culling captures and CPU-data retention check.

Learning: Explain why one source image can need separate color/data texture interpretations.
