add_executable(scene_demo main.cpp)
set_target_properties(scene_demo PROPERTIES
    FOLDER "Panda/Examples"
    UNITY_BUILD "${PANDA_ENABLE_UNITY}")
target_link_libraries(scene_demo PRIVATE Panda::Scene Panda::Tools)
panda_configure_target(scene_demo)
