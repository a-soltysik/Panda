add_executable(buffer_allocation main.cpp)
set_target_properties(buffer_allocation PROPERTIES FOLDER "Panda/Examples"
    UNITY_BUILD "${PANDA_ENABLE_UNITY}")
target_link_libraries(buffer_allocation PRIVATE Panda::Panda)
panda_configure_target(buffer_allocation)
