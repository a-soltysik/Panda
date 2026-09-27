panda_add_component(panda_core
    ALIAS Panda::Panda
    INCLUDE_DIRECTORY "${PROJECT_SOURCE_DIR}/include/core"
    SOURCES Panda.cpp
    LIBRARIES PUBLIC panda_common glm::glm)

set_target_properties(panda_core PROPERTIES OUTPUT_NAME panda)
target_compile_definitions(panda_core PRIVATE PANDA_VERSION_TEXT="${PROJECT_VERSION}")
