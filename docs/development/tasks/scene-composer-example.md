# Scene Composer example

Order, dependencies and status: [plan](../PLAN.md).

## Scope

Ship a standalone, interactive C++ example under `examples/scene_composer/`.
Build it against Panda's public Scene, Import and Tools APIs with CUDA disabled.
The application owns its UI flow, file commands and camera controls. A user
can start with an empty scene and complete the workflow through the GUI.

- Import static GLB/glTF models with their materials and embedded or relative
  textures. Keep the current scene intact when import fails and show the error.
- List and select objects and lights. Translate, rotate and scale models using
  transform gizmos. Add directional, point and spot lights from the GUI and
  edit their position, direction, color and intensity. Resize local light
  volume through range and spot cone controls. For directional shadows, expose
  coverage through the corresponding light setting.
- Navigate in first person with WASD and mouse look. Editing a GUI field or
  dragging a gizmo takes precedence over camera input.
- Choose a solid background color or a supplied skybox asset through the GUI.
- Save and reopen scene files. Preserve asset references, model transforms,
  light settings, the active camera and the background selection. Show a clear error for unresolved
  assets or failed saves and preserve the current scene on failed open.

Specifications: [Import](../../design/contracts/scene.md#import-adapter),
[scene persistence](../../design/contracts/scene.md#persistence-import-and-tools),
[scene tools](../../design/contracts/scene.md#optional-tools-and-safe-edits).

## Acceptance

- Build and run the example on Windows and Linux. Document how to start it
  and complete the workflow with a supplied textured model fixture.
- From an empty scene, import the fixture, transform a model in all three
  modes, add and edit each light type, and walk through the result. Verify
  that GUI capture prevents unintended camera motion.
- Save the scene, exit, and reopen the file in a fresh process. Verify the
  model's textures, transforms, lights, camera and skybox selection. Exercise invalid import,
  missing assets on reopen and a failed save with a readable diagnostic.

Learning: Follow a model from its source file through import, an interactive
transform edit, scene save and asset resolution on reopen.
