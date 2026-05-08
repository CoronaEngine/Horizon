//
// Created by Z on 12/04/2026.
//

#include <iostream>

#include "ast/layout_resolver.h"
#include "core/dynamic_buffer/dynamic_buffer_layout_codec.h"
#include "core/type_system/type_desc.h"
#include "math/real.h"

using namespace ocarina;

struct CodecRealLeaf {
    real value;
    Vector<real, 2> index;
};

OC_MAKE_STRUCT_REFLECTION(CodecRealLeaf, value, index)
OC_MAKE_STORAGE_TYPE(CodecRealLeaf, value, index)
OC_MAKE_STRUCT_DESC(CodecRealLeaf, value, index)
OC_MAKE_STRUCT_IS_DYNAMIC(CodecRealLeaf, value, index)

struct CodecRealNested {
    CodecRealLeaf leaf;
    Vector<real, 3> samples;
    float4 extra;
};

OC_MAKE_STRUCT_REFLECTION(CodecRealNested, leaf, samples, extra)
OC_MAKE_STORAGE_TYPE(CodecRealNested, leaf, samples, extra)
OC_MAKE_STRUCT_DESC(CodecRealNested, leaf, samples, extra)
OC_MAKE_STRUCT_IS_DYNAMIC(CodecRealNested, leaf, samples, extra)

struct CodecRealArray {
    ocarina::array<real, 4> samples;
    float weight;
};

OC_MAKE_STRUCT_REFLECTION(CodecRealArray, samples, weight)
OC_MAKE_STORAGE_TYPE(CodecRealArray, samples, weight)
OC_MAKE_STRUCT_DESC(CodecRealArray, samples, weight)
OC_MAKE_STRUCT_IS_DYNAMIC(CodecRealArray, samples, weight)

struct CodecPaddedRecord {
    real narrow;
    float wide;
};

OC_MAKE_STRUCT_REFLECTION(CodecPaddedRecord, narrow, wide)
OC_MAKE_STORAGE_TYPE(CodecPaddedRecord, narrow, wide)
OC_MAKE_STRUCT_DESC(CodecPaddedRecord, narrow, wide)
OC_MAKE_STRUCT_IS_DYNAMIC(CodecPaddedRecord, narrow, wide)

