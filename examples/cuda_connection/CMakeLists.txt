set(CMAKE_CUDA_HOST_COMPILER "${CMAKE_CXX_COMPILER}")
if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES AND NOT DEFINED ENV{CUDAARCHS})
    set(CMAKE_CUDA_ARCHITECTURES native)
endif()
enable_language(CUDA)

add_library(cuda_connection_kernel STATIC Kernel.cu ProducerBoundary.hpp)
set_target_properties(cuda_connection_kernel PROPERTIES
    FOLDER "Panda/Examples"
    CUDA_STANDARD 20 CUDA_STANDARD_REQUIRED ON CUDA_EXTENSIONS OFF
    CUDA_RUNTIME_LIBRARY Static
    COMPILE_WARNING_AS_ERROR "${PANDA_WARNINGS_AS_ERRORS}")
target_link_libraries(cuda_connection_kernel PRIVATE CUDA::cudart_static)
panda_enable_static_analysis(cuda_connection_kernel)
panda_enable_format_check(cuda_connection_kernel)

add_library(cuda_connection_producer STATIC Producer.cpp ProducerBoundary.hpp)
set_target_properties(cuda_connection_producer PROPERTIES
    FOLDER "Panda/Examples"
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
target_link_libraries(cuda_connection_producer PRIVATE
    cuda_connection_kernel CUDA::cudart_static)
panda_configure_target(cuda_connection_producer)

add_executable(cuda_connection main.cpp ProducerBoundary.hpp)
set_target_properties(cuda_connection PROPERTIES FOLDER "Panda/Examples")
target_link_libraries(cuda_connection PRIVATE Panda::Cuda cuda_connection_producer)
panda_configure_target(cuda_connection)
