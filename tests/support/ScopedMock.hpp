#pragma once

#include <gmock/gmock.h>

#include <panda/Assert.hpp>

namespace panda::test
{
template <typename Mock>
class ScopedMock final : public testing::StrictMock<Mock>
{
public:
    static auto getActiveMock() -> Mock&
    {
        return expect(getActiveSlot(), "Stub requires an active mock fixture");
    }

    ScopedMock()
    {
        expect(getActiveSlot() == nullptr, "Mock scopes of the same type cannot overlap");
        getActiveSlot() = this;
    }

    ScopedMock(const ScopedMock&) = delete;

    ScopedMock(ScopedMock&&) = delete;

    auto operator=(const ScopedMock&) -> ScopedMock& = delete;

    auto operator=(ScopedMock&&) -> ScopedMock& = delete;

    ~ScopedMock()
    {
        getActiveSlot() = nullptr;
    }

private:
    static auto getActiveSlot() -> Mock*&
    {
        static auto current = static_cast<Mock*>(nullptr);
        return current;
    }
};
}
