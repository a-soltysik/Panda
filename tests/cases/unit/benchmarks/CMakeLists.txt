panda_add_test(panda_unit_benchmark unit
    SOURCES
        Arguments.cpp
        Report.cpp
        Statistics.cpp
        "${PROJECT_SOURCE_DIR}/benchmarks/src/Arguments.cpp"
        "${PROJECT_SOURCE_DIR}/benchmarks/src/Report.cpp"
        "${PROJECT_SOURCE_DIR}/benchmarks/src/Statistics.cpp"
    INCLUDE_DIRECTORIES "${PROJECT_SOURCE_DIR}/benchmarks/src"
    LIBRARIES argparse::argparse nlohmann_json::nlohmann_json Panda::Panda)
