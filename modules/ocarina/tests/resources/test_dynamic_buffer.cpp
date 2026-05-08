//
// Created by Z on 2026/4/13.
//

#include <cmath>
#include <cstdlib>
#include <iostream>

#include "core/dynamic_buffer/host_dynamic_buffer.h"
#include "core/type_system/precision_policy.h"
#include "dsl/dsl.h"
#include "math/real.h"
#include "rhi/common.h"
#include "rhi/context.h"
#include "rhi/resources/bindless_array.h"
#include "rhi/resources/dynamic_buffer.h"

using namespace ocarina;

struct DynamicLogicalLeaf {
    real roughness;
    Vector<real, 3> direction;
};

OC_STRUCT(, DynamicLogicalLeaf, roughness, direction) {
};

struct DynamicLogicalRecord {
    DynamicLogicalLeaf leaf;
    ocarina::array<real, 4> weights;
    Matrix<real, 2, 2> basis;
    float extra;
};

OC_STRUCT(, DynamicLogicalRecord, leaf, weights, basis, extra) {
};

struct DynamicKernelLogicalRecord {
    Vector<real, 4> lhs;
    Vector<real, 4> rhs;
};

OC_STRUCT(, DynamicKernelLogicalRecord, lhs, rhs) {
};

struct DynamicPaddedLogicalRecord {
    real narrow;
    float wide;
};

OC_STRUCT(, DynamicPaddedLogicalRecord, narrow, wide) {
};

using DynamicKernelRecordF16 = storage_t<DynamicKernelLogicalRecord, PrecisionPolicy::force_f16>;
using DynamicKernelRecordF32 = storage_t<DynamicKernelLogicalRecord, PrecisionPolicy::force_f32>;

OC_STRUCT(, DynamicKernelRecordF16, lhs, rhs) {
};

OC_STRUCT(, DynamicKernelRecordF32, lhs, rhs) {
};

struct DynamicResolvedRecord {
    float4 lhs{};
    float4 rhs{};

    DynamicResolvedRecord() = default;
    DynamicResolvedRecord(float4 lhs, float4 rhs) : lhs(lhs), rhs(rhs) {}
};

OC_STRUCT(, DynamicResolvedRecord, lhs, rhs) {
};

