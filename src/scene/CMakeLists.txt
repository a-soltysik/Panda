panda_add_component(panda_scene
    ALIAS Panda::Scene
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/scene"
    SOURCES Entity.cpp ObjectId.cpp Scene.cpp SceneState.cpp
            SceneRenderViewBuilder.cpp SceneTransformMath.cpp
    PUBLIC_HEADERS panda/scene/Entity.hpp panda/scene/ObjectId.hpp panda/scene/Scene.hpp
    PRIVATE_HEADERS SceneState.hpp SceneRenderViewBuilder.hpp SceneTransformMath.hpp
    LIBRARIES PUBLIC Panda::Panda)
target_link_libraries(panda_scene PRIVATE panda_base_internal panda_core_internal)
