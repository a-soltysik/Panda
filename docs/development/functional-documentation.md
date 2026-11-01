# Functional documentation conventions

Functional documentation helps a C++ programmer use, read, debug and extend Panda.
Explain behavior and its realization through the main classes and important methods.
Add short notes about intent, ownership, ordering and failure guarantees that the
reader would otherwise have to infer from code.

## Responsibilities and location

| Document | Owns |
| --- | --- |
| Design contracts | Shared behavioral rules and invariants, including intended behavior |
| Use cases and subflows | Goals, paths, outcomes and their realization in code |
| Public API comments | Exact declaration-level obligations and results |
| Runnable examples | Current application usage |
| Task cards and plan | Delivery evidence, task status and acceptance |

A contract is an authoritative rule, such as Context outliving its resources or
submission not implying completion. Link its owning section from the relevant step.
Explain the local consequence without copying the full rule. If a flow description
moves from a contract to a scenario, replace it with a link and retain shared rules
in the contract. Resolve contradictions rather than presenting both as valid.

Keep English scenarios under `docs/use-cases/`, listed in its
[index](../use-cases/README.md). Use descriptive kebab-case filenames and ordinary
relative Markdown links. Identify implementation classes and methods by symbol,
not source filenames or line numbers. AI procedures belong in `.agents/`.

## Boundaries and subflows

A use case has a goal, trigger, boundary and recognizable outcome. Its caller may
be an application; an internal event can also trigger a meaningful flow. It need
not start with a human action or GPU signal. A scenario is a particular path,
including a failure path. A subflow explains part of a larger operation separately;
buffer creation can be both an application goal and a reusable subflow.

Search existing scenarios before adding one. Update an existing owner when its
goal and boundary match. Add a UC when authorized work introduces a distinct goal
that needs explanation; do not create one page per method or backfill unrelated work.

Extract a subflow when either:

1. At least two documented flows need the same explanation; or
2. It has its own named outcome and substantial branching or cleanup that obscures
   the parent diagram's purpose.

In either case it must have identifiable inputs, outcomes and failure guarantees.
Otherwise keep it as a step. Reuse does not justify extracting a trivial sentence.
Reassess this boundary during implementation: when another feature starts using
an already documented operation, extract its explanation if these criteria now
apply, then link both callers to it in the same change.
There is no fixed arrow count: the second criterion requires a readability judgment;
explain a consequential choice briefly. Link from the parent and state what result
it needs. Do not duplicate the extracted sequence or create circular decompositions.

## Scenario content

Start from the [template](../use-cases/template.md) and compare the
[worked example](../use-cases/create-buffer.md).

- State goal, trigger, start/end boundary and application/engine responsibilities.
- Identify implemented or planned scope. Planned behavior must not appear as
  available API; separate it from current realization. Link the relevant task
  rather than copying its status or verification log.
- Main participants are optional. Add a `Main participants` table only when a
  compact role map clarifies responsibilities beyond the goal, flow and diagram.
  Omit it when it repeats information already clear there.
- Preconditions are optional. Include only short statements of state that must
  already hold before the use case starts, such as a live `Context` before buffer
  creation. If present, use a numbered list. Put checked inputs and capabilities
  evaluated during the flow in its steps and variants; put later lifetime duties
  with ownership or outcomes.
- Number main-flow steps `1.`, `2.`, etc. without step prefixes in their labels.
  Refer to a step as `Step 1` in prose and variants, and label corresponding
  diagram messages `Step 1`, `Step 2`, etc. Include a positive sequence for the
  realization.
- Explain outcomes, publication, ownership and remaining duties. For asynchronous
  work distinguish accepted, submitted and completed work and observable completion.
  Present outcome alternatives with bold labels or unnumbered bullets, not as an
  ordered list.
- Use short notes to explain invariants and decisions, not narrate trivial code.
  Explain what an operation acts on, who owns that state and why the step is needed.
  A method name such as `collect()` does not explain which resources are released
  or what makes that release safe; make the effect and its condition explicit.
  Do not append prose explaining Mermaid notation, repeating visible calls or
  conditions, or listing omitted helpers. Notes must add a reason, guarantee or
  consequence that the diagram itself does not already communicate.
  Describe cleanup guarantees and responsibility boundaries rather than local
  mechanisms such as member declaration order. Keep such mechanics in code comments
  unless they are necessary to understand the scenario's flow.