namespace {

[[nodiscard]] bool check_impl(bool condition, const char *expr) {
    if (!condition) {
        std::cerr << "FAILED: " << expr << std::endl;
    }
    return condition;
}

#define CHECK(...)                                  \
    do {                                            \
        if (!check_impl((__VA_ARGS__), #__VA_ARGS__)) { \
            return false;                           \
        }                                           \
    } while (false)

[[nodiscard]] StoragePrecisionPolicy make_policy(PrecisionPolicy policy) {
    return StoragePrecisionPolicy{.policy = policy, .allow_real_in_storage = true};
}

[[nodiscard]] DynamicLogicalRecord make_logical_record(float base) {
    return {
        .leaf = DynamicLogicalLeaf{
            .roughness = real{base + 0.25f},
            .direction = Vector<real, 3>{real{base + 1.0f}, real{base + 2.0f}, real{base + 3.0f}}},
        .weights = {real{base + 4.0f}, real{base + 5.0f}, real{base + 6.0f}, real{base + 7.0f}},
        .basis = Matrix<real, 2, 2>{Vector<real, 2>{real{base + 8.0f}, real{base + 9.0f}},
                                     Vector<real, 2>{real{base + 10.0f}, real{base + 11.0f}}},
        .extra = base + 12.0f};
}

[[nodiscard]] DynamicResolvedRecord make_resolved_record(uint index) {
    float base = static_cast<float>(index * 8u);
    return DynamicResolvedRecord{
        make_float4(base + 1.f, base + 2.f, base + 3.f, base + 4.f),
        make_float4(base + 5.f, base + 6.f, base + 7.f, base + 8.f)};
}

[[nodiscard]] DynamicResolvedRecord transform_resolved_record(DynamicResolvedRecord value) {
    value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
    value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
    return value;
}

[[nodiscard]] DynamicKernelLogicalRecord make_kernel_logical_record(uint index) {
    float base = static_cast<float>(index * 8u);
    return {
        .lhs = Vector<real, 4>{real{base + 1.0f}, real{base + 2.0f}, real{base + 3.0f}, real{base + 4.0f}},
        .rhs = Vector<real, 4>{real{base + 5.0f}, real{base + 6.0f}, real{base + 7.0f}, real{base + 8.0f}}};
}

[[nodiscard]] DynamicKernelLogicalRecord transform_kernel_logical_record(DynamicKernelLogicalRecord value) {
    value.lhs[0] = real{static_cast<float>(value.lhs[0]) + 2.0f};
    value.lhs[1] = real{static_cast<float>(value.lhs[1]) + 4.0f};
    value.lhs[2] = real{static_cast<float>(value.lhs[2]) + 6.0f};
    value.lhs[3] = real{static_cast<float>(value.lhs[3]) + 8.0f};
    value.rhs[0] = real{static_cast<float>(value.rhs[0]) - 1.0f};
    value.rhs[1] = real{static_cast<float>(value.rhs[1]) - 3.0f};
    value.rhs[2] = real{static_cast<float>(value.rhs[2]) - 5.0f};
    value.rhs[3] = real{static_cast<float>(value.rhs[3]) - 7.0f};
    return value;
}

[[nodiscard]] DynamicPaddedLogicalRecord make_padded_logical_record(uint index) {
    float base = static_cast<float>(index * 3u);
    return DynamicPaddedLogicalRecord{.narrow = real{base + 0.5f}, .wide = base + 1.25f};
}

[[nodiscard]] bool equal_padded_logical_record(const DynamicPaddedLogicalRecord &lhs,
                                               const DynamicPaddedLogicalRecord &rhs,
                                               float eps = 1e-3f) {
    return std::abs(static_cast<float>(lhs.narrow) - static_cast<float>(rhs.narrow)) <= eps &&
           std::abs(lhs.wide - rhs.wide) <= eps;
}

[[nodiscard]] float precision_epsilon(PrecisionPolicy policy) {
    return policy == PrecisionPolicy::force_f16 ? 1e-2f : 1e-5f;
}

[[nodiscard]] const char *precision_suffix(PrecisionPolicy policy) {
    return policy == PrecisionPolicy::force_f16 ? "f16" : "f32";
}

template<PrecisionPolicy policy>
using kernel_record_t = storage_t<DynamicKernelLogicalRecord, policy>;

[[nodiscard]] bool equal_kernel_logical_record(const DynamicKernelLogicalRecord &lhs,
                                               const DynamicKernelLogicalRecord &rhs,
                                               float eps) {
    for (size_t index = 0; index < 4u; ++index) {
        CHECK(std::abs(static_cast<float>(lhs.lhs[index]) - static_cast<float>(rhs.lhs[index])) <= eps);
        CHECK(std::abs(static_cast<float>(lhs.rhs[index]) - static_cast<float>(rhs.rhs[index])) <= eps);
    }
    return true;
}

[[nodiscard]] bool close_float(float lhs, float rhs, float eps = 1e-5f) {
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool equal_float4(float4 lhs, float4 rhs, float eps = 1e-5f) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps) &&
           close_float(lhs.z, rhs.z, eps) &&
           close_float(lhs.w, rhs.w, eps);
}

[[nodiscard]] bool equal_resolved_record(DynamicResolvedRecord lhs, DynamicResolvedRecord rhs) {
    return equal_float4(lhs.lhs, rhs.lhs) && equal_float4(lhs.rhs, rhs.rhs);
}

[[nodiscard]] bool equal_logical_record(const DynamicLogicalRecord &lhs,
                                        const DynamicLogicalRecord &rhs,
                                        float eps = 1e-2f) {
    CHECK(close_float(static_cast<float>(lhs.leaf.roughness), static_cast<float>(rhs.leaf.roughness), eps));
    for (size_t index = 0; index < 3u; ++index) {
        CHECK(close_float(static_cast<float>(lhs.leaf.direction[index]), static_cast<float>(rhs.leaf.direction[index]), eps));
    }
    for (size_t index = 0; index < 4u; ++index) {
        CHECK(close_float(static_cast<float>(lhs.weights[index]), static_cast<float>(rhs.weights[index]), eps));
    }
    for (size_t col = 0; col < 2u; ++col) {
        for (size_t row = 0; row < 2u; ++row) {
            CHECK(close_float(static_cast<float>(lhs.basis[col][row]), static_cast<float>(rhs.basis[col][row]), eps));
        }
    }
    CHECK(close_float(lhs.extra, rhs.extra, eps));
    return true;
}

[[nodiscard]] bool equal_bytes(span<const std::byte> lhs, span<const std::byte> rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool equal_logical_records(span<const DynamicLogicalRecord> lhs,
                                         span<const DynamicLogicalRecord> rhs,
                                         float eps = 1e-2f) {
    CHECK(lhs.size() == rhs.size());
    for (size_t index = 0; index < lhs.size(); ++index) {
        CHECK(equal_logical_record(lhs[index], rhs[index], eps));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_matches_host_bytes(Device &device) {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f16);
    auto host = HostDynamicBuffer<DynamicLogicalRecord>::create(policy);
    vector<DynamicLogicalRecord> records = {make_logical_record(0.f), make_logical_record(20.f), make_logical_record(40.f)};
    host.write_all(span<const DynamicLogicalRecord>(records.data(), records.size()));

    auto buffer = device.create_dynamic_buffer<DynamicLogicalRecord>(policy, 0u,
                                                                     "test_dynamic_buffer_host_bytes");
    auto stats = buffer.sync_immediately(host);
    CHECK(stats.full_upload);

    CHECK(buffer.logical_type() == Type::of<DynamicLogicalRecord>());
    CHECK(buffer.resolved_type() == host.layout_plan().resolved_type());
    CHECK(buffer.policy().policy == policy.policy);
    CHECK(buffer.policy().allow_real_in_storage == policy.allow_real_in_storage);
    CHECK(buffer.element_stride() == host.layout_plan().element_size_bytes());
    CHECK(buffer.element_alignment() == host.layout_plan().element_alignment());
    CHECK(buffer.size() == host.element_count());
    CHECK(buffer.size_in_byte() == host.storage_size_bytes());
    CHECK(buffer.capacity() == host.element_count());
    CHECK(buffer.capacity_in_byte() == host.storage_size_bytes());

    auto middle = buffer.view(1u, 1u);
    CHECK(middle.offset() == 1u);
    CHECK(middle.size() == 1u);
    CHECK(middle.size_in_byte() == buffer.element_stride());
    CHECK(middle.total_size() == buffer.size());
    CHECK(middle.total_size_in_byte() == buffer.size_in_byte());

    vector<std::byte> downloaded(buffer.size_in_byte());
    buffer.byte_view().download(downloaded.data(), 0u, false)->accept(*buffer.device()->command_visitor());
    CHECK(equal_bytes(host.bytes(), downloaded));
    return true;
}

[[nodiscard]] bool test_typed_dynamic_buffer_logical_round_trip(Device &device) {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f16);
    vector<DynamicLogicalRecord> host_src = {make_logical_record(2.f), make_logical_record(18.f), make_logical_record(34.f)};
    vector<DynamicLogicalRecord> host_dst(host_src.size());

    auto buffer = device.create_dynamic_buffer<DynamicLogicalRecord>(policy,
                                                                     0u,
                                                                     "test_typed_dynamic_buffer_logical_round_trip");
    buffer.upload_immediately(span<const DynamicLogicalRecord>{host_src.data(), host_src.size()});

    CHECK(buffer.logical_type() == Type::of<DynamicLogicalRecord>());
    CHECK(buffer.policy().policy == policy.policy);
    CHECK(buffer.policy().allow_real_in_storage == policy.allow_real_in_storage);
    CHECK(buffer.size() == host_src.size());

    buffer.download_immediately(host_dst.data());
    for (size_t index = 0; index < host_src.size(); ++index) {
        CHECK(equal_logical_record(host_src[index], host_dst[index]));
    }
    return true;
}

[[nodiscard]] bool test_typed_dynamic_buffer_view_aos_round_trip(Device &device) {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f16);
    vector<DynamicLogicalRecord> initial = {
        make_logical_record(10.f), make_logical_record(30.f), make_logical_record(50.f),
        make_logical_record(70.f), make_logical_record(90.f), make_logical_record(110.f)};
    vector<DynamicLogicalRecord> patch = {make_logical_record(150.f), make_logical_record(170.f)};
    vector<DynamicLogicalRecord> expected = initial;
    expected[2] = patch[0];
    expected[3] = patch[1];
    vector<DynamicLogicalRecord> downloaded(initial.size());
    vector<DynamicLogicalRecord> downloaded_patch(patch.size());

    auto buffer = device.create_dynamic_buffer<DynamicLogicalRecord>(policy,
                                                                     0u,
                                                                     "test_typed_dynamic_buffer_view_aos_round_trip");
    buffer.upload_immediately(span<const DynamicLogicalRecord>{initial.data(), initial.size()}, DynamicBufferLayout::AOS);

    auto middle = buffer.view(2u, patch.size());
    CHECK(middle.size() == patch.size());
    auto upload_commands = middle.upload(span<const DynamicLogicalRecord>{patch.data(), patch.size()},
                                         false,
                                         DynamicBufferLayout::AOS);
    upload_commands.accept(*buffer.device()->command_visitor());

    buffer.download_immediately(downloaded.data(), DynamicBufferLayout::AOS);
    CHECK(equal_logical_records(downloaded, expected));

    auto download_commands = middle.download(downloaded_patch.data(),
                                             false,
                                             DynamicBufferLayout::AOS);
    download_commands.accept(*buffer.device()->command_visitor());
    CHECK(equal_logical_records(downloaded_patch, patch));
    return true;
}

[[nodiscard]] bool test_typed_dynamic_buffer_view_soa_round_trip(Device &device) {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f32);
    vector<DynamicResolvedRecord> host_src(6u);
    vector<DynamicResolvedRecord> host_dst(host_src.size());
    for (uint index = 0u; index < host_src.size(); ++index) {
        host_src[index] = make_resolved_record(index + 40u);
    }

