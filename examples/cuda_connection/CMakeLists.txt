set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}")
enable_language(CUDA)

add_library(cuda_connection_kernel STATIC Kernel.cu)
set_target_properties(cuda_connection_kernel PROPERTIES
    FOLDER "Panda/Examples"
    CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON CUDA_EXTENSIONS OFF
    CUDA_ARCHITECTURES native CUDA_RUNTIME_LIBRARY Static)
target_link_libraries(cuda_connection_kernel PRIVATE CUDA::cudart_static)
panda_enable_format_check(cuda_connection_kernel Kernel.cu)

add_library(cuda_connection_producer STATIC Producer.cpp)
set_target_properties(cuda_connection_producer PROPERTIES
    FOLDER "Panda/Examples"
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(cuda_connection_producer PRIVATE
    cuda_connection_kernel CUDA::cudart_static)
panda_configure_target(cuda_connection_producer ProducerBoundary.hpp)

add_executable(cuda_connection main.cpp)
set_target_properties(cuda_connection PROPERTIES FOLDER "Panda/Examples")
target_link_libraries(cuda_connection PRIVATE Panda::Cuda cuda_connection_producer)
panda_configure_target(cuda_connection)
