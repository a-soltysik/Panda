panda_add_test(panda_integration_core integration
    SOURCES ImplementationSubstitution.cpp
    LIBRARIES Panda::Panda PandaTest::LogCapture
    MOCKS PandaTest::CoreVersionMock)
