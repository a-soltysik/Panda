# Use cases and implementation flows

These documents connect application goals with Panda's implementation. Read the
positive path first, then follow the variants table to failures and cleanup.
Class and method names identify main participants; diagrams omit incidental
forwarding and helper calculations.

| Flow | Boundary |
| --- | --- |
| [Create a buffer](create-buffer.md) | Worked example: positive sequence, ownership and binding-failure cleanup for an ordinary buffer |

For authors, see the [conventions](../development/functional-documentation.md) and
[scenario template](template.md). The template is an authoring aid, not an
implemented feature. Shared guarantees remain in the
[resource contract](../design/contracts/resources.md) and other design contracts
listed in the [documentation index](../README.md).
