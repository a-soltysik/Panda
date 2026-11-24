add_executable(custom_shaders main.cpp)
set_target_properties(custom_shaders PROPERTIES
    FOLDER "Panda/Examples"
    UNITY_BUILD "${PANDA_ENABLE_UNITY}"
)
target_link_libraries(custom_shaders PRIVATE Panda::Tools)
panda_configure_target(custom_shaders)
panda_add_shaders(custom_shaders
    NAMESPACE demo::shader
    SOURCES gyroid.vert gyroid.frag)
