panda_add_test(panda_unit_window unit
    SOURCES Window.cpp
    LIBRARIES Panda::Tools
    MOCKS PandaTest::GlfwMock)

panda_add_test(panda_unit_image_writer unit
    SOURCES ImageWriter.cpp
    LIBRARIES Panda::Tools)
target_include_directories(panda_unit_image_writer SYSTEM PRIVATE "${stb_SOURCE_DIR}")
