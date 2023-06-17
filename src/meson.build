target_name = 'Panda.Engine'

glm_dep = glm.get_variable('glm_dep').as_system()
glfw_dep = glfw.get_variable('glfw_dep')
fmt_dep = fmt.get_variable('fmt_dep')

app_src = [
    'app' / 'App.cpp',
    'app' / 'Logger.cpp',
    'app' / 'Window.cpp'
]

gfx_src = [
    'gfx' / 'api' / 'vulkan' / 'Buffer.cpp',
    'gfx' / 'api' / 'vulkan' / 'Device.cpp',
    'gfx' / 'api' / 'vulkan' / 'Model.cpp',
    'gfx' / 'api' / 'vulkan' / 'Object.cpp',
    'gfx' / 'api' / 'vulkan' / 'Pipeline.cpp',
    'gfx' / 'api' / 'vulkan' / 'Renderer.cpp',
    'gfx' / 'api' / 'vulkan' / 'RenderSystem.cpp',
    'gfx' / 'api' / 'vulkan' / 'Shader.cpp',
    'gfx' / 'api' / 'vulkan' / 'SwapChain.cpp',
    'gfx' / 'api' / 'vulkan' / 'Vulkan.cpp'
]

utils_src = [

]

add_project_arguments('-DPD_TARGET_NAME="@0@"'.format(target_name), language : 'cpp')
add_project_arguments('-DVULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1', language : 'cpp')
add_project_arguments('-DVULKAN_HPP_NO_EXCEPTIONS', language : 'cpp')
add_project_arguments('-DVULKAN_HPP_ASSERT_ON_RESULT=EXPECT', language : 'cpp')
add_project_arguments('-DVULKAN_HPP_ASSERT=DEBUG_EXPECT', language : 'cpp')

executable(
    target_name,
    ['main.cpp', app_src, gfx_src, utils_src],
    dependencies: [vulkan_dep, glm_dep, glfw_dep, fmt_dep],
    cpp_pch : 'pch' / 'pch.h',
    override_options : ['cpp_std=c++20']
)