    auto buffer = device.create_dynamic_buffer<DynamicResolvedRecord>(policy,
                                                                      0u,
                                                                      "test_typed_dynamic_buffer_view_soa_round_trip",
                                                                      DynamicBufferLayout::SOA);
    buffer.upload_immediately(span<const DynamicResolvedRecord>{host_src.data(), host_src.size()}, DynamicBufferLayout::SOA);

    auto full = buffer.view();
    CHECK(full.size() == host_src.size());
    CHECK(full.size_in_byte() == buffer.size_in_byte());
    CHECK(full.policy().policy == policy.policy);
    CHECK(full.policy().allow_real_in_storage == policy.allow_real_in_storage);

    buffer.download_immediately(host_dst.data(), DynamicBufferLayout::SOA);
    for (size_t index = 0; index < host_src.size(); ++index) {
        CHECK(equal_resolved_record(host_dst[index], host_src[index]));
    }
    return true;
}

[[nodiscard]] bool test_typed_dynamic_buffer_soa_uses_physical_soa_bytes(Device &device) {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f16);
    vector<DynamicPaddedLogicalRecord> host_src(5u);
    vector<DynamicPaddedLogicalRecord> host_dst(host_src.size());
    for (uint index = 0u; index < host_src.size(); ++index) {
        host_src[index] = make_padded_logical_record(index + 1u);
    }

    auto buffer = device.create_dynamic_buffer<DynamicPaddedLogicalRecord>(policy,
                                                                           0u,
                                                                           "test_typed_dynamic_buffer_soa_uses_physical_soa_bytes",
                                                                           DynamicBufferLayout::SOA);
    buffer.upload_immediately(host_src.data(), host_src.size(), DynamicBufferLayout::SOA);

    const auto soa_bytes = DynamicBufferLayoutCodec<DynamicPaddedLogicalRecord>::storage_bytes(host_src.size(),
                                                                                                policy,
                                                                                                DynamicBufferLayout::SOA);
    const auto aos_bytes = DynamicBufferLayoutCodec<DynamicPaddedLogicalRecord>::storage_bytes(host_src.size(),
                                                                                                policy,
                                                                                                DynamicBufferLayout::AOS);
    CHECK(buffer.layout() == DynamicBufferLayout::SOA);
    CHECK(buffer.size_in_byte() == soa_bytes);
    CHECK(soa_bytes < aos_bytes);

    buffer.download_immediately(host_dst.data(), DynamicBufferLayout::SOA);
    for (size_t index = 0; index < host_src.size(); ++index) {
        CHECK(equal_padded_logical_record(host_dst[index], host_src[index]));
    }
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_dynamic_buffer_bindless_aos_access_impl(Device &device) {
    constexpr uint count = 12u;
    using ResolvedRecord = kernel_record_t<precision>;
    StoragePrecisionPolicy policy = make_policy(precision);
    string suffix = precision_suffix(precision);
    auto src = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_bindless_src_" + suffix);
    auto dst = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_bindless_dst_" + suffix);

    vector<DynamicKernelLogicalRecord> host_src(count);
    vector<DynamicKernelLogicalRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_kernel_logical_record(index);
    }
    src.upload_immediately(host_src.data(), host_src.size());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            auto src_view = bindless.byte_buffer_var(src_slot).template aos_view_var<ResolvedRecord>();
            auto dst_view = bindless.byte_buffer_var(dst_slot).template aos_view_var<ResolvedRecord>();
            Var<ResolvedRecord> value = src_view.read(index);
            value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
            value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
            dst_view.write(index, value);
        };
    };

    Stream stream = device.create_stream();
    auto shader = device.compile(kernel, string{"test_dynamic_buffer_bindless_aos_access_"} + suffix);
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_kernel_logical_record(host_dst[index],
                                          transform_kernel_logical_record(host_src[index]),
                                          precision_epsilon(precision)));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_bindless_aos_access(Device &device, PrecisionPolicy precision) {
    switch (precision) {
        case PrecisionPolicy::force_f16:
            return test_dynamic_buffer_bindless_aos_access_impl<PrecisionPolicy::force_f16>(device);
        case PrecisionPolicy::force_f32:
            return test_dynamic_buffer_bindless_aos_access_impl<PrecisionPolicy::force_f32>(device);
    }
    return false;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_dynamic_buffer_bindless_soa_access_impl(Device &device) {
    constexpr uint count = 12u;
    using ResolvedRecord = kernel_record_t<precision>;
    StoragePrecisionPolicy policy = make_policy(precision);
    string suffix = precision_suffix(precision);
    auto src = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_bindless_soa_src_" + suffix,
                                                                        DynamicBufferLayout::SOA);
    auto dst = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_bindless_soa_dst_" + suffix,
                                                                        DynamicBufferLayout::SOA);

    vector<DynamicKernelLogicalRecord> host_src(count);
    vector<DynamicKernelLogicalRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_kernel_logical_record(index + 20u);
    }
    src.upload_immediately(host_src.data(), host_src.size(), DynamicBufferLayout::SOA);

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            auto src_view = bindless.byte_buffer_var(src_slot).template soa_view_var<ResolvedRecord>();
            auto dst_view = bindless.byte_buffer_var(dst_slot).template soa_view_var<ResolvedRecord>();
            Var<ResolvedRecord> value = src_view.read(index);
            value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
            value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
            dst_view.write(index, value);
        };
    };

    Stream stream = device.create_stream();
    auto shader = device.compile(kernel, string{"test_dynamic_buffer_bindless_soa_access_"} + suffix);
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data(), true, DynamicBufferLayout::SOA)
           << synchronize()
           << commit();

    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_kernel_logical_record(host_dst[index],
                                          transform_kernel_logical_record(host_src[index]),
                                          precision_epsilon(precision)));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_bindless_soa_access(Device &device, PrecisionPolicy precision) {
    switch (precision) {
        case PrecisionPolicy::force_f16:
            return test_dynamic_buffer_bindless_soa_access_impl<PrecisionPolicy::force_f16>(device);
        case PrecisionPolicy::force_f32:
            return test_dynamic_buffer_bindless_soa_access_impl<PrecisionPolicy::force_f32>(device);
    }
    return false;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_dynamic_buffer_capture_aos_access_impl(Device &device) {
    constexpr uint count = 8u;
    using ResolvedRecord = kernel_record_t<precision>;
    StoragePrecisionPolicy policy = make_policy(precision);
    string suffix = precision_suffix(precision);
    auto src = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_capture_aos_src_" + suffix);
    auto dst = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_capture_aos_dst_" + suffix);

    vector<DynamicKernelLogicalRecord> host_src(count);
    vector<DynamicKernelLogicalRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_kernel_logical_record(index + 10u);
    }
    src.upload_immediately(host_src.data(), host_src.size());

    Kernel kernel = [&]() {
        Uint index = dispatch_id();
        $if(index < count) {
            auto src_view = src.aos_view_var<ResolvedRecord>();
            auto dst_view = dst.aos_view_var<ResolvedRecord>();
            Var<ResolvedRecord> value = src_view.read(index);
            value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
            value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
            dst_view.write(index, value);
        };
    };

    Stream stream = device.create_stream();
    auto shader = device.compile(kernel, string{"test_dynamic_buffer_capture_aos_access_"} + suffix);
    stream << shader().dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_kernel_logical_record(host_dst[index],
                                          transform_kernel_logical_record(host_src[index]),
                                          precision_epsilon(precision)));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_capture_aos_access(Device &device, PrecisionPolicy precision) {
    switch (precision) {
        case PrecisionPolicy::force_f16:
            return test_dynamic_buffer_capture_aos_access_impl<PrecisionPolicy::force_f16>(device);
        case PrecisionPolicy::force_f32:
            return test_dynamic_buffer_capture_aos_access_impl<PrecisionPolicy::force_f32>(device);
    }
    return false;
}

