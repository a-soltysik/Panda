add_executable(simple_scene main.cpp)
set_target_properties(simple_scene PROPERTIES
    FOLDER "Panda/Examples"
    UNITY_BUILD "${PANDA_ENABLE_UNITY}"
)
target_link_libraries(simple_scene PRIVATE Panda::Panda Panda::Tools)
panda_configure_target(simple_scene)
