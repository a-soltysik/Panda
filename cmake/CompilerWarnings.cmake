function(panda_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive-
            /w14242 /w14254 /w14263 /w14265 /w14287 /w14296
            /w14311 /w14545 /w14546 /w14547 /w14549 /w14555
            /w14619 /w14640 /w14826 /w14905 /w14906 /w14928
        )
        if(PANDA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
            -Wunused -Woverloaded-virtual -Wconversion -Wsign-conversion
            -Wnull-dereference -Wdouble-promotion -Wformat=2
            -Wimplicit-fallthrough -Wswitch-enum
        )
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE
                -Wmisleading-indentation -Wduplicated-cond
                -Wduplicated-branches -Wlogical-op -Wuseless-cast
                -Wsuggest-override
            )
        endif()
        if(PANDA_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    else()
        message(WARNING
            "No reviewed Panda warning set for ${CMAKE_CXX_COMPILER_ID}; "
            "record a compiler probe before claiming support"
        )
    endif()
endfunction()
