# Test organization

GoogleTest and gMock provide the behavior-test runner and mock expectations. CTest
selects cases discovered by GoogleTest. Dependencies are pinned through
CPM only when `PANDA_BUILD_TESTS` is enabled. Tests do not introduce virtual functions
or mock configuration into production interfaces.

`PANDA_BUILD_TESTS=ON` enables the implemented CPU-only modules and builds every
test level, including Tools tests when `PANDA_BUILD_TOOLS` is OFF in the cache.
CUDA remains selected explicitly with `PANDA_BUILD_CUDA` so an ordinary test
build needs no toolkit. The CUDA connection is a runnable example, not a CTest
case. With CUDA selected, unit cases run the real `CudaAdapter.cpp` against a
controlled `cudaGetDeviceCount` call, and an integration case calls the real
CUDA Runtime through the public facade;
the latter can run on a machine without a usable device. Select execution
through CTest labels; building system tests does not require running them.

## Levels and execution

| Level        | Directory                    | Purpose                                                                        | CTest labels  |
|--------------|------------------------------|--------------------------------------------------------------------------------|---------------|
| Unit         | `tests/cases/unit/<module>`        | Test real Panda implementation with controlled dependencies, or pure CPU logic | `unit`        |
| Integration  | `tests/cases/integration/<module>` | Test real components and dependencies together                                 | `integration` |
| System smoke | `tests/cases/system`               | Exercise bounded native startup, event processing and teardown                 | `system`      |

Test executables are grouped by level and compatible dependency selections, not
necessarily by a whole module. Their
CMake files own their sources and registration. Each unit source file tests one
class or coherent free-function module; its death tests stay in that file.
`tests/cases` holds executable test scenarios; `tests/support` holds shared test
plumbing and target helpers. `tests/support/mocks/panda/<module>` holds
Panda implementation replacements, and `tests/support/mocks/external/<library>` holds
dependency mocks. Each replacement pairs a `*Mock.hpp` declaration with a thin
`*Stub.cpp` forwarding implementation. Target helpers live in
`tests/support/cmake/TestTargets.cmake`; suite CMake files declare only sources, libraries
their level and optional `MOCKS` through `panda_add_test`. Every executable links the same
`GTest::gmock_main` runner; tests do not define their own main. The helper prefixes
each CTest name with its level (`unit.`, `integration.`, `system.`) and applies the
matching label. GoogleTest suite and case names stay unchanged.

Test-only CMake aliases describe their role: `PandaTest::Support` exposes the
generic mock fixture, `PandaTest::LogCapture` observes the real logger, and
`PandaTest::GlfwMock` replaces GLFW. Log capture is an observer/helper, not a
replacement logger. `PandaTest::MockHeaders` exposes mock declarations, including
`mocks/panda/common/SinkMock.hpp`, without selecting stub implementations.
With CUDA enabled, `PandaTest::CudaRuntimeMock` replaces only the external
`cudaGetDeviceCount` function while `CudaAdapter.cpp` remains real.
Tests list the production module explicitly alongside these
helpers, so their real subject remains visible.

## Names

| Level       | Source file                                                              | GoogleTest suite                                                       | Executable target                                                          |
|-------------|--------------------------------------------------------------------------|------------------------------------------------------------------------|----------------------------------------------------------------------------|
| Unit        | Class name, e.g. `Window.cpp`; free functions use their module name      | Class/module name, with `Test` when it is a fixture, e.g. `WindowTest` | `panda_unit_<component>`; compatible cases may share `panda_unit_<module>` |
| Integration | Collaboration or behavior, e.g. `WindowLifecycle.cpp`, `FileLogging.cpp` | Behavior followed by `Integration`, e.g. `WindowLifecycleIntegration`  | `panda_integration_<module>`                                               |
| System      | Application scenario, e.g. `ApplicationStartup.cpp`                      | Scenario followed by `System`, e.g. `ApplicationStartupSystem`         | `panda_system_<scenario>`                                                  |

Case names describe the observed behavior, such as `RejectsMovedFromUse`.
Unit death suites end in `DeathTest` so GoogleTest schedules them correctly;
they remain in the source file for the tested class/module. System tests are
named for application scenarios even while an initial scenario exercises only
window startup.

## Commands

From a configured build with tests enabled:

```sh
cmake --build <build-directory>
ctest --test-dir <build-directory> --output-on-failure -L unit
ctest --test-dir <build-directory> --output-on-failure -L integration
ctest --test-dir <build-directory> --output-on-failure -L system
```

Use `-LE system` for a noninteractive run that also includes formatting checks.
GoogleTest death tests assert both process termination and the originating
diagnostic. GoogleTest owns subprocess handling and platform differences; test
files need no separate fatal executable, custom main or conditional process code.

