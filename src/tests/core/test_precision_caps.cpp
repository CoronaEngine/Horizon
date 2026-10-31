//
// Created by Z on 12/04/2026.
//

#include "ast/layout_resolver.h"
#include "math/real.h"
#include "core/stl.h"

#include <iostream>

using namespace ocarina;

static bool check_impl(bool cond, const char *expr) {
    if (!cond) {
        std::cerr << "FAILED: " << expr << std::endl;
    }
    return cond;
}

#define CHECK(...)                            \
    do {                                      \
        if (!check_impl((__VA_ARGS__), #__VA_ARGS__)) { \
            return false;                     \
        }                                     \
    } while (false)

static bool test_no_fp16_support() {
    constexpr size_t GB = size_t(1) << 30;
    // SM < 53: no fp16 → always force_f32 regardless of VRAM
    DevicePrecisionCaps caps{.compute_capability = 52, .total_vram_bytes = 2 * GB};
    CHECK(caps.recommend_policy() == PrecisionPolicy::force_f32);
    CHECK(caps.recommend_policy(8 * GB) == PrecisionPolicy::force_f32);
    return true;
}

static bool test_native_fp16_slow() {
    constexpr size_t GB = size_t(1) << 30;
    // SM 53-59: native fp16 but no fast throughput → force_f32
    DevicePrecisionCaps caps{
        .compute_capability = 53, .total_vram_bytes = 4 * GB,
        .has_native_fp16 = true};
    CHECK(caps.recommend_policy() == PrecisionPolicy::force_f32);
    return true;
}

static bool test_fast_fp16_no_pressure() {
    constexpr size_t GB = size_t(1) << 30;
    // SM 60-69, enough VRAM → force_f32
    DevicePrecisionCaps caps{
        .compute_capability = 61, .total_vram_bytes = 8 * GB,
        .has_native_fp16 = true, .has_fast_fp16 = true};
    CHECK(caps.recommend_policy() == PrecisionPolicy::force_f32);
    return true;
}

static bool test_fast_fp16_vram_tight() {
    constexpr size_t GB = size_t(1) << 30;
    // SM 60-69, VRAM < 4GB → force_f16
    DevicePrecisionCaps caps{
        .compute_capability = 61, .total_vram_bytes = 3 * GB,
        .has_native_fp16 = true, .has_fast_fp16 = true};
    CHECK(caps.recommend_policy() == PrecisionPolicy::force_f16);
    return true;
}

static bool test_fast_fp16_scene_pressure() {
    constexpr size_t GB = size_t(1) << 30;
    // SM 60-69, scene exceeds 60% VRAM → force_f16
    DevicePrecisionCaps caps{
        .compute_capability = 61, .total_vram_bytes = 8 * GB,
        .has_native_fp16 = true, .has_fast_fp16 = true};
    CHECK(caps.recommend_policy(6 * GB) == PrecisionPolicy::force_f16);
    return true;
}

static bool test_tensor_fp16_no_pressure() {
    constexpr size_t GB = size_t(1) << 30;
    // SM >= 70, enough VRAM → force_f32
    DevicePrecisionCaps caps{
        .compute_capability = 75, .total_vram_bytes = 8 * GB,
        .has_native_fp16 = true, .has_fast_fp16 = true, .has_tensor_fp16 = true};
    CHECK(caps.recommend_policy() == PrecisionPolicy::force_f32);
    return true;
}

static bool test_tensor_fp16_vram_tight() {
    constexpr size_t GB = size_t(1) << 30;
    // SM >= 70, VRAM < 4GB → force_f16 (fast_fp16 branch fires first)
    DevicePrecisionCaps caps{
        .compute_capability = 86, .total_vram_bytes = 3 * GB,
        .has_native_fp16 = true, .has_fast_fp16 = true, .has_tensor_fp16 = true};
    CHECK(caps.recommend_policy() == PrecisionPolicy::force_f16);
    return true;
}

static bool test_tensor_fp16_scene_pressure() {
    constexpr size_t GB = size_t(1) << 30;
    // SM >= 70, scene > 60% VRAM → force_f16
    DevicePrecisionCaps caps{
        .compute_capability = 86, .total_vram_bytes = 8 * GB,
        .has_native_fp16 = true, .has_fast_fp16 = true, .has_tensor_fp16 = true};
    CHECK(caps.recommend_policy(7 * GB) == PrecisionPolicy::force_f16);
    return true;
}

static bool test_recommend_feeds_layout_resolver() {
    constexpr size_t GB = size_t(1) << 30;
    DevicePrecisionCaps caps{
        .compute_capability = 86, .total_vram_bytes = 8 * GB,
        .has_native_fp16 = true, .has_fast_fp16 = true, .has_tensor_fp16 = true};
    PrecisionPolicy policy = caps.recommend_policy();
    CHECK(policy == PrecisionPolicy::force_f32);
    LayoutResolver resolver(StoragePrecisionPolicy{.policy = policy, .allow_real_in_storage = true});
    CHECK(resolver.resolve(Type::of<real>()) == Type::of<float>());
    CHECK(resolver.resolve(Type::of<float>()) == Type::of<float>());
    return true;
}

static bool test_boundary_sm_values() {
    constexpr size_t GB = size_t(1) << 30;
    // Exact SM 53 boundary — has_native_fp16 only
    {
        DevicePrecisionCaps caps{
            .compute_capability = 53, .total_vram_bytes = 2 * GB,
            .has_native_fp16 = true};
        CHECK(caps.recommend_policy() == PrecisionPolicy::force_f32);
    }
    // Exact SM 60 boundary with pressure
    {
        DevicePrecisionCaps caps{
            .compute_capability = 60, .total_vram_bytes = 2 * GB,
            .has_native_fp16 = true, .has_fast_fp16 = true};
        CHECK(caps.recommend_policy() == PrecisionPolicy::force_f16);
    }
    // Exact SM 70 boundary without pressure
    {
        DevicePrecisionCaps caps{
            .compute_capability = 70, .total_vram_bytes = 8 * GB,
            .has_native_fp16 = true, .has_fast_fp16 = true, .has_tensor_fp16 = true};
        CHECK(caps.recommend_policy() == PrecisionPolicy::force_f32);
    }
    return true;
}

static bool test_zero_vram() {
    // total_vram_bytes = 0 → always tight
    DevicePrecisionCaps caps{
        .compute_capability = 86, .total_vram_bytes = 0,
        .has_native_fp16 = true, .has_fast_fp16 = true, .has_tensor_fp16 = true};
    CHECK(caps.recommend_policy() == PrecisionPolicy::force_f16);
    return true;
}

static bool test_default_caps() {
    // Default-constructed caps: everything zero/false → force_f32
    DevicePrecisionCaps caps{};
    CHECK(caps.recommend_policy() == PrecisionPolicy::force_f32);
    return true;
}

int main() {
    bool passed = true;
    passed = test_no_fp16_support() && passed;
    passed = test_native_fp16_slow() && passed;
    passed = test_fast_fp16_no_pressure() && passed;
    passed = test_fast_fp16_vram_tight() && passed;
    passed = test_fast_fp16_scene_pressure() && passed;
    passed = test_tensor_fp16_no_pressure() && passed;
    passed = test_tensor_fp16_vram_tight() && passed;
    passed = test_tensor_fp16_scene_pressure() && passed;
    passed = test_recommend_feeds_layout_resolver() && passed;
    passed = test_boundary_sm_values() && passed;
    passed = test_zero_vram() && passed;
    passed = test_default_caps() && passed;

    if (!passed) {
        std::cerr << "precision caps test failed" << std::endl;
        return 1;
    }
    std::cout << "precision caps test passed" << std::endl;
    return 0;
}
