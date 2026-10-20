find_program(PANDA_NODE_EXECUTABLE NAMES node REQUIRED)
find_program(PANDA_NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
execute_process(COMMAND "${PANDA_NODE_EXECUTABLE}" --version
    OUTPUT_VARIABLE panda_node_version OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE panda_node_result)
string(REGEX REPLACE "^v" "" panda_node_version "${panda_node_version}")
if(NOT panda_node_result EQUAL 0 OR panda_node_version VERSION_LESS "22.0.0")
    message(FATAL_ERROR "Documentation checks require Node.js 22 or newer")
endif()

set(panda_diagram_source "${CMAKE_CURRENT_LIST_DIR}/../tools/documentation")
set(panda_diagram_binary "${CMAKE_CURRENT_BINARY_DIR}/documentation-tools")
file(MAKE_DIRECTORY "${panda_diagram_binary}")
foreach(panda_diagram_file IN ITEMS package.json package-lock.json)
    configure_file("${panda_diagram_source}/${panda_diagram_file}"
        "${panda_diagram_binary}/${panda_diagram_file}" COPYONLY)
endforeach()

add_custom_command(OUTPUT "${panda_diagram_binary}/dependencies.stamp"
    BYPRODUCTS "${panda_diagram_binary}/node_modules/.package-lock.json"
    COMMAND "${PANDA_NPM_EXECUTABLE}" ci --ignore-scripts --no-audit --no-fund
    COMMAND "${CMAKE_COMMAND}" -E touch "${panda_diagram_binary}/dependencies.stamp"
    DEPENDS "${panda_diagram_binary}/package.json" "${panda_diagram_binary}/package-lock.json"
    WORKING_DIRECTORY "${panda_diagram_binary}"
    COMMENT "Installing pinned Mermaid syntax-check dependencies"
    USES_TERMINAL VERBATIM)

set(panda_diagram_inputs
    "${PROJECT_SOURCE_DIR}/README.md"
    "${PROJECT_SOURCE_DIR}/AGENTS.md"
    "${PROJECT_SOURCE_DIR}/docs/**/*.md"
    "${PROJECT_SOURCE_DIR}/.agents/skills/**/*.md"
    "${PROJECT_SOURCE_DIR}/.github/**/*.md"
    "${PROJECT_SOURCE_DIR}/examples/**/*.md"
    "${PROJECT_SOURCE_DIR}/benchmarks/**/*.md")
add_custom_target(panda_check_diagrams
    COMMAND "${PANDA_NPM_EXECUTABLE}" exec --offline --no
        -- mermaid-lint --no-semantic --quiet --exclude "**/node_modules/**" ${panda_diagram_inputs}
    DEPENDS "${panda_diagram_binary}/dependencies.stamp"
    WORKING_DIRECTORY "${panda_diagram_binary}"
    COMMENT "Checking Mermaid syntax in maintained Markdown"
    USES_TERMINAL VERBATIM)
set_target_properties(panda_check_diagrams PROPERTIES FOLDER "Panda/Documentation")
