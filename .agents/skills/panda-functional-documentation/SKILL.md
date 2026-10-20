---
name: panda-functional-documentation
description: Create, review or update Panda use cases and Mermaid implementation flows from code, including documentation affected by behavior or collaboration changes. Use for functional scenarios and subflows, not declaration-only API comments.
---

# Document Panda behavior through code

Read the [conventions](../../../docs/development/functional-documentation.md),
[scenario index](../../../docs/use-cases/README.md) and relevant contract/task.
Use the [template](../../../docs/use-cases/template.md) and
[worked example](../../../docs/use-cases/create-buffer.md) when writing a scenario.
The conventions own the format and extraction criteria; do not redefine them here.

## Choose the document

Search existing goals, symbols and related flows before creating a page. Update an
existing owner when its boundary matches. Add a UC for a distinct goal within the
authorized task, or extract a subflow under the conventions' criteria. Changes to
depicted collaboration require updating its realization even without API changes.
Do not backfill unrelated subsystems or create one UC per method. Read-only reviews
return findings; documentation work does not authorize engine implementation,
phase advancement, commit or publication.

## Establish the actual path

Trace the entry operation through main collaborators, including private implementation
and RAII destructors where they determine ownership or cleanup. Inspect error branches,
publication, retained owners, submission and completion separately. Inspect representative
test bodies, not just names; compare contracts and task evidence. Do not treat mocks
as hardware proof or tests present in source as tests run in this session.

Use current class/method names to orient the reader, but omit source filenames/line
numbers from scenarios. Collapse forwarding and arithmetic helpers by their meaning,
not visibility or naming. Mark summarized actions rather than inventing methods.
Treat existing docs as claims to check, not substitutes for tracing code. Report
contract/code contradictions; do not silently change a contract to match a defect.
Separate unimplemented designs and do not invent their realization.

For a documentation coverage audit, inventory implemented goals and meaningful
shared subflows, not individual methods. Identify existing coverage, the missing
explanation, priority and parent/caller relationships. Keep planned functionality
separate from current documentation debt. An inventory request does not authorize
writing the listed scenarios; use the requested local Markdown location for the
backlog and leave implementation evidence there rather than in finished scenarios.

## Write and reconcile

Write the positive path with matching step labels in text and Mermaid. Connect notes
to reasons behind implementation, not diagram notation or a repetition of visible
calls and conditions. Put alternatives in the variants table; give
substantial cleanup its own sequence with explicit entry and exit state. Link shared
rules and extracted flows instead of duplicating them. Keep test inventories and
execution evidence out of the scenario; related information links contracts, flows
and relevant upstream sections. Add the page to the index and repair callers after
moves or removals.
Apply the conventions' review questions before handoff, including to the worked
example; existing examples do not override the conventions.

Apply [project maintenance](../panda-project-maintenance/SKILL.md), including its
documentation checker. Run the Mermaid syntax checker using the conventions'
local commands after changing diagrams or the checker. Review messages against code
and render when available; syntax acceptance does not establish readable layout.
Report checks, rendering omissions, unresolved mismatches and the learning handoff
without claiming maintainer acceptance.
