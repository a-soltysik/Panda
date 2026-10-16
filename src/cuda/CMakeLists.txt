add_library(panda_cuda_adapter STATIC CudaAdapter.cpp CudaAdapter.hpp)
set_target_properties(panda_cuda_adapter PROPERTIES
    FOLDER "Panda/Libraries"
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF
    UNITY_BUILD "${PANDA_ENABLE_UNITY}")
target_link_libraries(panda_cuda_adapter PRIVATE CUDA::cudart_static)
panda_configure_target(panda_cuda_adapter)

panda_add_component(panda_cuda
    ALIAS Panda::Cuda
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/cuda"
    SOURCES Cuda.cpp
    PUBLIC_HEADERS panda/cuda/Cuda.hpp
    PRIVATE_HEADERS CudaAdapter.hpp
    LIBRARIES PUBLIC Panda::Panda PRIVATE panda_cuda_adapter)
