panda_add_test(panda_integration_cuda integration
    SOURCES CudaAvailability.cpp CudaExternalBuffer.cpp
    LIBRARIES Panda::Cuda)
