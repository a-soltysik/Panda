# Build foundation

Order, dependencies and status: [plan](../PLAN.md).
Specifications: [package boundary](../../design/package-and-toolchain.md#source-package-first),
[dependency pins](../../design/package-and-toolchain.md#dependency-pins-and-notices),
[quality](../quality.md).

## Result

A minimal static `Panda::Panda` source package with a reproducible development setup
that subsequent engine tasks can extend. Formatting, warnings, static analysis and
the first C++ behavior test work together from the first source files.

Paths: root CMake/presets and quality configuration, `cmake/`, `../../../include/core/panda`,
`../../../include/common/panda`,
`src/`, `tests/`, CI and affected documentation.
No renderer, CUDA implementation, optional module scaffolding or legacy source edits.

## Acceptance

- The core builds as C++23 with no CUDA discovery or reference-tree dependencies.
  Language/include requirements propagate; private diagnostics do not leak to consumers.
- Shared diagnostics compile in an internal object target and are embedded once in
  core. Add-ons inherit them through core; no separate common archive is delivered.
- Options and development/quality presets express the supported workflows. CI tools
  are version-pinned; compatible local versions work with the same configuration.
  Ordinary quality builds are non-unity.
- Dependencies come from checked upstream releases with explicit version pins.
  Preserve third-party notices and avoid duplicate compatible parent targets.
- Warnings, clang-tidy, cppcheck and formatting cover owned code, including headers
  and tests. Temporary violations fail the actual quality build; remove them afterward.
- The GoogleTest/CTest version test passes. Public declarations use Doxygen and
  established C++ conventions; no replacement test framework or synthetic CMake fixtures.
- Report compiler/build results on available Windows/Linux environments and explicit
  gaps. Hosted CI success requires an actual hosted run.

The [tests and API documentation task](tests-and-api-docs.md) adds the broader
ordinary/unity, header-isolation and documentation jobs. This task establishes
effective quality checks before any engine implementation builds on them.

## Current evidence and remaining work

Shared diagnostics use `std::stacktrace` for fatal failures and explicit
`log::writeWithStacktrace` requests, limited to 32 frames. Ordinary entries remain
untraced. Filtering precedes collection; the fatal context is flushed before the
best-effort trace, and capture/formatting failure cannot suppress that context or
abort. No Boost dependency or exception throw-stack interception is used.
`PANDA_ENABLE_STACKTRACE` defaults to ON; compilation/linkage is checked and
`stdc++exp` propagates to consumers when required by libstdc++.

With stack traces enabled, Windows GCC passed the full quality build and 52
noninteractive checks (35 unit, three integration, 14 formatting). Linux Clang and
Windows MSVC passed 38 behavior cases each; MSVC also passed the common-target
quality build. The diagnostics suite passed 23 cases under Linux GCC ASan/UBSan and
Windows MSVC ASan. A Windows MSVC build with traces disabled passed 22 diagnostic
cases, including the explicit API emitting an ordinary message. Small standalone
probes produced symbolized frames on Windows GCC/MSVC and Linux GCC/Clang. Debug
symbols and optimization still govern frame detail; failure-injection for trace
allocation/symbolization remains unverified; hosted CI success is still pending.


Windows/MSYS2 UCRT64: GCC 16.1.0, CMake 4.4.2, Ninja 1.13.2,
clang-tidy/clang-format 22.1.8 and cppcheck 2.22.0. The non-unity Debug build
passed with compiler warnings-as-errors and all three quality tools enabled.
Current Windows commands: `cmake --build build-diagnostics-windows -j 2`
and `ctest --test-dir build-diagnostics-windows --output-on-failure -LE system`.
All 50 checks passed: 33 unit cases, three integration cases and 14 formatting
checks. Death tests are ordinary GoogleTest cases in their owning unit files and
use the shared framework runner. Sink error tests cover both stream status and
contained dependency exceptions. The real GLFW integration uses the null platform.
Linux GCC 16.0.1 in `build-engine-linux` passed the same 50 checks and the non-unity
Debug build with warnings-as-errors, formatting, clang-tidy and cppcheck enabled.
Windows MSVC 19.51.36260 in `build-msvc-plain` also passed all 50 checks, using the
same `-LE system` selection. Its non-unity Debug build includes Tools and the example,
with warnings-as-errors, formatting, clang-tidy and cppcheck enabled. Configuration
and direct Ninja builds run from ordinary PowerShell processes without a developer
shell; a core-only build also passed with spaces in its build-directory path.
The compiler, linker and analyzers share the recorded MSVC environment through the
ordinary `MsvcRun.cmake` launcher. A temporary
duplicate mock selection in the real Window unit target failed configuration with
the expected diagnostic; the normal configuration was restored.
Windows also passed the full noninteractive suite with `PANDA_BUILD_TESTS=ON`
and cached `PANDA_BUILD_TOOLS=OFF`, confirming tests enable Tools. With both OFF,
the core-only build passed and its target graph contains no Tools or GLFW.
Tools also built with tests OFF and `PANDA_BUILD_TOOLS=ON`. Requesting examples
with both OFF was rejected before dependency discovery.
The build uses upstream GLFW 3.4, GLM 1.0.3 and GoogleTest/gMock 1.18.0 sources
through CPM; GLFW and GLM use cached source overrides, while GoogleTest was fetched
by CPM. Dependency notices remain in those source trees.

`panda_common` compiles Logger.cpp, Assert.cpp and FunctionName.cpp with only standard-library
headers. Core embeds their object files and publicly propagates the common
include directory. There is no common
archive or public Common target. The Tools module publicly links core and inherits the
same definitions and headers. Current Tools/example build evidence is recorded in
the [window task](window-wrapper.md).

An intentional formatting violation in the common Logger.hpp failed the actual
`panda_common` build. The header was restored and the quality build/test suite
then passed. The object target's formatting dependency also runs without a link
step. CTest is enabled before source subdirectories so their formatting checks
are registered. Compiler and analyzer findings in the moved headers were
reported by the real target builds. The constructor that intentionally converts
format literals has one documented cppcheck suppression; other diagnostics
remain enabled.

The header inclusion filter is stored in `.clang-tidy`; dependency/reference
exclusions and compiler compatibility arguments are supplied by CMake. A temporary
short local name in an owned header was rejected by the actual GCC quality build
with `readability-identifier-length` treated as an error. The probe was removed and
the original source rebuilt successfully.

CPU sanitizer presets use separate build directories and keep the detectors outside
normal development builds. Linux GCC ASan + UBSan (`gcc-sanitizers`) and Windows
MSVC ASan (`msvc-sanitizers`) each passed 49 noninteractive checks: 33 unit cases,
three integration cases and 13 formatting checks, with examples disabled. MSVC's
source dependencies retain STL annotations and its runtime DLL is available beside
test executables without a developer shell. Temporary overflow, division-by-zero
and leak probes confirmed the runtime wiring, then were removed. Incompatible
ASan/TSan selection failed configuration. Standalone Linux LSan compiled and linked
the core unit executable and passed its existing version test.
TSan execution and a complete instrumented MSan toolchain remain unverified.
The separate test workflow includes GCC, Clang and MSVC CPU jobs plus ASan/UBSan
jobs. The quality workflow owns analysis and formatting, including the example.
Both share tool installation through a local composite action. The initial hosted
Windows quality job failed during tool setup because Chocolatey did not provide
the requested cppcheck version. Windows now pins the available 2.19.0 package; its
package and official installer downloads were verified, including the matching
SHA-256 checksum. A hosted quality pass remains pending. Runtime presets disable
formatting as well as static analysis. The GCC, Clang and MSVC CPU presets and both
ASan configurations each passed
their 36 behavior cases after the separation; formatting remains in the quality workflow.
Top-level configuration defaults to a persistent `.cache/cpm` source cache, while
explicit CMake and environment cache paths remain supported. Fresh Windows and
Linux build directories configured with dependency fetching disabled from the
populated cache. Moving an existing build to the new cache also reloads CPM correctly.
Dependency configuration retains warnings/errors and prints concise library lines.
Workflow YAML, composite-step shells and preset references were checked locally;
hosted cache restore/save remains unverified.
The test clang-tidy configuration disables both magic-number checks without
changing their production configuration.

`python .agents/skills/panda-project-maintenance/scripts/check_docs.py` and
`git diff --check` passed. The package contract owns the common/module layout.
The Linux Clang 22.1.2/LLD Debug package build passed with warnings-as-errors and
formatting; its analyzers were disabled. Its noninteractive suite passed all 50
checks. Function-name formatting skips Clang anonymous-namespace qualifiers before
recognizing the parameter list, preserving the existing bounded, allocation-free
parser. Text diagnostics also strip the Panda source-root prefix using a
non-owning view, preserving original metadata and external paths. The compiler-signature, real-method/lambda and bounded-output cases also
passed on Windows GCC and MSVC with their quality builds enabled.
Release, unity, hosted CI success, documentation generation and GPU execution remain
unverified for the current source. The subsequent toolchain
and tests/API-documentation tasks retain their acceptance scope. Maintainer
acceptance and later-phase authorization remain separate.

Learning: a directly linked CMake object library contributes its objects to the
core archive, while transitive consumers inherit its usage requirements without
embedding the objects again. This keeps a single logger definition and avoids
an additional library for the application's link command.