The real GLFW null-platform integration check needs no display. The native smoke
uses real native windows in a bounded test. It reports an unavailable
platform as a skip; creation and later operation failures remain failures. This
does not establish that the interactive example or a rendered image was inspected.
Rendering-test policy remains deferred.

## Controlled dependencies

Keep the implementation under test real. Replace its dependency at build/link time:
production declarations and implementation sources remain unchanged, while a thin
stub forwards dependency calls to a non-virtual gMock object. Do not maintain fake
backend state, simulated resource ownership or a second backend implementation.
Each test declares relevant arguments, answers and required call ordering.

Production modules are `STATIC` libraries built once per configuration. Shared
stubs are `OBJECT` libraries linked directly into the selected test executable.
The stub's definitions are present before static archives are searched, so the
linker does not extract the production member implementing those definitions.
Other archive members remain available normally. This uses ordinary strong symbols
and archive extraction, without weak symbols, duplicate-definition suppression or
ordering between competing static libraries. CMake documents
[direct object-library linking](https://cmake.org/cmake/help/latest/command/target_link_libraries.html#linking-object-libraries).

Declare the implementation identity in `REPLACES`. `API_LIBRARY` supplies its
production compilation requirements through `COMPILE_ONLY`; it defaults to
`REPLACES` when that identity is itself a target, such as GLFW:

```cmake
panda_add_mock(panda_test_glfw_mock
        REPLACES glfw
        SOURCES external/glfw/GlfwStub.cpp
        LIBRARIES PandaTest::MockHeaders)
add_library(PandaTest::GlfwMock ALIAS panda_test_glfw_mock)

panda_add_test(panda_unit_window unit
        SOURCES Window.cpp
        LIBRARIES Panda::Tools
        MOCKS PandaTest::GlfwMock)
```

For a Panda class, use its qualified name as the identity and its module as
`API_LIBRARY`, for example `REPLACES panda::tools::Window API_LIBRARY Panda::Tools`.
This does not require a production target for that class. Additional mock
dependencies belong in `LIBRARIES` or ordinary target configuration. The mock
target records `PANDA_REPLACES`; `panda_add_test` rejects unknown mock targets,
targets not declared through `panda_add_mock`, and duplicate replacement identities.

Window unit tests, the example and integration link the same `Panda::Tools`
archive. Unit tests additionally link GLFW stub objects; integration uses real
GLFW. The real dependency remains in the link graph with its normal transitive
system libraries, but members whose definitions are supplied by the stub are not
extracted. Tests do not rebuild production sources or select another module variant.

The replacement unit is a complete production `.cpp`, not an arbitrary subset of
its symbols. Keep independently replaceable implementations in separate source
files and supply every externally used definition from that file. If another
required symbol causes that member to be extracted, overlapping strong definitions
produce a link error. Component sources are excluded from unity in test builds;
stubs are also compiled separately. Do not use whole-archive linking or forced
symbol inclusion for replaced archives. Shared-library implementations, definitions
inline in callers and template instantiations in callers cannot be replaced this way.
Stub declarations, ABI and compilation configuration must match production.

Source lists contain `.cpp` files; headers are included normally, analyzed through
translation units and discovered separately for formatting. Libraries follow module
responsibilities rather than class count. All cases in one executable share its
replacement set; incompatible selections require separate executables. Tests depend
on production targets and shared support, never on another level's test executable
or fixture. `ImplementationSubstitution.cpp` checks a mocked core archive member
alongside the real logger from the same archive; the ordinary version unit test
checks the production implementation in a separate executable.

A generic `ScopedMock<Mock>` registration exposes the current mock to stub entry points.
Tests can own a local `ScopedMock`, or fixtures can derive from `testing::Test` and
own one `ScopedMock` member per mocked dependency. Pass that object directly to
`EXPECT_CALL`. Different mock types can be active together. Two scopes of the
same type cannot overlap in one process.
Dependent objects must be destroyed before their mock scope. GoogleTest runs ordinary
cases sequentially, while CTest may run separate executables in parallel. Any worker
must finish before registration is released.

Use strict mocks for backend boundaries. Default successful error reads may be
shared fixture plumbing; failures and ownership transitions are explicit expectations.
Test-only observers may collect emitted diagnostic entries without simulating a
backend. Avoid expectations for incidental ordering unless that ordering is part
of the contract, such as destroying windows before terminating GLFW.

## Scope and verification

Test Panda behavior and plausible defects, not standard-library behavior in isolation.
Preserve existing useful cases when reorganizing them. Run affected suites and the
quality build; the [quality policy](quality.md) owns analysis and evidence rules.
A mock pass verifies the specified scenario, not compatibility with a real driver.
Integration evidence and unavailable environments are reported separately.
