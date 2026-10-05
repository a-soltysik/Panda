# Contributor instructions

Read [the plan](docs/development/PLAN.md) and the selected task, then only the
specification sections needed for that task. Use [the workflow](docs/development/workflow.md)
and [quality policy](docs/development/quality.md) throughout implementation.
[The documentation index](docs/README.md) routes other questions.

## Working boundaries

- The build foundation is in progress. Follow the plan's dependencies and phase
  approvals; a documentation request does not authorize engine implementation.
- Preserve unrelated changes. Do not stage, commit, push or rewrite history unless asked.
- Keep reference source and assets in `legacy/` unchanged and outside new targets.
  Removing them needs explicit maintainer approval after the replacement is accepted.
- Public documentation is English. AI guidance belongs in `.agents/`; only this
  entry point stays at the root. Human specifications and tasks belong in `docs/`.

## Implementation standard

Own the complete result within the task's scope. Before editing, briefly identify
its invariant, the existing pattern to reuse, consequential choices and verification.
Choose routine implementation details yourself; ask only for an unresolved decision
that changes the agreed boundary. A small diff still needs sound engineering.

Follow the checked-in conventions. Learn selectively from `legacy`: reuse a sound
idea after checking it against current APIs and the task's requirements. Do not copy
old dependencies, suppressions or synchronization assumptions without review.
After feedback, correct the cause and analogous cases within the task, not only the
reported line. Do not weaken checks to make a result pass.

## Maintenance and handoff

Use [panda-project-maintenance](.agents/skills/panda-project-maintenance/SKILL.md).
For local Windows and WSL builds, follow
[panda-dual-platform-build](.agents/skills/panda-dual-platform-build/SKILL.md).
Documentation describes the current project and intended behavior, not a release
log or conversation history. Replace or delete obsolete text and update its links;
do not append a correction while retaining the stale rule.

Run `python .agents/skills/panda-project-maintenance/scripts/check_docs.py`.
Report actual checks, omissions and remaining issues. Explain the implementation
and check the maintainer's understanding following [the learning guide](docs/development/learning.md).
A passing build is neither maintainer acceptance nor GPU proof.
