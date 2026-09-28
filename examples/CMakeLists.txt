add_subdirectory(simple_scene)
if(PANDA_BUILD_CUDA)
    add_subdirectory(cuda_connection)
endif()
