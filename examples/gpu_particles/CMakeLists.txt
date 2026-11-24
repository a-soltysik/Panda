add_executable(gpu_particles main.cpp)
set_target_properties(gpu_particles PROPERTIES
    FOLDER "Panda/Examples"
    UNITY_BUILD "${PANDA_ENABLE_UNITY}"
)
target_link_libraries(gpu_particles PRIVATE Panda::Tools)
panda_configure_target(gpu_particles)
panda_add_shaders(gpu_particles
    NAMESPACE demo::particle_shader
    SOURCES particle.vert particle.frag
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
