add_executable(custom_shader_shadows main.cpp)
set_target_properties(custom_shader_shadows PROPERTIES
    FOLDER "Panda/Examples"
    UNITY_BUILD "${PANDA_ENABLE_UNITY}"
)
target_link_libraries(custom_shader_shadows PRIVATE Panda::Tools)
panda_configure_target(custom_shader_shadows)
panda_add_shaders(custom_shader_shadows
    NAMESPACE demo::shadow_shader
    SOURCES object.vert object.frag shadow.vert shadow.frag)