[[nodiscard]] bool test_dynamic_buffer_capture_direct_resolved_access(Device &device) {
    constexpr uint count = 8u;
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f32);
    auto src = device.create_dynamic_buffer<DynamicResolvedRecord>(policy,
                                                                   count,
                                                                   "test_dynamic_buffer_capture_direct_resolved_src");
    auto dst = device.create_dynamic_buffer<DynamicResolvedRecord>(policy,
                                                                   count,
                                                                   "test_dynamic_buffer_capture_direct_resolved_dst");

    vector<DynamicResolvedRecord> host_src(count);
    vector<DynamicResolvedRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_resolved_record(index + 90u);
    }
    src.upload_immediately(host_src.data(), host_src.size());

    Kernel kernel = [&]() {
        Uint index = dispatch_id();
        $if(index < count) {
            Var<DynamicResolvedRecord> value = src.read(index);
            value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
            value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
            dst.write(index, value);
        };
    };

    Stream stream = device.create_stream();
    auto shader = device.compile(kernel, "test_dynamic_buffer_capture_direct_resolved_access");
    stream << shader().dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_resolved_record(host_dst[index], transform_resolved_record(host_src[index])));
    }
    return true;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_dynamic_buffer_capture_direct_logical_access_impl(Device &device) {
    constexpr uint count = 8u;
    StoragePrecisionPolicy policy = make_policy(precision);
    string suffix = precision_suffix(precision);
    auto src = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_capture_direct_logical_src_" + suffix);
    auto dst = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_capture_direct_logical_dst_" + suffix);

    vector<DynamicKernelLogicalRecord> host_src(count);
    vector<DynamicKernelLogicalRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_kernel_logical_record(index + 120u);
    }
    src.upload_immediately(host_src.data(), host_src.size());

    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(policy);

    Kernel kernel = [&]() {
        Uint index = dispatch_id();
        $if(index < count) {
            Var<DynamicKernelLogicalRecord> value = src.read(index);
            value.lhs[0] = value.lhs[0] + real{2.0f};
            value.lhs[1] = value.lhs[1] + real{4.0f};
            value.lhs[2] = value.lhs[2] + real{6.0f};
            value.lhs[3] = value.lhs[3] + real{8.0f};
            value.rhs[0] = value.rhs[0] - real{1.0f};
            value.rhs[1] = value.rhs[1] - real{3.0f};
            value.rhs[2] = value.rhs[2] - real{5.0f};
            value.rhs[3] = value.rhs[3] - real{7.0f};
            dst.write(index, value);
        };
    };

    Stream stream = device.create_stream();
    auto shader = device.compile(kernel, string{"test_dynamic_buffer_capture_direct_logical_access_"} + suffix);
    stream << shader().dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    set_global_storage_policy(previous_policy);

    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_kernel_logical_record(host_dst[index],
                                          transform_kernel_logical_record(host_src[index]),
                                          precision_epsilon(precision)));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_capture_direct_logical_access(Device &device, PrecisionPolicy precision) {
    switch (precision) {
        case PrecisionPolicy::force_f16:
            return test_dynamic_buffer_capture_direct_logical_access_impl<PrecisionPolicy::force_f16>(device);
        case PrecisionPolicy::force_f32:
            return test_dynamic_buffer_capture_direct_logical_access_impl<PrecisionPolicy::force_f32>(device);
    }
    return false;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_dynamic_buffer_param_aos_access_impl(Device &device) {
    constexpr uint count = 8u;
    using ResolvedRecord = kernel_record_t<precision>;
    StoragePrecisionPolicy policy = make_policy(precision);
    string suffix = precision_suffix(precision);
    auto src = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_param_aos_src_" + suffix);
    auto dst = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_param_aos_dst_" + suffix);

    vector<DynamicKernelLogicalRecord> host_src(count);
    vector<DynamicKernelLogicalRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_kernel_logical_record(index + 30u);
    }
    src.upload_immediately(host_src.data(), host_src.size());

    Kernel kernel = [&](BufferVar<ResolvedRecord> input, BufferVar<ResolvedRecord> output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Var<ResolvedRecord> value = input.read(index);
            value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
            value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
            output.write(index, value);
        };
    };

    Stream stream = device.create_stream();
    auto shader = device.compile(kernel, string{"test_dynamic_buffer_param_aos_access_"} + suffix);
    stream << shader(src, dst, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_kernel_logical_record(host_dst[index],
                                          transform_kernel_logical_record(host_src[index]),
                                          precision_epsilon(precision)));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_param_aos_access(Device &device, PrecisionPolicy precision) {
    switch (precision) {
        case PrecisionPolicy::force_f16:
            return test_dynamic_buffer_param_aos_access_impl<PrecisionPolicy::force_f16>(device);
        case PrecisionPolicy::force_f32:
            return test_dynamic_buffer_param_aos_access_impl<PrecisionPolicy::force_f32>(device);
    }
    return false;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_dynamic_buffer_param_aos_logical_access_impl(Device &device) {
    constexpr uint count = 8u;
    StoragePrecisionPolicy policy = make_policy(precision);
    string suffix = precision_suffix(precision);
    auto src = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_param_logical_aos_src_" + suffix);
    auto dst = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_param_logical_aos_dst_" + suffix);

    vector<DynamicKernelLogicalRecord> host_src(count);
    vector<DynamicKernelLogicalRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_kernel_logical_record(index + 60u);
    }
    src.upload_immediately(host_src.data(), host_src.size());

    StoragePrecisionPolicy previous_policy = global_storage_policy();
    set_global_storage_policy(policy);

    Kernel kernel = [&](BufferVar<DynamicKernelLogicalRecord> input, BufferVar<DynamicKernelLogicalRecord> output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Var<DynamicKernelLogicalRecord> value = input.read(index);
            value.lhs[0] = value.lhs[0] + real{2.0f};
            value.lhs[1] = value.lhs[1] + real{4.0f};
            value.lhs[2] = value.lhs[2] + real{6.0f};
            value.lhs[3] = value.lhs[3] + real{8.0f};
            value.rhs[0] = value.rhs[0] - real{1.0f};
            value.rhs[1] = value.rhs[1] - real{3.0f};
            value.rhs[2] = value.rhs[2] - real{5.0f};
            value.rhs[3] = value.rhs[3] - real{7.0f};
            output.write(index, value);
        };
    };

    Stream stream = device.create_stream();
    auto shader = device.compile(kernel, string{"test_dynamic_buffer_param_aos_logical_access_"} + suffix);
    stream << shader(src, dst, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    set_global_storage_policy(previous_policy);

    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_kernel_logical_record(host_dst[index],
                                          transform_kernel_logical_record(host_src[index]),
                                          precision_epsilon(precision)));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_param_aos_logical_access(Device &device, PrecisionPolicy precision) {
    switch (precision) {
        case PrecisionPolicy::force_f16:
            return test_dynamic_buffer_param_aos_logical_access_impl<PrecisionPolicy::force_f16>(device);
        case PrecisionPolicy::force_f32:
            return test_dynamic_buffer_param_aos_logical_access_impl<PrecisionPolicy::force_f32>(device);
    }
    return false;
}

