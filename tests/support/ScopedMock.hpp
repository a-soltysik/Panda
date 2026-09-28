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
        expect(getActiveSlot() != nullptr, "Stub requires an active mock fixture");
        return *getActiveSlot();
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
