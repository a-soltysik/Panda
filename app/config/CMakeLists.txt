set(TMP_CONFIG "${CMAKE_BINARY_DIR}/config.h.in.tmp")

file(COPY_FILE "config.h.in" ${TMP_CONFIG})
file(APPEND ${TMP_CONFIG} "namespace app::config::resource{")

set(DIRS "")
file(GLOB CHILDREN RELATIVE ${RESOURCE_DIR} ${RESOURCE_DIR}/*)
foreach (CHILD ${CHILDREN})
    if (IS_DIRECTORY ${RESOURCE_DIR}/${CHILD})
        list(APPEND DIRS ${CHILD})
    endif ()
endforeach ()

foreach (DIR ${DIRS})
    file(APPEND ${TMP_CONFIG} "inline const auto ${DIR} = std::filesystem::path{resourceDir} / std::filesystem::path{\"${DIR}\"};")
endforeach ()

file(APPEND ${TMP_CONFIG} "}")

configure_file(${TMP_CONFIG} "${CONFIG_OUTPUT}/internal/config.h" ESCAPE_QUOTES)
file(REMOVE ${TMP_CONFIG})