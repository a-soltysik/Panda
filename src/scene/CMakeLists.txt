panda_add_component(panda_scene
    ALIAS Panda::Scene
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/scene"
    SOURCES Entity.cpp ObjectId.cpp Scene.cpp SceneState.cpp
            SceneRenderViewBuilder.cpp SceneTransformMath.cpp
    PUBLIC_HEADERS panda/scene/Entity.hpp panda/scene/ObjectId.hpp panda/scene/Scene.hpp
    PRIVATE_HEADERS SceneState.hpp SceneRenderViewBuilder.hpp SceneTransformMath.hpp
    LIBRARIES PUBLIC Panda::Panda glm::glm)
target_include_directories(panda_scene PRIVATE "${PROJECT_SOURCE_DIR}/src/core" "${PROJECT_SOURCE_DIR}/src/common")
