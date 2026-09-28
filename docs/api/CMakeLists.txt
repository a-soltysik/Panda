find_package(Doxygen REQUIRED)

set(DOXYGEN_PROJECT_NAME Panda)
set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_GENERATE_LATEX NO)
set(DOXYGEN_HAVE_DOT NO)
set(DOXYGEN_QUIET YES)
set(DOXYGEN_EXTRACT_ALL NO)
set(DOXYGEN_EXTRACT_PRIVATE NO)
set(DOXYGEN_EXCLUDE_SYMBOLS "panda::detail" "panda::log::detail")
set(DOXYGEN_WARNINGS YES)
set(DOXYGEN_WARN_IF_UNDOCUMENTED YES)
set(DOXYGEN_WARN_IF_DOC_ERROR YES)
set(DOXYGEN_WARN_AS_ERROR FAIL_ON_WARNINGS)
set(DOXYGEN_RECURSIVE YES)
set(DOXYGEN_FILE_PATTERNS "*.hpp")

set(api_directories
        "${PROJECT_SOURCE_DIR}/include/common"
        "${PROJECT_SOURCE_DIR}/include/core"
        "${PROJECT_SOURCE_DIR}/include/tools"
        "${PROJECT_SOURCE_DIR}/include/cuda")

doxygen_add_docs(panda_docs ${api_directories}
        COMMENT "Checking and generating Panda public API documentation")
set_target_properties(panda_docs PROPERTIES FOLDER "Panda/Documentation")
