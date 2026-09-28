panda_add_test(panda_unit_window unit
    SOURCES Window.cpp
    LIBRARIES Panda::Tools
    MOCKS PandaTest::GlfwMock)
