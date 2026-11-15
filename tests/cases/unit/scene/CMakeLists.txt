panda_add_test(panda_unit_scene unit
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/src/scene"
    SOURCES Scene.cpp SceneState.cpp SceneRenderViewBuilder.cpp SceneTransformMath.cpp
    LIBRARIES Panda::Scene)
