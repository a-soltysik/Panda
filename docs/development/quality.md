# Code and verification quality

These rules apply from the first source file. Completed foundation evidence is
recorded in [Build foundation](tasks/build-foundation.md); the [plan](PLAN.md) tracks
remaining work.
The [package specification](../design/package-and-toolchain.md) owns dependency and
platform boundaries.
The [project conventions](conventions.md) own maintainer preferences for code style
and organization beyond the checked-in formatter.

## Readable modern C++

- Use C++23 for engine code and the agreed C++20 CUDA boundary. Prefer standard
  value types and algorithms over bespoke containers, result types or frameworks.
  Use concepts, ranges and templates when they clarify a real requirement.
- Express ownership with RAII and values. Prefer unique ownership; use shared
  ownership only for genuinely shared lifetimes. Borrow with references, `span`
  or `string_view` where suitable, and make the lifetime obligation explicit.
  Avoid dangling views, hidden copies and allocation on steady-state GPU paths.
- Keep functions and interfaces focused, const-correct and explicit about errors.
  Use `constexpr` for meaningful compile-time use, not decoration; its definition
  must be visible to callers that need constant evaluation. Use `noexcept` only
  when the behavior supports it and `[[nodiscard]]` for results callers must inspect.
- Follow the [error contract](../design/contracts/resources.md#errors):
  `std::expected` for recoverable failure, `std::optional` for ordinary absence,
  and checked diagnostics in release builds. Catch dependency exceptions at adapters;
  do not disable exceptions globally when those adapters need them.
- Keep headers self-contained, include what is used and minimize public dependencies.
  Prefer simple composition to inheritance or abstraction without a current use.
- Inspect recoverable errors unless ignoring them is justified.
- Follow the checked-in formatter and project conventions. Do not change project
  style during unrelated work.
  Keep private paths and task bookkeeping out of production code.

Review correctness and clarity even when analyzers pass. Modern language features
and a large warning list do not by themselves make a good design.

## Use references with judgment

Start with relevant [legacy examples](../../legacy/README.md) and existing new code.
The drip configuration is the style reference; its target helpers and resource
wrappers are examples to assess, not a complete template to copy. Check each idea
against the current contract and current upstream API/tool documentation.

Keep sound naming, encapsulation and implementation patterns. Reject global build
setting leakage, broad analyzer suppressions, stale dependency selections and
unproved GPU lifetime/barrier assumptions. Prefer an established support library
within the approved dependency set over a home-grown replacement. Explain a useful
deviation briefly next to its owning configuration or design.

## Reproducible CMake

Use target-scoped language features, include paths, definitions and dependencies.
Keep warnings, analysis and hardening private to Panda targets, including its tests;
do not impose them on consumers or third-party code. Use CMake functions for reusable
local setup, options for user-facing switches and presets for supported workflows.
Normal development and CI should not depend on undocumented terminal overrides.

Enable optional features only when implemented. CUDA off means no CUDA language,
toolkit discovery, headers or link dependencies. Development defaults and dependency
pins follow the package specification. Check current upstream releases when selecting
a dependency; pin the chosen compatible release by its version tag.
Do not fetch a moving latest revision on every build or inherit legacy versions blindly.

Require an ordinary non-unity build and compile public headers in isolation. Analyze
ordinary translation units with their real compiler arguments or compilation database.
Keep unity as a separate build check; it can hide missing includes and is not proof
of ODR correctness. Use one exact supported toolchain per preset/environment and
record compiler, standard library and SDK compatibility through actual probes.

## Effective quality checks

Strong compiler warnings, clang-tidy, cppcheck and formatting form the foundation.
Cover Panda source, tests and owned headers automatically as targets grow, excluding
third-party and reference trees. Verify header diagnostic filters; an analyzer parsing
a header does not mean its findings are reported. Avoid separate hand-maintained
file lists or scripts duplicating CMake analyzer invocations.

`.clang-tidy` owns the checks, diagnostic severity and inclusion filter for headers
under `include/`, `src/`, `tests/` and `examples/`. CMake attaches the tools privately
to owned targets, excludes `_deps/` and `legacy/`, and supplies compiler compatibility
arguments. Dependencies use system includes; system-header diagnostics remain disabled.
MSVC analysis uses the Windows MSVC target and the configured exception mode,
including when the installed LLVM tools default to MinGW. cppcheck's command-line
settings stay in CMake because `.clang-tidy` does not configure cppcheck.

Pin compatible tool versions in CI and use the same configuration locally.
Allow compatible local patch versions; a CI pin is not an exact-version build gate.
Check actual preset inheritance and overrides. When establishing or changing a check,
use a temporary deliberate violation to prove the real quality build rejects it;
include source and header coverage where relevant, then remove the violation.
A successful invocation or a printed warning is insufficient evidence of enforcement.

Keep suppressions narrow and justified. Fix tool invocation/parsing failures rather
than globally hiding internal errors or whole categories. Do not weaken requirements,
change expected results or exclude code merely to make checks pass.

Test sources, mocks and support headers inherit the root clang-tidy configuration
through `tests/.clang-tidy`, which disables the two magic-number checks and
`cppcoreguidelines-non-private-member-variables-in-classes` /
`misc-non-private-member-variables-in-classes`. Numeric fixtures and expected
values do not need production-style named constants; GoogleTest fixtures may expose
mock fields to their generated subclasses. Other checks remain enabled.

## Continuous integration

[Quality](../../.github/workflows/quality.yml) compiles owned production, example
and test sources with warnings-as-errors, clang-tidy and cppcheck on Linux/GCC and
Windows/MSVC. Separate CUDA jobs install Toolkit 13.4, analyze the optional
module and its tests, and run the CUDA CTest cases without a GPU. They leave
examples off because `cuda_connection` detects a native GPU architecture when
building its kernel. Toolkit 13.4.92 files are cached separately per OS, so a
cache hit skips the NVIDIA download and installation. The ordinary quality jobs
build and analyze examples.
CTest selections check formatting.
[Tests](../../.github/workflows/tests.yml) runs unit and integration cases on
Linux/GCC, Linux/Clang and Windows/MSVC, plus separate Linux ASan/UBSan and Windows
ASan builds. Runtime presets disable static analysis and formatting to keep those
results in the quality workflow. Native-display scenarios remain explicit local
system tests; hosted jobs do not claim native-window or rendering evidence.

Both workflows run for pull requests targeting `master` and pushes to `master`.
Feature-branch pushes do not start a second run alongside the pull request.

Both workflows use the small [setup action](../../.github/actions/setup/action.yml)
and matching CMake configure/build/test presets. They build their own dependencies
and executables; no compiled artifacts are shared across compilers or sanitizer
configurations. CPM sources and its versioned script persist in `.cache/cpm`;
`actions/cache` restores them using an OS/dependency-definition key and an OS fallback.
CPM still resolves each package from its requested arguments, so older restored
sources do not replace newly pinned dependencies. TSan and MSan are not hosted jobs
until their environments are verified.

## Stack trace support

`PANDA_ENABLE_STACKTRACE` defaults to ON. Configuration checks actual C++23
compilation and linkage of `std::stacktrace`; if needed with GNU/Clang it checks
`stdc++exp`, then propagates that link requirement from the embedded common code.
An unsupported library produces an actionable error rather than silently omitting
traces. Set the option to OFF to build without the feature. No Boost dependency or
exception-interception hooks are introduced. The [error contract](../design/contracts/resources.md#errors)
owns capture timing, failure behavior and limits.

## CPU sanitizers

The `PANDA_ENABLE_SANITIZER_*` options are independent `ON/OFF` switches, visible
in `ccmake`, and default to OFF:

| Suffix | Purpose | Configured toolchains |
| --- | --- | --- |
| `ADDRESS` | Invalid memory access and lifetime errors | GCC/Clang with an available runtime; MSVC ASan |
| `UNDEFINED_BEHAVIOR` | Undefined behavior, including invalid arithmetic | GCC/Clang |
| `THREAD` | Data races | Linux GCC/Clang |
| `MEMORY` | Uninitialized reads | Linux Clang with an instrumented C++ toolchain |
| `LEAK` | Standalone leak detection | Linux GCC/Clang |

ASan and UBSan can run together. TSan and MSan need separate builds from ASan,
LSan and each other; incompatible selections fail configuration. UBSan can accompany
those other modes. On Linux, ASan already includes leak detection by default;
`LEAK=OFF` does not disable that integrated check. Explicit LSan is useful for
a separate leak-only build. These compiler capabilities are documented by
[GCC](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html) and
[LLVM](https://clang.llvm.org/docs/AddressSanitizer.html).

Sanitizer flags apply to Panda code, test code and source-built dependencies. Compiler
instrumentation stays private; runtime link options propagate from static libraries
to the final application. Parent-provided imported libraries cannot be recompiled
here and must already be compatible. In particular, MSVC ASan requires matching
[STL annotations](https://learn.microsoft.com/en-us/cpp/sanitizers/error-container-overflow);
Panda does not disable these checks to allow mixed libraries.

The matching configure/build/test presets are `gcc-sanitizers` (Linux ASan + UBSan),
`gcc-thread-sanitizer` (Linux TSan) and `msvc-sanitizers` (Windows ASan). They retain
warnings-as-errors, while static analyzers and formatting run in the separate
quality presets. Their tests exclude native-display system scenarios.
MSVC uses `RelWithDebInfo` to avoid incompatible `/RTC` checks and disables
incremental linking on instrumented targets. Its ASan runtime DLL is copied next to
owned executables, including before GoogleTest discovery, so later CTest runs work
from ordinary PowerShell. Other MSVC configurations must also avoid `/RTC` and `/ZI`.
[MSVC compatibility](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-known-issues).

MSan is an advanced toolchain configuration, not a ready preset. It requires
instrumentation of dependencies and the C++ standard library; a successful compiler
and runtime link probe cannot establish that compatibility. Supply the instrumented
toolchain externally; Panda rebuilds its source dependencies with the selected
flags, but does not build a replacement standard library or suppress findings from
an incompatible one. [MemorySanitizer requirements](https://clang.llvm.org/docs/MemorySanitizer.html).

Each sanitizer configuration probes compilation and runtime linkage before dependency
discovery. Run the existing Panda tests with instrumentation; do not maintain tests
of the sanitizer implementations themselves. A temporary defect can verify new
build wiring once and must be removed afterward. Sanitizers are development checks
and remain outside performance runs.

## Tests and evidence

Use the GoogleTest/gMock and CTest setup described in [test organization](testing.md).
Test observable contracts,
boundaries and failure cases: each case should detect a plausible defect, not repeat
an implementation assertion. Do not write a custom test framework or synthetic
projects merely to inspect CMake variables. Validate consumption through a real
application as the renderer becomes available.

| Check | What it establishes |
| --- | --- |
| CPU tests and appropriate sanitizers | Algorithms, state transitions, error paths and host lifetimes |
| Compile and package checks | Header isolation, ordinary/unity builds and language/dependency boundaries |
| Shader/ABI checks | Reflected offsets, strides, supported interfaces and rejection of invalid layouts |
| Vulkan/CUDA tests | Actual device behavior, synchronization, retirement and failure handling |
| Visual inspection and benchmarks | Image behavior and performance under recorded effective settings |

Use Vulkan validation, including synchronization validation, and Compute Sanitizer
where applicable. Keep diagnostic overhead out of performance baselines. A screenshot
or CPU test cannot establish cross-API safety. Record GPU/environment skips separately
from passes; Windows and Linux evidence remains required where specified.

Small maintenance scripts need execution after a change, not standalone test suites.
This exception does not apply to engine algorithms or shader/GPU behavior.

## Public API and learning

Use Doxygen at public declarations (`@brief`, `@param`, `@return` as relevant).
Explain ownership, borrowing lifetime, units, thread/update phase, defaults, errors,
allocation, blocking and device requirements where they matter. Distinguish enqueued
from completed work. Avoid comments that merely translate the function name.

Keep guides and rationale in Markdown; generated API pages are build artifacts.
Enforce useful Doxygen warnings through the documentation harness and compile public
examples. Each handoff explains one invariant, a failure case and the main tradeoff;
see [learning](learning.md). Performance evidence follows [benchmarking](benchmarking.md).

References: [CMake target features](https://cmake.org/cmake/help/latest/command/target_compile_features.html),
[clang-tidy](https://clang.llvm.org/extra/clang-tidy/),
[Vulkan validation](https://docs.vulkan.org/guide/latest/validation_overview.html),
[Compute Sanitizer](https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html),
[Doxygen](https://www.doxygen.nl/manual/config.html).
