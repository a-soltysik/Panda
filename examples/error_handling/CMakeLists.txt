add_executable(error_handling main.cpp)
set_target_properties(error_handling PROPERTIES FOLDER "Panda/Examples"
    UNITY_BUILD "${PANDA_ENABLE_UNITY}")
target_link_libraries(error_handling PRIVATE Panda::Panda)
panda_configure_target(error_handling)
