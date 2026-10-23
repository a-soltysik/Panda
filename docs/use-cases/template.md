# Scenario template

Copy the skeleton below into a descriptively named page and replace its instructions.
Follow the [conventions](../development/functional-documentation.md) and compare
the worked example [UC Create a buffer](create-buffer.md). The diagram's participants and calls are
placeholders, not Panda API. Adapt step counts to the code. Optional state, decision
or ownership diagrams belong beside the behavior they explain.
Keep diagram notation guidance in the conventions. Do not add explanations of
arrows, `opt` blocks, visible calls or omitted helpers below a finished diagram.

````markdown
# Verb and goal

## Goal and boundary

Explain the caller's goal, trigger, start/end points and responsibility boundary.
State implemented or planned scope; link the relevant task and shared contracts.
Bold the responsible parties, such as **Panda** and **The application**, where
assigning responsibilities. Put exact code symbols in `monospace`.

## Main participants (optional)

Add a compact role map only when it clarifies responsibilities beyond the goal,
flow and diagram. Remove this section when it would repeat those descriptions.

| Participant | Responsibility |
| --- | --- |
| Caller | Triggers the flow or supplies its input |
| Main collaborator | Performs a consequential operation or owns relevant state |

## Preconditions (optional)

Include only short statements of state that must already hold before this use
case starts. Number the points when the section applies. Do not list
inputs or capabilities that the flow checks; describe those in the relevant step
and variant. Remove this section when there are no true preconditions.

1. **Parent object.** A live `Context` exists before creating its buffer.

## Main flow

1. **Request.** Identify the entry operation and its input.
2. **Prepare.** Identify affected data/resources, their owner, the operation's
   effect and why this step is needed. Do not rely on the method name to explain it.
3. **Publish.** Explain the observable result and publication point.

```mermaid
sequenceDiagram
    participant A as Caller
    participant F as Facade / Impl
    participant S as Main collaborator
    A->>F: Step 1 - entryOperation(input)
    F->>S: Step 2 - prepare(input)
    S-->>F: Prepared result
    F-->>A: Step 3 - Published result
```

## Notes

**Ownership.** Explain who owns or borrows important objects and what must outlive them.

**Publication guarantee.** Identify the *publication point* and what must succeed
before the result becomes visible. Adapt note labels to the actual invariant or
non-obvious decision. Refer to another use case with a link labeled `UC <title>`
and summarize its required outcome here. Use the same name in a sequence note and
explain facts the diagram does not already communicate.
Use emphasis sparingly; do not bold whole explanations.

## Variants and errors

|  | Point and condition | Response | Resulting state and next action |
| --- | --- | --- | --- |
| **Error&nbsp;1** | Step 2: preparation fails | Explain returned error and cleanup; link a substantial failure sequence | State what remains valid and whether retry is allowed |

### Error 1 - Preparation cleanup

Include only for substantial handling. State completed steps, existing resources
and submitted work. Add a separate Mermaid sequence from divergence through cleanup
to the result. Remove this section for a simple return.

## Outcomes

State success guarantees and remaining caller obligations as unnumbered
alternatives (for example, **Success** and **Failure**). For GPU operations,
distinguish submission from completion. Summarize failure guarantees without
promising rollback beyond the implementation.

## Related information

Link owning contract sections, related flows and specific upstream documentation
that explains the mechanism, such as Vulkan memory binding or CUDA synchronization.
Briefly state what each reference explains. Keep test lists and execution evidence
in development documentation rather than this scenario.
````
