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