namespace {

[[nodiscard]] bool check_impl(bool condition, const char *expr) {
    if (!condition) {
        std::cerr << "FAILED: " << expr << std::endl;
    }
    return condition;
}

#define CHECK(...)                            \
    do {                                      \
        if (!check_impl((__VA_ARGS__), #__VA_ARGS__)) { \
            return false;                     \
        }                                     \
    } while (false)

[[nodiscard]] bool close_float(float lhs, float rhs, float eps = 1e-3f) {
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool equal_real(real lhs, real rhs, float eps = 1e-3f) {
    return close_float(static_cast<float>(lhs), static_cast<float>(rhs), eps);
}

template<typename T, size_t N>
[[nodiscard]] bool equal_vec(const Vector<T, N> &lhs, const Vector<T, N> &rhs, float eps = 1e-3f) {
    for (size_t index = 0; index < N; ++index) {
        if (!close_float(static_cast<float>(lhs[index]), static_cast<float>(rhs[index]), eps)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool equal_leaf(const CodecRealLeaf &lhs, const CodecRealLeaf &rhs, float eps = 1e-3f) {
    return equal_real(lhs.value, rhs.value, eps) && equal_vec(lhs.index, rhs.index, eps);
}

[[nodiscard]] bool equal_nested(const CodecRealNested &lhs, const CodecRealNested &rhs, float eps = 1e-3f) {
    return equal_leaf(lhs.leaf, rhs.leaf, eps) &&
           equal_vec(lhs.samples, rhs.samples, eps) &&
           close_float(lhs.extra.x, rhs.extra.x, eps) &&
           close_float(lhs.extra.y, rhs.extra.y, eps) &&
           close_float(lhs.extra.z, rhs.extra.z, eps) &&
           close_float(lhs.extra.w, rhs.extra.w, eps);
}

[[nodiscard]] bool equal_real_array(const CodecRealArray &lhs, const CodecRealArray &rhs, float eps = 1e-6f) {
    for (size_t index = 0; index < lhs.samples.size(); ++index) {
        if (!equal_real(lhs.samples[index], rhs.samples[index], eps)) {
            return false;
        }
    }
    return close_float(lhs.weight, rhs.weight, eps);
}

[[nodiscard]] bool equal_padded_record(const CodecPaddedRecord &lhs,
                                       const CodecPaddedRecord &rhs,
                                       float eps = 1e-3f) {
    return equal_real(lhs.narrow, rhs.narrow, eps) && close_float(lhs.wide, rhs.wide, eps);
}

[[nodiscard]] CodecRealLeaf make_leaf(uint index) {
    float value = static_cast<float>(index);
    return {
        .value = real{value + 0.125f},
        .index = Vector<real, 2>{real{value + 1.25f}, real{value + 2.5f}}};
}

[[nodiscard]] CodecRealNested make_nested(uint index) {
    float value = static_cast<float>(index);
    return {
        .leaf = make_leaf(index),
        .samples = Vector<real, 3>{real{value + 3.0f}, real{value + 4.5f}, real{value + 5.75f}},
        .extra = make_float4(value + 6.0f, value + 7.0f, value + 8.0f, value + 9.0f)};
}

[[nodiscard]] CodecRealArray make_real_array(uint index) {
    float value = static_cast<float>(index);
    return {
        .samples = {real{value + 0.25f}, real{value + 1.25f}, real{value + 2.25f}, real{value + 3.25f}},
        .weight = value + 4.5f};
}

[[nodiscard]] CodecPaddedRecord make_padded_record(uint index) {
    float value = static_cast<float>(index * 10u);
    return {
        .narrow = real{value + 0.5f},
        .wide = value + 1.25f};
}

[[nodiscard]] StoragePrecisionPolicy make_policy(PrecisionPolicy policy) {
    return StoragePrecisionPolicy{.policy = policy, .allow_real_in_storage = true};
}

template<typename T>
[[nodiscard]] bool test_aos_storage_matches_layout_resolver(StoragePrecisionPolicy policy) {
    LayoutResolver resolver(policy);
    const Type *resolved = resolver.resolve(Type::of<T>());
    CHECK(resolved != nullptr);
    CHECK(DynamicBufferLayoutCodec<T>::storage_bytes(3u, policy, DynamicBufferLayout::AOS) == resolved->size() * 3u);
    return true;
}

template<typename T>
[[nodiscard]] bool test_shared_compile_time_layout_helpers(StoragePrecisionPolicy policy) {
    CHECK(detail::compile_time_resolved_layout_size<T>(policy) ==
          DynamicBufferLayoutCodec<T>::storage_bytes(1u, policy, DynamicBufferLayout::AOS));
    CHECK(detail::compile_time_soa_stride<T>(policy) == detail::compile_time_soa_storage_bytes<T>(1u, policy));
    CHECK(detail::compile_time_soa_storage_bytes<T>(3u, policy) ==
          DynamicBufferLayoutCodec<T>::storage_bytes(3u, policy, DynamicBufferLayout::SOA));
    return true;
}

template<typename T>
[[nodiscard]] bool test_shared_runtime_layout_helpers(StoragePrecisionPolicy policy) {
    const Type *resolved = Type::resolve(Type::of<T>(), policy);
    CHECK(resolved != nullptr);
    CHECK(detail::runtime_resolved_layout_size(resolved, policy) == resolved->size());
    CHECK(detail::runtime_resolved_layout_alignment(resolved, policy) == resolved->alignment());
    CHECK(detail::runtime_soa_storage_bytes(resolved, 3u, policy) ==
          DynamicBufferLayoutCodec<T>::storage_bytes(3u, policy, DynamicBufferLayout::SOA));
    return true;
}

[[nodiscard]] bool test_force_f32_array_uses_float_storage() {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f32);
    HostByteBuffer bytes;
    CodecRealArray value = make_real_array(0u);
    DynamicBufferLayoutCodec<CodecRealArray>::encode(&value, 1u, bytes, policy, DynamicBufferLayout::AOS);
    CHECK(bytes.size() == 20u);
    CHECK(close_float(bytes.load<float>(0u), 0.25f, 1e-6f));
    CHECK(close_float(bytes.load<float>(4u), 1.25f, 1e-6f));
    CHECK(close_float(bytes.load<float>(8u), 2.25f, 1e-6f));
    CHECK(close_float(bytes.load<float>(12u), 3.25f, 1e-6f));
    CHECK(close_float(bytes.load<float>(16u), 4.5f, 1e-6f));
    return true;
}

[[nodiscard]] bool test_storage_type_scheme() {
    using LeafHalfStorage = resolved_storage_by_tag_t<CodecRealLeaf, half>;
    using NestedHalfStorage = resolved_storage_by_tag_t<CodecRealNested, half>;
    using LeafF16 = storage_t<CodecRealLeaf, PrecisionPolicy::force_f16>;
    using LeafF32 = storage_t<CodecRealLeaf, PrecisionPolicy::force_f32>;
    using NestedF16 = storage_t<CodecRealNested, PrecisionPolicy::force_f16>;
    using ArrayF16 = storage_t<CodecRealArray, PrecisionPolicy::force_f16>;
    StoragePrecisionPolicy f16_policy = make_policy(PrecisionPolicy::force_f16);

    static_assert(std::is_same_v<LeafHalfStorage, LeafF16>);
    static_assert(std::is_same_v<NestedHalfStorage, NestedF16>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<LeafF16>().value)>, half>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<LeafF16>().index)>, half2>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<LeafF32>().value)>, float>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<NestedF16>().leaf)>, LeafF16>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<NestedF16>().samples)>, half3>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::declval<ArrayF16>().samples)>, ocarina::array<half, 4>>);
    static_assert(std::same_as<std::remove_cvref_t<decltype(TypeDesc<LeafF16>::name())>, string_view>);

    const Type *leaf_type = Type::of<LeafF16>();
    const Type *resolved_leaf_type = Type::resolve(Type::of<CodecRealLeaf>(), f16_policy);
    CHECK(leaf_type != nullptr);
    CHECK(resolved_leaf_type != nullptr);
    CHECK(leaf_type->is_structure());
    CHECK(leaf_type->description() == TypeDesc<LeafF16>::description());
    CHECK(leaf_type->cname() == "CodecRealLeaf_storage_f16");
    CHECK(leaf_type->alignment() == resolved_leaf_type->alignment());
    CHECK(leaf_type->members().size() == resolved_leaf_type->members().size());
    CHECK(leaf_type->members()[0] == resolved_leaf_type->members()[0]);
    CHECK(leaf_type->members()[1] == resolved_leaf_type->members()[1]);
    CHECK(leaf_type->description() != resolved_leaf_type->description());

    CodecRealNested nested = make_nested(2u);
    auto nested_f16 = to_storage_value<PrecisionPolicy::force_f16>(nested);
    auto nested_f32 = to_storage_value<PrecisionPolicy::force_f32>(nested);
    CHECK(close_float(static_cast<float>(nested_f16.leaf.value), 2.125f, 1e-2f));
    CHECK(close_float(nested_f32.leaf.value, 2.125f, 1e-6f));
    CHECK(equal_nested(nested, from_storage_value<CodecRealNested, PrecisionPolicy::force_f16>(nested_f16), 1e-2f));
    CHECK(equal_nested(nested, from_storage_value<CodecRealNested, PrecisionPolicy::force_f32>(nested_f32), 1e-6f));

    CodecRealArray array_value = make_real_array(1u);
    auto array_f16 = to_storage_value<PrecisionPolicy::force_f16>(array_value);
    CHECK(close_float(static_cast<float>(array_f16.samples[0]), 1.25f, 1e-2f));
    CHECK(equal_real_array(array_value, from_storage_value<CodecRealArray, PrecisionPolicy::force_f16>(array_f16), 1e-2f));
    return true;
}

