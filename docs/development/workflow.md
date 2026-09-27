# Working on Panda

## Select and understand the work

The [plan](PLAN.md) owns task order, status and phase approval. When asked to continue,
resume its current task or take the first ready task with accepted prerequisites.
A specific request can select other work within its authorized scope. Reviews are
read-only unless changes are requested. Do not start a later phase without approval.

Read the task, its linked specification sections, the [quality policy](quality.md),
the [project conventions](conventions.md) and relevant code. Before editing, give a brief implementation outline: the result,
invariants, existing patterns, consequential choices and verification. This is an
engineering check, not an extra permission step or a request for the maintainer to
design the solution. Check current upstream documentation when choosing dependency
versions or relying on version-specific API/tool behavior.

## Implement a reviewable result

- Task paths identify the main implementation area; necessary build/test wiring
  and documentation belong to the same change.
- Work on one coherent behavior at a time. A large task may have several implementation
  slices; finish and verify one before expanding. Keep unfinished acceptance items
  visible in that task rather than creating a second roadmap.
- Use one contributor initially. Parallel agents require an explicit request and
  independent interfaces; ownership and synchronization need one integration owner.
- Choose local algorithms and names within established conventions. Explain a
  consequential tradeoff; do not invent frameworks to fill an architectural gap.
- Propose changes to public API/ABI, ownership, synchronization, dependencies outside
  the approved set, platform support or product scope before relying on them.
  Include the reason, a practical alternative and the verification consequences.
- Treat feedback as a defect class: inspect analogous places in the current scope,
  fix the cause, and update its owning rule. Avoid a sequence of isolated patches.

Before handoff, review the entire change against the task's acceptance conditions,
including error paths and the effectiveness of checks. Repeat testing when a change
or unresolved concern warrants it. Do not hand off merely because it compiles.
For a new subsystem or high-risk boundary, review the diff independently of the
author's summary before acceptance.

## Status and evidence

| State | Meaning |
| --- | --- |
| Planned | Defined, but not yet authorized to start |
| Ready | Prerequisites accepted and its phase authorized |
| In progress | Work underway; acceptance items remain |
| Blocked | A named decision or unavailable capability prevents progress |
| Review | Implementation and required evidence are ready for maintainer review |
| Accepted | Maintainer accepted the result and its learning handoff |

The contributor may move a task to Review, but cannot accept it or authorize the
next phase. Report a failed prerequisite before dependent work; do not conceal it
with a workaround. Keep task status only in the plan.

Keep one concise current verification record in the task: commands, relevant
versions/environment, results, unavailable checks and remaining work. Replace stale
results after changes; retain only evidence that still supports the current result.
Keep raw logs and large captures outside source control. Do not invent measurements.
End with a short walkthrough of one invariant or failure case, following
[the learning guide](learning.md).

## Maintain the documentation

Use [the maintenance procedure](../../.agents/skills/panda-project-maintenance/SKILL.md).
Edit the owning section in place. Delete obsolete descriptions, completed planning
rituals, empty decision lists and duplicate explanations; repair incoming links.
Do not turn specifications into release notes, append dated correction histories,
or describe removed files and approaches. Keep useful rationale next to the current
decision. Git history records how the text changed.

Use descriptive task and phase names and ordinary links. Specifications define
behavior; task cards define a bounded delivery and its evidence. Keep API details
near declarations and avoid duplicating the same rule across all three places.
Planned interfaces are clearly labeled; instructions for current use name only
available commands and files.

Keep source docs, small test fixtures and curated benchmark baselines versioned.
Exclude generated HTML, caches, local paths, private notes and raw runs. AI guidance
and maintenance helpers live in `.agents/`, human documentation in `docs/`.
Do not stage or commit without a request.

Run the documentation checker from `AGENTS.md` and inspect semantic consistency.
It validates local links and task structure, not technical correctness. For a small
maintenance script, running the changed script is sufficient; engine tests still
follow the quality policy. Report documentation reconciliation in the handoff.
