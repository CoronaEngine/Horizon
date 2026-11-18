#include "backends/vulkan_compute/vk_trace_dispatch_limits.h"

using ocarina::VkTraceDispatchLimits;

constexpr VkTraceDispatchLimits small{{8, 16, 32}, 64};
static_assert(!small.exceeded_axis({8, 16, 32}));
static_assert(small.exceeded_axis({9, 2, 3}) == 0);
static_assert(small.exceeded_axis({1, 17, 3}) == 1);
static_assert(small.exceeded_axis({1, 2, 33}) == 2);
static_assert(!small.exceeds_invocation_count({4, 4, 4}));
static_assert(small.exceeds_invocation_count({5, 13, 1}));

constexpr VkTraceDispatchLimits large{{UINT64_MAX, UINT64_MAX, UINT64_MAX}, UINT32_MAX};
static_assert(!large.exceeded_axis({UINT32_MAX, UINT32_MAX, UINT32_MAX}));
static_assert(!large.exceeds_invocation_count({UINT32_MAX, 1, 1}));
static_assert(large.exceeds_invocation_count({65536, 65536, 1}));
// A three-factor product in uint64_t would wrap to zero.
static_assert(large.exceeds_invocation_count({1u << 31u, 1u << 31u, 4}));
static_assert(!large.exceeds_invocation_count({UINT32_MAX, UINT32_MAX, 0}));
static_assert(!large.exceeds_invocation_count({0, UINT32_MAX, UINT32_MAX}));

int main() {}
