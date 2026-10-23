#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace ap27_v3d::shaders {

// glslc -mfmt=c emits SPIR-V as 32-bit word initializers. Typed arrays provide
// the size and alignment required by VkShaderModuleCreateInfo::pCode.
inline constexpr uint32_t stage_0[] =
#include "stage_0.spv.inc"
;
inline constexpr uint32_t stage_1[] =
#include "stage_1.spv.inc"
;
inline constexpr uint32_t stage_2[] =
#include "stage_2.spv.inc"
;
inline constexpr uint32_t stage_3[] =
#include "stage_3.spv.inc"
;
inline constexpr uint32_t stage_4[] =
#include "stage_4.spv.inc"
;
inline constexpr uint32_t stage_5[] =
#include "stage_5.spv.inc"
;
inline constexpr uint32_t stage_6[] =
#include "stage_6.spv.inc"
;
inline constexpr uint32_t stage_7[] =
#include "stage_7.spv.inc"
;

inline constexpr std::array<std::span<const uint32_t>, 8> stages = {
    stage_0, stage_1, stage_2, stage_3, stage_4, stage_5, stage_6, stage_7
};

} // namespace ap27_v3d::shaders
