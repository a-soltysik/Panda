---
name: panda-dual-platform-build
description: Build and test Panda efficiently with the Windows checkout and a synchronized WSL Linux mirror.
---

# Build Panda on Windows and WSL

Keep the Windows checkout as the editable source of truth. Read
[`docs/development/testing.md`](../../../docs/development/testing.md) for the
project's build and test policy. Use the checked-in
[`scripts/build-wsl.ps1`](scripts/build-wsl.ps1) for WSL synchronization; it
synchronizes source into a managed mirror under `~/src` on the Linux filesystem.
Do not edit the mirror. It has no Git metadata, and each sync replaces its source
files with the Windows checkout while preserving Linux build directories and
caches.

## Preset commands

Follow [panda-code-workflow](../panda-code-workflow/SKILL.md) for complete iteration
and pre-commit checks. Native CMake/CTest presets own configurations and paths;
ccache is required. On Windows, for example:

```powershell
cmake --preset msvc-fast
cmake --build --preset msvc-fast --parallel
ctest --preset msvc-fast
```

After every source edit, synchronize from the Windows checkout before using the
native Linux commands in the mirror:

```powershell
.\.agents\skills\panda-dual-platform-build\scripts\build-wsl.ps1 -SyncOnly
wsl.exe -d Ubuntu -- bash -lc 'cd "$HOME/src/Panda-wsl-build" && cmake --preset gcc-fast'
wsl.exe -d Ubuntu -- bash -lc 'cd "$HOME/src/Panda-wsl-build" && cmake --build --preset gcc-fast --parallel'
wsl.exe -d Ubuntu -- bash -lc 'cd "$HOME/src/Panda-wsl-build" && ctest --preset gcc-fast'
```

## Recover WSL access and prerequisites

`Wsl/Service/E_ACCESSDENIED` from a sandboxed Windows command means the WSL host
service denied that launch; it does not mean Ubuntu or Linux tests are unavailable.
Retry the same diagnostic or build command through the command tool's escalated
execution (`sandbox_permissions: require_escalated`) with a reason naming the
requested Linux verification. Do not keep retrying the same restricted call, reset
WSL, or write an unverified "Linux unavailable" conclusion after one denial.
The synchronization helper also supports the current `gcc-fast` preset and full
system tests: `build-wsl.ps1 -Preset gcc-fast -RunTests -IncludeSystemTests`.

Once Ubuntu starts, check the actual Linux prerequisite reported by CMake. For the
built-in shader path, both `glslc` and `spirv-val` are required. If they are missing
and installation requires `sudo`, tell the maintainer the exact package command to
run in Ubuntu and continue after it completes. Never ask for the password in chat or
bypass package authorization by extracting packages into a private directory. Keep
host-service access failures distinct from missing Linux tools and actual configure,
build or test failures. Record only the final commands and their observed results.

Run the code workflow's full Linux quality/CUDA sequence in that same mirror.
Both platforms retain the fast and quality preset trees; CUDA analysis is nested
inside quality. Presets share `.cache/ccache` within each native source tree;
the Linux mirror keeps its cache on the Linux filesystem, outside `/mnt/c`.
Fast builds use `--parallel` for Ninja's automatic CPU-based parallelism. Quality
presets use four jobs; omit `--parallel` to preserve that limit. Run the two
platforms' quality builds sequentially, without another build running, to avoid
exhausting host memory. Reduce concurrency further when needed.

The older helper's optional uncached fallback and default `system` exclusion do
not satisfy agent verification. Do not use `-Target` to reduce build scope.
`-TestRegex` is permitted only for test-only iteration, never pre-commit checks.
Fast test presets run the full suite without filters.

## Verification and handoff

Use the preset that matches the requested work, then report the actual commands
and results. For source changes, include the relevant Windows build/test and WSL
build/test evidence when available. System tests exercise the local WSL
environment; they do not establish Windows behavior or prove that rendered output
was visually inspected. Keep user changes untouched and do not stage or commit.
