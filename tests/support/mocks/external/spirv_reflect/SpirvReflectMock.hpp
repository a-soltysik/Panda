#pragma once

#include <gmock/gmock.h>
#include <spirv_reflect.h>

#include <cstddef>
#include <cstdint>

namespace panda::test
{
class SpirvReflectMock
{
public:
    // gMock cannot generate trailing-return declarations.
    // NOLINTBEGIN(modernize-use-trailing-return-type)
    MOCK_METHOD(SpvReflectResult, createShaderModule, (std::size_t, const void*, SpvReflectShaderModule*), ());
    MOCK_METHOD(void, destroyShaderModule, (SpvReflectShaderModule*), ());
    const SpvReflectEntryPoint* entryPoint {};
    MOCK_METHOD(SpvReflectResult,
                enumerateEntryPointDescriptorBindings,
                (const SpvReflectShaderModule*, const char*, std::uint32_t*, SpvReflectDescriptorBinding**),
                ());
    MOCK_METHOD(SpvReflectResult,
                enumerateEntryPointPushConstantBlocks,
                (const SpvReflectShaderModule*, const char*, std::uint32_t*, SpvReflectBlockVariable**),
                ());
    // NOLINTEND(modernize-use-trailing-return-type)
};
}
