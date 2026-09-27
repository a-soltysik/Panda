# Reference code

These examples help explain the rewrite. They are not dependencies of the new engine.

| Tree | Useful context |
| --- | --- |
| [Panda](panda/README.md) | Original Vulkan engine, demo, assets and build setup |
| [drip engine](drip-engine/README.md) | Newer C++ style, build configuration, resource wrappers, billboards and CUDA sharing |

Compare an example with the current specification before reusing its approach.
Keep good conventions and clear implementations; do not carry forward stale dependency
versions, broad suppressions or unproved GPU synchronization. The
[quality policy](../docs/development/quality.md) defines the standard for new code.

Keep reference source and assets unchanged and outside ordinary builds/formatting.
Neither tree has been validated as a standalone build in this layout; drip omits the
application and solver. Preserve embedded attribution and third-party notices.
Remove reference code only after the replacement is accepted and removal is explicitly
requested.