[[nodiscard]] bool test_half_aos_round_trip() {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f16);
    vector<CodecRealNested> src(3u);
    vector<CodecRealNested> dst(3u);
    for (uint index = 0; index < src.size(); ++index) {
        src[index] = make_nested(index);
    }
    HostByteBuffer bytes;
    DynamicBufferLayoutCodec<CodecRealNested>::encode(src.data(), src.size(), bytes, policy, DynamicBufferLayout::AOS);
    DynamicBufferLayoutCodec<CodecRealNested>::decode(bytes, dst.size(), dst.data(), policy, DynamicBufferLayout::AOS);
    for (size_t index = 0; index < src.size(); ++index) {
        CHECK(equal_nested(src[index], dst[index]));
    }
    return true;
}

[[nodiscard]] bool test_float_aos_round_trip() {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f32);
    vector<CodecRealLeaf> src(4u);
    vector<CodecRealLeaf> dst(4u);
    for (uint index = 0; index < src.size(); ++index) {
        src[index] = make_leaf(index);
    }
    HostByteBuffer bytes;
    DynamicBufferLayoutCodec<CodecRealLeaf>::encode(src.data(), src.size(), bytes, policy, DynamicBufferLayout::AOS);
    DynamicBufferLayoutCodec<CodecRealLeaf>::decode(bytes, dst.size(), dst.data(), policy, DynamicBufferLayout::AOS);
    for (size_t index = 0; index < src.size(); ++index) {
        CHECK(equal_leaf(src[index], dst[index], 1e-6f));
    }
    return true;
}

[[nodiscard]] bool test_half_soa_round_trip() {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f16);
    vector<CodecRealNested> src(2u);
    vector<CodecRealNested> dst(2u);
    for (uint index = 0; index < src.size(); ++index) {
        src[index] = make_nested(index);
    }
    HostByteBuffer bytes;
    DynamicBufferLayoutCodec<CodecRealNested>::encode(src.data(), src.size(), bytes, policy, DynamicBufferLayout::SOA);
    CHECK(bytes.size() == 60u);
    DynamicBufferLayoutCodec<CodecRealNested>::decode(bytes, dst.size(), dst.data(), policy, DynamicBufferLayout::SOA);
    for (size_t index = 0; index < src.size(); ++index) {
        CHECK(equal_nested(src[index], dst[index]));
    }
    return true;
}

