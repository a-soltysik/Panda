add_executable(simple_scene main.cpp)
set_target_properties(simple_scene PROPERTIES FOLDER "Panda/Examples")
target_link_libraries(simple_scene PRIVATE Panda::Panda Panda::Tools)
set_target_properties(simple_scene PROPERTIES
        UNITY_BUILD "${PANDA_ENABLE_UNITY}"
)
panda_configure_target(simple_scene)
