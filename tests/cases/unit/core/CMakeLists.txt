panda_add_test(panda_unit_core unit
    SOURCES Version.cpp
    LIBRARIES Panda::Panda)
target_compile_definitions(panda_unit_core PRIVATE PANDA_EXPECTED_VERSION="${PROJECT_VERSION}")