template<PrecisionPolicy precision>
[[nodiscard]] bool test_dynamic_buffer_capture_soa_access_impl(Device &device) {
    constexpr uint count = 8u;
    using ResolvedRecord = kernel_record_t<precision>;
    StoragePrecisionPolicy policy = make_policy(precision);
    string suffix = precision_suffix(precision);
    auto src = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_capture_soa_src_" + suffix,
                                                                        DynamicBufferLayout::SOA);
    auto dst = device.create_dynamic_buffer<DynamicKernelLogicalRecord>(policy,
                                                                        count,
                                                                        "test_dynamic_buffer_capture_soa_dst_" + suffix,
                                                                        DynamicBufferLayout::SOA);

    vector<DynamicKernelLogicalRecord> host_src(count);
    vector<DynamicKernelLogicalRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_kernel_logical_record(index + 20u);
    }
    src.upload_immediately(host_src.data(), host_src.size(), DynamicBufferLayout::SOA);

    Kernel kernel = [&]() {
        Uint index = dispatch_id();
        $if(index < count) {
            auto src_view = src.soa_view_var<ResolvedRecord>();
            auto dst_view = dst.soa_view_var<ResolvedRecord>();
            Var<ResolvedRecord> value = src_view.read(index);
            value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
            value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
            dst_view.write(index, value);
        };
    };

    Stream stream = device.create_stream();
    auto shader = device.compile(kernel, string{"test_dynamic_buffer_capture_soa_access_"} + suffix);
    stream << shader().dispatch(count)
           << dst.download(host_dst.data(), true, DynamicBufferLayout::SOA)
           << synchronize()
           << commit();

    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_kernel_logical_record(host_dst[index],
                                          transform_kernel_logical_record(host_src[index]),
                                          precision_epsilon(precision)));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_capture_soa_access(Device &device, PrecisionPolicy precision) {
    switch (precision) {
        case PrecisionPolicy::force_f16:
            return test_dynamic_buffer_capture_soa_access_impl<PrecisionPolicy::force_f16>(device);
        case PrecisionPolicy::force_f32:
            return test_dynamic_buffer_capture_soa_access_impl<PrecisionPolicy::force_f32>(device);
    }
    return false;
}

