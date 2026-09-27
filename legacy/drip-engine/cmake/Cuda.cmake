macro(drip_target_link_cuda target_name)
    set_target_properties(${target_name} PROPERTIES
            CUDA_RUNTIME_LIBRARY Shared
            POSITION_INDEPENDENT_CODE ON
            CUDA_SEPARABLE_COMPILATION OFF
    )

    if (DRIP_CUDA_ENABLE_CUSTOM_ARCHITECTURES)
        set_target_properties(${target_name} PROPERTIES
                CUDA_ARCHITECTURES "${DRIP_CUDA_ARCHITECTURES}"
        )
    endif ()
    if (DRIP_CUDA_ENABLE_LINEINFO)
            target_compile_options(${target_name} PRIVATE
                    $<$<COMPILE_LANGUAGE:CUDA>:-lineinfo>
        )
    endif ()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANGUAGE:CUDA>:-g>
        )
        if (DRIP_CUDA_ENABLE_DEBUG)
            target_compile_options(${target_name} PRIVATE
                    $<$<COMPILE_LANGUAGE:CUDA>:-G>
            )
        endif ()
    else ()
        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANGUAGE:CUDA>:--ptxas-options=-O3>
        )
    endif ()

    if (DRIP_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANGUAGE:CUDA>:-Werror=all-warnings>
        )
    endif ()

    target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-relaxed-constexpr>
            $<$<COMPILE_LANGUAGE:CUDA>:--expt-extended-lambda>
            $<$<COMPILE_LANGUAGE:CUDA>:--extended-lambda>
    )

    if (MSVC)
        target_compile_options(${target_name} PRIVATE
                $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=/Zc:preprocessor>
        )
    endif ()
endmacro()