- End with related information: owning contract sections, related scenarios and
  relevant upstream documentation, such as specific Vulkan or CUDA sections.
  Explain what each reference helps the reader understand. Keep test inventories,
  commands and verification results out of scenarios; evidence belongs in task cards.

Combine or omit inapplicable optional sections. Remove template instructions from
finished pages. Keep the smallest complete explanation of the selected boundary. Describe
application behavior generically; keep choices specific to a runnable example in
that example's README. Related information may link to an example as an illustration.

## Text emphasis

Use formatting by meaning so readers can scan a scenario and locate symbols in code.
Follow the [project text and markup conventions](conventions.md#text-and-markup).

| Format | Use | Example |
| --- | --- | --- |
| **Bold** | Responsible party, key guarantee or important restriction | **Panda** owns native creation; initial contents are **undefined** |
| `Monospace` | Exact code symbols: types, methods, parameters and enum values | `MemoryPool`, `Context::createBuffer()`, `BufferMemory::Upload` |
| *Italics* | A conceptual term when first introduced | The *publication point* is the moment a prepared result becomes visible |
| Plain text | Explanations and ordinary domain words | A memory pool can serve several resources |

Begin numbered steps with a short bold action label, such as **Allocate memory.**
Begin notes with their main point, such as **Ownership** or **Failure guarantee**.
Use **Error&nbsp;1** to identify an error case and **Variant&nbsp;1** for an ordinary
alternative. Number each kind separately. *Failure* describes an unsuccessful path
or outcome; it is not a synonym for a returned `Error`. Leave the first table-header
cell blank; the complete identifier in each row is sufficient. Use monospace for
error codes.
Keep explanations in ordinary prose with code symbols in monospace. Do not bold
whole paragraphs, every class name or every occurrence of an important word.
Do not combine bold and monospace for routine symbols. Keep Mermaid labels plain;
Markdown formatting in surrounding prose need not be reproduced inside diagrams.

## Diagrams

Use fenced `mermaid` blocks in Markdown, with syntax supported by GitHub's renderer.
A newer local renderer alone does not establish GitHub compatibility. Place ordinary
Markdown links beside diagrams for navigation. Label a link to another use case as
`UC <title>` and use that same name in a sequence note when the diagram delegates
part of its path. PlantUML's `ref over` is not Mermaid syntax. A documentation
website is optional and must use the same sources rather than a second content copy.
Keep references to local steps as plain `Step 5` labels. Link to an ordinary
section heading when navigation is useful; do not insert HTML anchors for each step.

| Diagram | Useful question |
| --- | --- |
| Sequence | Which main objects call each other and exchange data, in what order? |
| State | Which lifecycle transitions, guards and fault states are allowed? |
| Activity/flowchart | Which decisions determine the path? |
| Ownership | Who owns, borrows or retains an object until completion? |
| Component | Where are project/module responsibility boundaries? |

The positive sequence is the default. Add other types when they contribute
information; there is no mandatory set. Show main participants, not the full stack.
Include a participant when omitting it would hide important ownership, data flow,
state, ordering or cleanup. Omit arithmetic and formatting helpers; explain an
important calculation in a note or algorithm description. Collapse forwarding
facades with their `Impl` unless the split explains a meaningful responsibility or
lifetime boundary. Use descriptive labels for summarized actions rather than
inventing method names. Keep notation guidance here, not in individual scenarios.

Use this legend consistently:

- `->>`: synchronous host call; `-->>`: return/result.
- `-)`: asynchronous work enqueue; its return is not GPU completion.
- Label waits, completion signals and asynchronous notifications explicitly;
  vertical position alone must not imply cross-device synchronization.
- Label ownership transfer, borrowing and retention in messages or notes. Ownership
  diagrams use explicit relationship labels rather than unexplained arrows.

Keep the main diagram focused on successful execution. Show calls to collaborating
participants rather than replacing their interaction solely with a note. Use `opt`
for an optional collaboration, such as calling a service only when it exists. A
short `alt` may show mutually exclusive successful paths when needed to explain
the collaboration. Substantial alternatives and failures belong in the variants
table and separate sequences; do not mix error handling into the main diagram.

## Variants and errors

For each meaningful error case or ordinary alternative give a distinct identifier
(`Error 1`, `Error 2` for error cases; `Variant 1`, `Variant 2` for ordinary
alternatives), the main step, condition, response, resulting state and permitted
next action. Number errors and variants separately. Keep outcomes such as skipped
frames as variants when they describe a normal alternative. Use *failure* for an
unsuccessful path or outcome, not as a synonym for an error case.
Distinguish recoverable results, fatal contract violations and asynchronous faults.
Do not imply that every error permits retry or restores the starting state.

A simple return needs only a table entry. Substantial cleanup, recovery or state
transitions need a separate sequence with a stable heading linked from the table.
Start at divergence: state completed steps, existing resources and submitted work.
End with the resulting state; do not replay the successful prefix. Share cleanup
only where entry states and guarantees match. Show RAII cleanup when its order
matters, even without an explicit cleanup call in the operation body.

## Maintenance and review

Update affected scenarios in the same change as behavior or depicted implementation.
A refactor changing a shown symbol, responsibility, ownership or data flow requires
updating the realization even when public behavior is unchanged. A helper change
outside the diagram's abstraction level normally does not.

Before handing off a scenario, review these questions against the implementation:

- Can a reader follow the caller's goal through the main classes and important
  methods without reconstructing every helper call?
- Does each participant take part in a meaningful interaction? If its involvement
  is conditional, is that condition and its actual call shown?
- Do summarized actions name the affected data, its owner and the observable
  effect, rather than relying on vague verbs such as "collect" or "prepare"?
- Do notes explain a reason or guarantee beyond what is already visible, without
  explaining diagram notation or local member-layout mechanics?
- Do step labels agree across prose, messages and variant entries? Does each
  failure identify what remains valid and the permitted next action?
- Are substantial recovery paths separate from the positive sequence, with their
  own entry state and outcome? Are submission and completion kept distinct?
- Do shared explanations have one owner, and do references lead to the relevant
  contract, subflow or upstream section instead of a test inventory?

Check agreement between text, labels, diagrams, contracts and code, especially
publication and failure boundaries. The Quality workflow's independent
`Mermaid syntax` job builds the same CMake target used locally. With CMake, Ninja,
Node.js 22 or newer and npm installed, run on Windows or Linux:

```sh
cmake --preset docs-checks
cmake --build --preset docs-checks
```

The preset selects `PANDA_DOCS_ONLY`, which configures no compiler or engine
dependencies. Its `panda_check_diagrams` target installs the pinned
`@mermaid-lint/cli` package into the build directory on first use or lockfile changes. npm downloads
use npm's default cache; the first run needs network access. CI caches it through
`actions/setup-node`. Ordinary
engine builds require neither Node.js nor npm. To add the target to an engine build,
explicitly enable `PANDA_BUILD_DOC_CHECKS`.

The linter package is pinned by the manifest and lockfile in `tools/documentation/`.
This is documentation tooling, not an engine or consumer dependency. CMake
defines the file scope and invokes the ready-made CLI with `--no-semantic` to check
syntax without adding style or semantic rules. The target uses `npm exec` to run
the installed `mermaid-lint` command in offline mode, without depending on the
package's internal file layout or installing another tool during the check. It
scans maintained Markdown under `docs/`, `.agents/skills/`, `.github/`, `examples/`
and `benchmarks/`, plus root README and AGENTS. It includes Mermaid blocks nested
inside fenced Markdown templates, reports source locations, rejects unclosed
Mermaid fences and reports the number of diagrams found. Generated output, dependencies
and legacy source are excluded.

The selected linter is [mermaid-lint](https://github.com/jasonworden/mermaid-lint),
version 0.53.1, under its upstream MIT license. It handles extraction, syntax
validation and diagnostics; Panda maintains no custom Mermaid-checking script.
This version also checks Mermaid fences nested inside other fenced code examples,
not only Markdown templates. Keep such embedded diagrams syntactically valid.

Syntax validation does not establish readable layout, GitHub renderer compatibility
or technical truth. Render Mermaid when a renderer is available and report omitted
rendering explicitly. The Python documentation checker validates links and task
structure only. Follow the
[maintenance workflow](workflow.md#maintain-the-documentation) and
[learning guide](learning.md): readers should trace a normal path, explain its
invariant and follow a representative failure in the implementation.
