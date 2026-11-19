# Runtime graphics settings

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Let the application request MSAA off, 2x or 4x for window and offscreen rendering,
and change a window's presentation mode after creation. A caller that uses
defaults starts with FIFO for window presentation and requests 4x MSAA.
The application owns menus, presets and saved preferences.

Specifications: [Anti-aliasing and sampling](../../design/contracts/rendering.md#anti-aliasing-and-sampling),
[frame and window state](../../design/contracts/resources.md#frame-and-window-state).

## Acceptance

- The application can query supported choices and distinguish requested settings
  from effective settings. The benchmark records the effective choices.
- Verify changes in both directions. An unsupported MSAA request falls back
  to the highest supported sample count at or below the request. An unsupported
  presentation mode returns `Unsupported` and keeps the current mode. Confirm
  offscreen rendering is independent of presentation mode.
- Apply changes at frame boundaries. Verify with Vulkan synchronization
  validation that submitted frames keep their resources until completion.
- After a failed MSAA allocation, the previous configuration remains usable.
  Exercise swapchain recreation failure and recovery, including when Vulkan
  has retired the old swapchain.

Learning: Trace a 4x to 2x switch across two frames that overlap on the GPU.

## Current verification

- Unit and integration tests cover presentation changes, unsupported modes and
  retry after swapchain retirement. Attachment tests cover format-specific MSAA
  fallback, failure after allocating just part of a replacement, reuse of the
  previous configuration and successful retry.
- Windows Visual Studio 2026 18.11.3, RTX 4070 Ti with driver 617.14.
  `cmake --preset msvc-fast`, `cmake --build --preset msvc-fast --parallel`
  and `ctest --preset msvc-fast` with `VK_VALIDATION_VALIDATE_SYNC=true` passed.
  Of 670 cases, 665 passed and 5 skipped.
- Ubuntu in WSL. `cmake --preset gcc-fast`,
  `cmake --build --preset gcc-fast --parallel` and `ctest --preset gcc-fast`
  with `VK_VALIDATION_VALIDATE_SYNC=true` passed. Of 670 cases, 662 passed
  and 8 skipped.
- The system tests passed on both platforms. They switch the window presentation
  mode in both directions and verify that offscreen rendering leaves a pending
  window mode request queued. They exercise window and offscreen requests for
  4x, 2x, 1x and back to 4x after submitting frames without application waits.
- One-second Windows benchmark smoke runs at 320x240 with
  `--present-mode immediate` and `--samples 1`, `--samples 2` and `--samples 4`
  reported matching requested and effective settings.
- The documentation checker, Mermaid syntax target and `gcc-docs` passed.
- Windows `msvc-quality` (clang-tidy 22.1.8 and Cppcheck 2.19.0) and WSL
  `gcc-quality` passed. On both platforms, the public-header isolation and
  format targets passed. CUDA host and device analysis passed with
  `clang-cuda-analysis-windows` and `clang-cuda-analysis`.