[[nodiscard]] bool test_dynamic_buffer_async_upload_round_trip(Device &device) {
    constexpr uint count = 5u;
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f32);
    auto buffer = device.create_dynamic_buffer<DynamicResolvedRecord>(policy,
                                                                      0u,
                                                                      "test_dynamic_buffer_async_round_trip");
    vector<DynamicResolvedRecord> host_src(count);
    vector<DynamicResolvedRecord> host_dst(count);
    for (uint index = 0u; index < count; ++index) {
        host_src[index] = make_resolved_record(index + 30u);
    }

    Stream stream = device.create_stream();
    stream << buffer.upload(host_src.data(), host_src.size())
           << buffer.download(host_dst.data())
           << synchronize()
           << commit();

    CHECK(buffer.size() == count);
    CHECK(buffer.capacity() == count);
    CHECK(buffer.size_in_byte() == host_src.size() * sizeof(DynamicResolvedRecord));
    for (uint index = 0u; index < count; ++index) {
        CHECK(equal_resolved_record(host_dst[index], host_src[index]));
    }
    return true;
}

[[nodiscard]] bool test_dynamic_buffer_direct_host_sync(Device &device) {
    StoragePrecisionPolicy policy = make_policy(PrecisionPolicy::force_f16);
    auto host = HostDynamicBuffer<DynamicLogicalRecord>::create(policy, 3u);
    vector<DynamicLogicalRecord> records = {make_logical_record(5.f), make_logical_record(25.f), make_logical_record(45.f)};
    host.write_all(span<const DynamicLogicalRecord>(records.data(), records.size()));

    auto buffer = device.create_dynamic_buffer<DynamicLogicalRecord>(policy, 0u,
                                                                     "test_dynamic_buffer_direct_host_sync");
    auto first_stats = buffer.sync_immediately(host);
    CHECK(first_stats.full_upload);
    CHECK(first_stats.uploaded_segment_count == 1u);
    CHECK(first_stats.uploaded_bytes == host.storage_size_bytes());
    CHECK(host.dirty_segments().empty());
    CHECK(!host.dirty_range().dirty);

    auto roughness_path = make_typed_field_path<FieldMemberStep<0u>, FieldMemberStep<0u>>();
    auto extra_path = make_typed_field_path<FieldMemberStep<3u>>();
    host.patch(0u, roughness_path, real{13.5f});
    host.patch(2u, extra_path, 207.25f);
    vector<ByteRegion> dirty_segments(host.dirty_segments().begin(), host.dirty_segments().end());
    size_t dirty_bytes = 0u;
    for (const auto &segment : dirty_segments) {
        dirty_bytes += segment.size();
    }

    auto second_stats = buffer.sync_immediately(host);
    CHECK(!second_stats.full_upload);
    CHECK(second_stats.uploaded_segment_count == dirty_segments.size());
    CHECK(second_stats.uploaded_bytes == dirty_bytes);
    CHECK(host.dirty_segments().empty());
    CHECK(!host.dirty_range().dirty);

    vector<std::byte> downloaded(buffer.size_in_byte());
    buffer.byte_view().download(downloaded.data(), 0u, false)->accept(*buffer.device()->command_visitor());
    CHECK(equal_bytes(host.bytes(), downloaded));
    return true;
}

}// namespace

