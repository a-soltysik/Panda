#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <panda/Assert.hpp>

namespace panda::test
{
template <typename Mock>
class ScopedMock final
{
public:
    static auto getActiveMock() -> Mock&
    {
        expect(getActiveSlot() != nullptr, "Stub requires an active mock fixture");
        return *getActiveSlot();
    }

    ScopedMock()
    {
        expect(getActiveSlot() == nullptr, "Mock scopes cannot overlap");
        getActiveSlot() = &_mock;
    }

    ScopedMock(const ScopedMock&) = delete;

    ScopedMock(ScopedMock&&) = delete;

    auto operator=(const ScopedMock&) -> ScopedMock& = delete;

    auto operator=(ScopedMock&&) -> ScopedMock& = delete;

    ~ScopedMock()
    {
        getActiveSlot() = nullptr;
    }

    auto getMock() -> testing::StrictMock<Mock>&
    {
        return _mock;
    }

private:
    static auto getActiveSlot() -> Mock*&
    {
        static Mock* current {nullptr};
        return current;
    }

    testing::StrictMock<Mock> _mock;
};

template <typename Mock>
class MockTest : public testing::Test
{
protected:
    auto getMock() -> testing::StrictMock<Mock>&
    {
        return _scope.getMock();
    }

private:
    ScopedMock<Mock> _scope;
};
}
