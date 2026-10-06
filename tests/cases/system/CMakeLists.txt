panda_add_test(panda_system_application_startup system
    SOURCES ApplicationStartup.cpp
    LIBRARIES Panda::Tools PandaTest::Support glfw)

panda_add_test(panda_system_vulkan_context system
    SOURCES VulkanContext.cpp
    LIBRARIES Panda::Panda Vulkan::Headers)

panda_add_test(panda_system_windowed_context system
    SOURCES WindowedContext.cpp
    LIBRARIES Panda::Tools PandaTest::Support glfw)
