#include <spirv_reflect.h>

#include <cstddef>
#include <cstdint>

#include "ScopedMock.hpp"
#include "external/spirv_reflect/SpirvReflectMock.hpp"

// Match parameter names from the vendor-declared C API.
extern "C" {
auto spvReflectCreateShaderModule(std::size_t size, const void* code, SpvReflectShaderModule* module)
    -> SpvReflectResult
{
    return panda::test::ScopedMock<panda::test::SpirvReflectMock>::getActiveMock().createShaderModule(size,
                                                                                                      code,
                                                                                                      module);
}

void spvReflectDestroyShaderModule(SpvReflectShaderModule* module)
{
    panda::test::ScopedMock<panda::test::SpirvReflectMock>::getActiveMock().destroyShaderModule(module);
}

// NOLINTBEGIN(readability-identifier-naming)
auto spvReflectGetEntryPoint(const SpvReflectShaderModule* p_module, const char* entry_point)
    -> const SpvReflectEntryPoint*
{
    static_cast<void>(p_module);
    static_cast<void>(entry_point);
    return panda::test::ScopedMock<panda::test::SpirvReflectMock>::getActiveMock().entryPoint;
}

auto spvReflectEnumerateEntryPointDescriptorBindings(const SpvReflectShaderModule* p_module,
                                                     const char* entry_point,
                                                     std::uint32_t* p_count,
                                                     SpvReflectDescriptorBinding** pp_bindings) -> SpvReflectResult
{
    return panda::test::ScopedMock<panda::test::SpirvReflectMock>::getActiveMock()
        .enumerateEntryPointDescriptorBindings(p_module, entry_point, p_count, pp_bindings);
}

auto spvReflectEnumerateEntryPointPushConstantBlocks(const SpvReflectShaderModule* p_module,
                                                     const char* entry_point,
                                                     std::uint32_t* p_count,
                                                     SpvReflectBlockVariable** pp_blocks) -> SpvReflectResult
{
    return panda::test::ScopedMock<panda::test::SpirvReflectMock>::getActiveMock()
        .enumerateEntryPointPushConstantBlocks(p_module, entry_point, p_count, pp_blocks);
}
}
// NOLINTEND(readability-identifier-naming)
