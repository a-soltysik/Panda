---
name: panda-project-maintenance
description: Keep Panda specifications, task state and contributor guidance concise and consistent with an authorized change. Read-only reviews return findings.
---

# Maintain Panda documentation

Read [the plan](../../../docs/development/PLAN.md), the affected task and its relevant
specification sections. Follow [the workflow](../../../docs/development/workflow.md).
This procedure does not authorize engine work or acceptance of a later phase.

## Reconcile the current project

- Identify the owner of each changed requirement, behavior, path or convention.
  Search its other mentions and links before changing it.
- Update the owner in place. Remove stale text and redundant pages; repair callers.
  Documentation is not release notes: no appended correction history, obsolete
  API examples, descriptions of removed mechanisms or empty decision backlogs.
- Keep necessary rationale and current evidence, not a transcript of iterations.
  Use descriptive names and links for tasks, phases and decisions.
- Distinguish intended behavior, implemented behavior, actual verification and
  maintainer acceptance. Update task evidence and plan state together when needed.
- Preserve unrelated work. Reference source/assets in `legacy/` remain unchanged
  unless explicitly authorized; its short source notes are enough for ordinary use.
- For read-only requests, report corrections without editing. Do not manufacture
  documentation churn when a change has no documentation impact.

## Verify and hand off

Run `python .agents/skills/panda-project-maintenance/scripts/check_docs.py`.
If this helper changes, execute it; it needs no separate test framework.
Manually check for contradictions, dropped requirements, stale links and unsupported
claims. The checker cannot establish technical truth or maintainer approval.

Report what documentation was consolidated or removed, actual checks and remaining
issues. Public documentation is English. Keep maintenance helpers with this skill.
