panda_add_component(panda_core
    ALIAS Panda::Panda
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/core"
    SOURCES
        Panda.cpp
        Error.cpp
        Projection.cpp
        Cube.cpp
        Buffer.cpp
        Mesh.cpp
        Texture.cpp
        vulkan/external/ExternalBuffer.cpp
        vulkan/external/ExternalBufferManager.cpp
        vulkan/external/ExternalBufferOwnershipTransferCommand.cpp
        vulkan/external/ExternalMemoryHandle.cpp
        vulkan/external/ExternalSemaphoreHandle.cpp
        vulkan/external/ExternalSemaphoreResource.cpp
        CompletionPoint.cpp
        UploadBatch.cpp
        vulkan/Context.cpp
        vulkan/DeviceSelection.cpp
        vulkan/external/ExternalContext.cpp
        vulkan/performance/PerformanceManager.cpp
        vulkan/performance/PerformanceManagerImpl.cpp
        vulkan/presentation/Presentation.cpp
        vulkan/renderer/MeshRenderer.cpp
        vulkan/renderer/RenderViewValidation.cpp
        vulkan/SubmissionQueue.cpp
        vulkan/upload/UploadBatchImpl.cpp
        vulkan/upload/UploadManager.cpp
        vulkan/upload/UploadCommand.cpp
        vulkan/presentation/SwapchainGeneration.cpp
        vulkan/VulkanError.cpp
        vulkan/resources/memory/RangeAllocator.cpp
        vulkan/resources/memory/MemoryBlock.cpp
        vulkan/resources/memory/MemoryAllocation.cpp
        vulkan/resources/memory/MemoryPool.cpp
        vulkan/resources/BufferResource.cpp
        vulkan/resources/MeshLayout.cpp
        vulkan/resources/MeshResource.cpp
        vulkan/resources/TextureResource.cpp
    PUBLIC_HEADERS
        panda/Buffer.hpp
        panda/CompletionPoint.hpp
        panda/Context.hpp
        panda/Error.hpp
        panda/ExternalBuffer.hpp
        panda/ExternalBufferManager.hpp
        panda/ExternalMemoryHandle.hpp
        panda/ExternalSemaphoreHandle.hpp
        panda/ExternalTimelineValue.hpp
        panda/StrongAlias.hpp
        panda/Mesh.hpp
        panda/Panda.hpp
        panda/PerformanceManager.hpp
        panda/Projection.hpp
        panda/Cube.hpp
        panda/Geometry.hpp
        panda/Texture.hpp
        panda/UploadBatch.hpp
        panda/WindowSurface.hpp
        panda/RenderView.hpp
    PRIVATE_HEADERS
        vulkan/DeviceSelection.hpp
        vulkan/external/ExternalContext.hpp
        vulkan/external/ExternalSemaphoreResource.hpp
        vulkan/external/ExternalBufferOwnershipTransferCommand.hpp
        vulkan/external/ScopedExternalHandle.hpp
        vulkan/performance/PerformanceManagerImpl.hpp
        vulkan/presentation/Presentation.hpp
        vulkan/SubmissionQueue.hpp
        vulkan/presentation/SwapchainGeneration.hpp
        vulkan/upload/UploadBatchId.hpp
        vulkan/upload/UploadBatchImpl.hpp
        vulkan/upload/UploadCommand.hpp
        vulkan/upload/UploadManager.hpp
        vulkan/upload/UploadOperation.hpp
        vulkan/VulkanError.hpp
        vulkan/VulkanHpp.hpp
        vulkan/resources/BufferResource.hpp
        vulkan/resources/memory/MemoryAllocation.hpp
        vulkan/resources/memory/MemoryBlock.hpp
        vulkan/resources/memory/MemoryPool.hpp
        vulkan/resources/memory/MemoryProperties.hpp
        vulkan/resources/MeshLayout.hpp
        vulkan/resources/MeshResource.hpp
        vulkan/resources/memory/RangeAllocator.hpp
        vulkan/resources/TextureResource.hpp
        vulkan/renderer/MeshRenderer.hpp
        vulkan/renderer/RenderViewValidation.hpp
    LIBRARIES PUBLIC panda_common Vulkan::Headers glm::glm PRIVATE Vulkan::Vulkan)

find_program(PANDA_GLSLC_EXECUTABLE NAMES glslc REQUIRED)
find_program(PANDA_SPIRV_VAL_EXECUTABLE NAMES spirv-val REQUIRED)

set(PANDA_BUILTIN_SHADER_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(PANDA_MESH_VERTEX_SPIRV "${PANDA_BUILTIN_SHADER_DIRECTORY}/mesh.vert.spv")
set(PANDA_MESH_FRAGMENT_SPIRV "${PANDA_BUILTIN_SHADER_DIRECTORY}/mesh.frag.spv")
set(PANDA_BUILTIN_SHADER_HEADER "${PANDA_BUILTIN_SHADER_DIRECTORY}/BuiltinShaders.hpp")
add_custom_command(
    OUTPUT "${PANDA_BUILTIN_SHADER_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PANDA_BUILTIN_SHADER_DIRECTORY}"
    COMMAND "${PANDA_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 "${PROJECT_SOURCE_DIR}/shaders/builtin/mesh.vert"
            -o "${PANDA_MESH_VERTEX_SPIRV}"
    COMMAND "${PANDA_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 "${PROJECT_SOURCE_DIR}/shaders/builtin/mesh.frag"
            -o "${PANDA_MESH_FRAGMENT_SPIRV}"
    COMMAND "${PANDA_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.3 "${PANDA_MESH_VERTEX_SPIRV}"
    COMMAND "${PANDA_SPIRV_VAL_EXECUTABLE}" --target-env vulkan1.3 "${PANDA_MESH_FRAGMENT_SPIRV}"
    COMMAND "${CMAKE_COMMAND}"
            "-DOUTPUT_FILE=${PANDA_BUILTIN_SHADER_HEADER}"
            "-DVERTEX_SPIRV=${PANDA_MESH_VERTEX_SPIRV}"
            "-DFRAGMENT_SPIRV=${PANDA_MESH_FRAGMENT_SPIRV}"
            -P "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/shaders/builtin/mesh.vert"
        "${PROJECT_SOURCE_DIR}/shaders/builtin/mesh.frag"
        "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
    VERBATIM)
target_sources(panda_core PRIVATE "${PANDA_BUILTIN_SHADER_HEADER}")
target_include_directories(panda_core PRIVATE "${PANDA_BUILTIN_SHADER_DIRECTORY}")

set_target_properties(panda_core PROPERTIES OUTPUT_NAME panda)
target_compile_definitions(panda_core PRIVATE PANDA_VERSION_TEXT="${PROJECT_VERSION}")
panda_configure_vulkan_internal(panda_core)
