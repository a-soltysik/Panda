panda_add_test(panda_unit_cuda unit
        SOURCES CudaAdapter.cpp
        LIBRARIES panda_cuda_adapter CUDA::cudart_static
        MOCKS PandaTest::CudaRuntimeMock)
target_include_directories(panda_unit_cuda PRIVATE "${PROJECT_SOURCE_DIR}/src/cuda")
