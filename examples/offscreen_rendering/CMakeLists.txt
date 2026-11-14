add_executable(offscreen_rendering main.cpp)
set_target_properties(offscreen_rendering PROPERTIES
    FOLDER "Panda/Examples"
    UNITY_BUILD "${PANDA_ENABLE_UNITY}"
)
target_link_libraries(offscreen_rendering PRIVATE Panda::Panda)
panda_configure_target(offscreen_rendering)