[[nodiscard]] bool test_float_soa_array_round_trip() {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f32);
    vector<CodecRealArray> src(3u);
    vector<CodecRealArray> dst(3u);
    for (uint index = 0; index < src.size(); ++index) {
        src[index] = make_real_array(index);
    }
    HostByteBuffer bytes;
    DynamicBufferLayoutCodec<CodecRealArray>::encode(src.data(), src.size(), bytes, policy, DynamicBufferLayout::SOA);
    CHECK(bytes.size() == 60u);
    CHECK(close_float(bytes.load<float>(0u), 0.25f, 1e-6f));
    CHECK(close_float(bytes.load<float>(4u), 1.25f, 1e-6f));
    CHECK(close_float(bytes.load<float>(8u), 2.25f, 1e-6f));
    DynamicBufferLayoutCodec<CodecRealArray>::decode(bytes, dst.size(), dst.data(), policy, DynamicBufferLayout::SOA);
    for (size_t index = 0; index < src.size(); ++index) {
        CHECK(equal_real_array(src[index], dst[index], 1e-6f));
    }
    return true;
}

[[nodiscard]] bool test_half_soa_struct_packs_members_without_padding() {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f16);
    vector<CodecPaddedRecord> src(3u);
    vector<CodecPaddedRecord> dst(3u);
    for (uint index = 0; index < src.size(); ++index) {
        src[index] = make_padded_record(index);
    }

    CHECK(detail::compile_time_resolved_layout_size<CodecPaddedRecord>(policy) == 8u);
    CHECK(detail::compile_time_soa_storage_bytes<CodecPaddedRecord>(1u, policy) == 6u);
    CHECK(DynamicBufferLayoutCodec<CodecPaddedRecord>::storage_bytes(3u, policy, DynamicBufferLayout::AOS) == 24u);
    CHECK(DynamicBufferLayoutCodec<CodecPaddedRecord>::storage_bytes(3u, policy, DynamicBufferLayout::SOA) == 18u);

    HostByteBuffer bytes;
    DynamicBufferLayoutCodec<CodecPaddedRecord>::encode(src.data(), src.size(), bytes, policy, DynamicBufferLayout::SOA);
    CHECK(bytes.size() == 18u);

    CHECK(bytes.load<uint16_t>(0u) == float_to_half(0.5f));
    CHECK(bytes.load<uint16_t>(2u) == float_to_half(10.5f));
    CHECK(bytes.load<uint16_t>(4u) == float_to_half(20.5f));
    CHECK(close_float(bytes.load<float>(6u), 1.25f, 1e-6f));
    CHECK(close_float(bytes.load<float>(10u), 11.25f, 1e-6f));
    CHECK(close_float(bytes.load<float>(14u), 21.25f, 1e-6f));

    DynamicBufferLayoutCodec<CodecPaddedRecord>::decode(bytes, dst.size(), dst.data(), policy, DynamicBufferLayout::SOA);
    for (size_t index = 0; index < src.size(); ++index) {
        CHECK(equal_padded_record(src[index], dst[index], 1e-2f));
    }
    return true;
}

}// namespace

int main() {
    bool passed = true;
    passed = test_aos_storage_matches_layout_resolver<CodecRealLeaf>(make_policy(PrecisionPolicy::force_f32)) && passed;
    passed = test_aos_storage_matches_layout_resolver<CodecRealLeaf>(make_policy(PrecisionPolicy::force_f16)) && passed;
    passed = test_aos_storage_matches_layout_resolver<CodecRealArray>(make_policy(PrecisionPolicy::force_f32)) && passed;
    passed = test_shared_compile_time_layout_helpers<CodecRealNested>(make_policy(PrecisionPolicy::force_f32)) && passed;
    passed = test_shared_compile_time_layout_helpers<CodecRealNested>(make_policy(PrecisionPolicy::force_f16)) && passed;
    passed = test_shared_runtime_layout_helpers<CodecRealNested>(make_policy(PrecisionPolicy::force_f32)) && passed;
    passed = test_shared_runtime_layout_helpers<CodecRealNested>(make_policy(PrecisionPolicy::force_f16)) && passed;
    passed = test_force_f32_array_uses_float_storage() && passed;
    passed = test_storage_type_scheme() && passed;
    passed = test_float_aos_round_trip() && passed;
    passed = test_half_aos_round_trip() && passed;
    passed = test_half_soa_round_trip() && passed;
    passed = test_float_soa_array_round_trip() && passed;
    passed = test_half_soa_struct_packs_members_without_padding() && passed;

    if (!passed) {
        std::cerr << "dynamic buffer layout codec test failed" << std::endl;
        return 1;
    }
    std::cout << "dynamic buffer layout codec test passed" << std::endl;
    return 0;
}