int main() {
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    constexpr auto kernel_precisions = std::array{PrecisionPolicy::force_f16, PrecisionPolicy::force_f32};

    bool passed = true;
    passed = test_dynamic_buffer_matches_host_bytes(device) && passed;
    passed = test_typed_dynamic_buffer_logical_round_trip(device) && passed;
    passed = test_typed_dynamic_buffer_view_aos_round_trip(device) && passed;
    passed = test_typed_dynamic_buffer_view_soa_round_trip(device) && passed;
    passed = test_typed_dynamic_buffer_soa_uses_physical_soa_bytes(device) && passed;
    passed = test_dynamic_buffer_capture_direct_resolved_access(device) && passed;
    for (auto precision : kernel_precisions) {
        passed = test_dynamic_buffer_param_aos_access(device, precision) && passed;
        passed = test_dynamic_buffer_param_aos_logical_access(device, precision) && passed;
        passed = test_dynamic_buffer_capture_aos_access(device, precision) && passed;
        passed = test_dynamic_buffer_capture_direct_logical_access(device, precision) && passed;
        passed = test_dynamic_buffer_capture_soa_access(device, precision) && passed;
        passed = test_dynamic_buffer_bindless_aos_access(device, precision) && passed;
        passed = test_dynamic_buffer_bindless_soa_access(device, precision) && passed;
    }
    passed = test_dynamic_buffer_async_upload_round_trip(device) && passed;
    passed = test_dynamic_buffer_direct_host_sync(device) && passed;

    if (!passed) {
        std::cerr << "dynamic buffer test failed" << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "dynamic buffer test passed" << std::endl;
    return EXIT_SUCCESS;
}