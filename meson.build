project(
    'Panda',
    'cpp',
    default_options : ['cpp_std=c++20']
)

message('AA')

vulkan_hpp_inc = include_directories('ext/Vulkan-Hpp')
glm = subproject('glm')
glfw = subproject('glfw')
fmt = subproject('fmt')
vulkan_dep = dependency('Vulkan')

add_project_arguments('-DPD_PROJECT_NAME="Panda"', language : 'cpp')

if get_option('buildtype') == 'debug'
    add_project_arguments(['-DPD_DEBUG=1', '-DPD_RELEASE=0'], language : 'cpp')
elif get_option('buildtype') == 'release'
    add_project_arguments(['-DPD_DEBUG=0', '-DPD_RELEASE=1'], language : 'cpp')
elif get_option('buildtype') == 'debugoptimized'
    add_project_arguments(['-DPD_DEBUG=1', '-DPD_RELEASE=1'], language : 'cpp')
else
    add_project_arguments(['-DPD_DEBUG=1', '-DPD_RELEASE=0'], language : 'cpp')
endif

message(get_option('buildtype'))

subdir('src')