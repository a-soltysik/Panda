---
name: panda-code-workflow
description: Develop Panda code through CMake presets with ccache, frequent complete builds and tests, formatting after edits, and full pre-commit analysis.
---

# Develop and verify Panda

Apply this workflow to source, header, test, shader and build changes, and before
every commit. Read the selected task and the repository quality policy first.
Documentation-only iterations need their relevant documentation checks; the full
pre-commit procedure still applies before every commit.

## During changes

- Format every edited owned C/C++/CUDA file with `clang-format -i` after each
  logical edit, before compiling. Include new files; leave `legacy/`, dependencies
  and unrelated files alone. Use the project's LLVM major version.
- Run the fast preset below after each coherent code change, before starting
  another substantial change. It builds every enabled target and runs every
  registered test, including `system`. Do not substitute selected targets or tests.
- Only while editing an individual test, without production/header/build changes,
  may an iteration use `ctest --preset <fast-preset> -R <case>` instead.
  Before a commit, the full suite is still mandatory.
- Run quality earlier whenever useful. Fix failures before extending the change.
- ccache is mandatory for every local C/C++/CUDA build, including ad hoc commands.
  If it or another required tool is missing, resolve that prerequisite; never
  silently run uncached or disable a check. Keep MSVC's environment launcher
  outside ccache, and use embedded debug information (`/Z7`).

## Presets

Use native CMake/CTest commands from the repository root. Presets own options,
compiler launchers and build paths; do not add a wrapper or override `binaryDir`.

| Platform | Fast | Quality | CUDA analysis |
| --- | --- | --- | --- |
| Windows | `msvc-fast` | `msvc-quality` | `clang-cuda-analysis-windows` |
| Linux | `gcc-fast` | `gcc-quality` | `clang-cuda-analysis` |

Fast presets enable tests, tools, examples, benchmarks and CUDA, disable static
analyzers, and request unity wherever existing target rules permit.
Mock archive substitution requires ordinary production translation units with tests
enabled, and test/mock targets disable unity. Do not change those rules for speed.

Quality presets enable the complete ordinary build, analyzers, public-header
isolation and format checks. Their nested CUDA configuration analyzes host and
device code with Clang. Each platform keeps two main preset build directories:
fast and quality. CUDA analysis lives inside quality, not in a third main tree.
All use ccache. Fast and quality builds detect the local GPU's native CUDA
architecture; building never runs the example. CUDA analysis and CI explicitly
select architecture 89 to work without a GPU and avoid CMake's native-only code
generation flags in Clang's host analysis pass.

### During implementation

Windows:

```powershell
cmake --preset msvc-fast
cmake --build --preset msvc-fast --parallel
ctest --preset msvc-fast
```

Linux:

```sh
cmake --preset gcc-fast
cmake --build --preset gcc-fast --parallel
ctest --preset gcc-fast
```

Fast builds use `--parallel` for Ninja's automatic CPU-based parallelism. Quality
build presets limit parallelism to four jobs because analyzers use much more RAM;
do not override them with `--parallel`. Run Windows and WSL quality sequentially,
without another build running. Reduce concurrency further if memory is tight. Inspect
`ccache --dir .cache/ccache --show-config` and
`ccache --dir .cache/ccache --show-stats`; caching must be enabled. For Windows/WSL operation, follow
[panda-dual-platform-build](../panda-dual-platform-build/SKILL.md).

## Before committing

Use incremental builds by default. Clean build outputs or caches only when a concrete
toolchain/dependency change invalidates them or diagnostics show stale or corrupt
artifacts.

Run the complete sequence after final edits on Windows and the synchronized Linux
mirror. Stop on a failed command; do not proceed to commit.

Windows:

```powershell
cmake --preset msvc-quality
cmake --build --preset msvc-quality
cmake --build --preset msvc-quality --target all_verify_interface_header_sets panda_format_checks
cmake --preset clang-cuda-analysis-windows
cmake --build --preset clang-cuda-analysis-windows
cmake --preset msvc-fast
cmake --build --preset msvc-fast --parallel
ctest --preset msvc-fast
```

Linux:

```sh
cmake --preset gcc-quality
cmake --build --preset gcc-quality
cmake --build --preset gcc-quality --target all_verify_interface_header_sets panda_format_checks
cmake --preset clang-cuda-analysis
cmake --build --preset clang-cuda-analysis
cmake --preset gcc-fast
cmake --build --preset gcc-fast --parallel
ctest --preset gcc-fast
```

Complete the documentation checks as part of the pre-commit procedure:

```sh
python .agents/skills/panda-project-maintenance/scripts/check_docs.py
cmake --preset docs-checks
cmake --build --preset docs-checks
```

Generate/check API documentation on Linux with `cmake --preset gcc-docs` followed
by `cmake --build --preset gcc-docs`. The documentation presets own their auxiliary
directories; they are not additional fast/quality code profiles. Report failures
or missing tools instead of omitting these checks.

Any later code/build edit or formatting rewrite invalidates the relevant result;
repeat complete verification. A focused test run is never a pre-commit substitute.
Do not filter tests, exclude system labels, disable CUDA, or analyze selected files
to obtain a pass. Sanitizer and coverage profiles remain additional
checks when the task requires them; full tests means the whole enabled suite, not
every possible build configuration.

Record platform, commands, tool versions, test counts, failures and skips. Missing
tools, unavailable environments and skipped hardware scenarios must stay visible;
do not describe an incomplete required check as passed or commit past a failed or
unavailable required check without the maintainer's explicit exception. Full test
invocation does not establish GPU correctness when hardware cases skip.
Verification does not authorize staging, committing or pushing; preserve the
maintainer's commit-message and PR-title approval requirements.
