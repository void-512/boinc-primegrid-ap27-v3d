#pragma once

#include <cstdint>

namespace ap27_v3d {

inline constexpr uint32_t CANDIDATE_CAPACITY = 1'000'000;
inline constexpr uint32_t INTERMEDIATE_RECORD_CAPACITY = 3'000'000;
inline constexpr uint64_t SECOND_SHIFT_RECORD_TAG = 1ULL << 48;

// Matches the two uvec4 push-constant vectors in shaders/tile.comp.
struct PushConstants {
    uint32_t sieve_stride_lo, sieve_stride_hi;
    uint32_t ap_step_lo, ap_step_hi;
    uint32_t first_shift, n59_count;
    uint32_t candidate_capacity, intermediate_record_capacity;
};
static_assert(sizeof(PushConstants) == 8 * sizeof(uint32_t));

// Control-buffer words shared by the host and the six shader stages.
inline constexpr uint32_t INTERMEDIATE_RECORD_COUNT_WORD = 0;
inline constexpr uint32_t CANDIDATE_COUNT_WORD = 1;
inline constexpr uint32_t ERROR_FLAGS_WORD = 2;
inline constexpr uint32_t AP_HIT_COUNT_WORD = 3;
inline constexpr uint32_t SIEVE_DISPATCH_WORD = 4; // VkDispatchIndirectCommand, words 4-6.
inline constexpr uint32_t CHECK_DISPATCH_WORD = 8; // VkDispatchIndirectCommand, words 8-10.
inline constexpr uint32_t FINAL_RECORD_COUNT_WORD = 12;

} // namespace ap27_v3d
