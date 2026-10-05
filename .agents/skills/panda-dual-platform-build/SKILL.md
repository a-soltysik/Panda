---
name: panda-dual-platform-build
description: Build and test Panda efficiently with the Windows checkout and a synchronized WSL Linux mirror.
---

# Build Panda on Windows and WSL

Keep the Windows checkout as the editable source of truth. Read
[`docs/development/testing.md`](../../../docs/development/testing.md) for the
project's build and test policy. Use the checked-in
[`scripts/build-wsl.ps1`](../../../scripts/build-wsl.ps1) for WSL builds; it
synchronizes source into a managed mirror under `~/src` on the Linux filesystem.
Do not edit the mirror. It has no Git metadata, and each sync replaces its source
files with the Windows checkout while preserving Linux build directories and
caches.

## Common commands

Run these from the repository root in PowerShell:

```powershell
cmake --preset msvc-development
cmake --build --preset msvc-development --parallel

cmake --preset msvc-tests
cmake --build --preset msvc-tests --parallel
ctest --preset msvc-tests

.\scripts\build-wsl.ps1 -Preset gcc-development -Target simple_scene
.\scripts\build-wsl.ps1 -Preset gcc-tests -RunTests
.\scripts\build-wsl.ps1 -Preset gcc-tests -IncludeSystemTests
```

The helper configures with an installed `ccache` and keeps its cache in the WSL
home directory. It chooses a conservative parallel job count from available
memory and CPU count; pass `-Jobs N` to override it. Pass `-SyncOnly` to update
the WSL mirror without building. `-TestRegex` selects CTest cases. The default
test-preset run excludes `system` tests; `-IncludeSystemTests` runs all tests
registered by the selected test-enabled preset.

## Verification and handoff

Use the preset that matches the requested work, then report the actual commands
and results. For source changes, include the relevant Windows build/test and WSL
build/test evidence when available. System tests exercise the local WSL
environment; they do not establish Windows behavior or prove that rendered output
was visually inspected. Keep user changes untouched and do not stage or commit.
