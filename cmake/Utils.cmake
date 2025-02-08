# find a subtring from a string by a given prefix such as VCVARSALL_ENV_START
function(
    find_substring_by_prefix
    output
    prefix
    input)
    # find the prefix
    string(FIND "${input}" "${prefix}" prefix_index)
    if("${prefix_index}" STREQUAL "-1")
        message(SEND_ERROR "Could not find ${prefix} in ${input}")
    endif()
    # find the start index
    string(LENGTH "${prefix}" prefix_length)
    math(EXPR start_index "${prefix_index} + ${prefix_length}")

    string(
        SUBSTRING "${input}"
                  "${start_index}"
                  "-1"
                  _output)
    set("${output}"
        "${_output}"
        PARENT_SCOPE)
endfunction()

# A function to set environment variables of CMake from the output of `cmd /c set`
function(set_env_from_string env_string)
    # replace ; in paths with __sep__ so we can split on ;
    string(
        REGEX
        REPLACE ";"
                "__sep__"
                env_string_sep_added
                "${env_string}")

    # the variables are separated by \r?\n
    string(
        REGEX
        REPLACE "\r?\n"
                ";"
                env_list
                "${env_string_sep_added}")

    foreach(env_var ${env_list})
        # split by =
        string(
            REGEX
            REPLACE "="
                    ";"
                    env_parts
                    "${env_var}")

        list(LENGTH env_parts env_parts_length)
        if("${env_parts_length}" EQUAL "2")
            # get the variable name and value
            list(
                GET
                env_parts
                0
                env_name)
            list(
                GET
                env_parts
                1
                env_value)

            # recover ; in paths
            string(
                REGEX
                REPLACE "__sep__"
                        ";"
                        env_value
                        "${env_value}")

            # set env_name to env_value
            set(ENV{${env_name}} "${env_value}")

            # update cmake program path
            if("${env_name}" EQUAL "PATH")
                list(APPEND CMAKE_PROGRAM_PATH ${env_value})
            endif()
        endif()
    endforeach()
endfunction()

function(get_all_targets var)
    set(targets)
    get_all_targets_recursive(targets ${CMAKE_CURRENT_SOURCE_DIR})
    set(${var}
        ${targets}
        PARENT_SCOPE)
endfunction()

function(get_all_installable_targets var)
    set(targets)
    get_all_targets(targets)
    foreach(_target ${targets})
        get_target_property(_target_type ${_target} TYPE)
        if(NOT
           ${_target_type}
           MATCHES
           ".*LIBRARY|EXECUTABLE")
            list(REMOVE_ITEM targets ${_target})
        endif()
    endforeach()
    set(${var}
        ${targets}
        PARENT_SCOPE)
endfunction()

macro(get_all_targets_recursive targets dir)
    get_property(
        subdirectories
        DIRECTORY ${dir}
        PROPERTY SUBDIRECTORIES)
    foreach(subdir ${subdirectories})
        get_all_targets_recursive(${targets} ${subdir})
    endforeach()

    get_property(
        current_targets
        DIRECTORY ${dir}
        PROPERTY BUILDSYSTEM_TARGETS)
    list(APPEND ${targets} ${current_targets})
endmacro()

function(setup_msvc_if_needed)
    # If MSVC is being used, and ASAN is enabled, we need to set the debugger environment
    # so that it behaves well with MSVC's debugger, and we can run the target from visual studio
    if(MSVC)
        get_all_installable_targets(all_targets)
        message("all_targets=${all_targets}")
        set_target_properties(${all_targets} PROPERTIES VS_DEBUGGER_ENVIRONMENT "PATH=$(VC_ExecutablePath_x64);%PATH%")
    endif()

    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT intro)
endfunction()
