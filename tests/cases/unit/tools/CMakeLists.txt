panda_add_test(panda_unit_window unit
    SOURCES Window.cpp
    LIBRARIES Panda::Tools
    MOCKS PandaTest::GlfwMock)

panda_add_test(panda_unit_camera unit
    SOURCES Camera.cpp
    LIBRARIES Panda::Tools)
