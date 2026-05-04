//
// Created by Zero on 2025/03/25.
//

#include <cmath>
#include <cstring>

#include "core/dynamic_buffer/host_dynamic_buffer.h"
#include "core/stl.h"
#include "core/type_system/precision_policy.h"
#include "core/type_system/type_desc.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/resources/dynamic_buffer.h"
#include "math/base.h"
#include "core/runtime/platform.h"
#include "rhi/context.h"

using namespace ocarina;

struct TestRecord {
    float4 lhs{};
    float4 rhs{};

    TestRecord() = default;
    TestRecord(float4 lhs, float4 rhs) : lhs(lhs), rhs(rhs) {}
};

struct PolicyRecord {
    real lhs{};
    real rhs{};
};

struct PaddedPolicyRecord {
    real narrow{};
    float wide{};
};

struct PaddedAtomicRecord {
    uint narrow{};
    ulong wide{};
};

struct DynamicRealRecord {
    real value{};
    Vector<real, 3> direction{};
    ocarina::array<real, 4> weights{};
    Matrix<real, 2, 2> basis{};
    float extra{};
};

OC_STRUCT(, PolicyRecord, lhs, rhs) {
};

OC_STRUCT(, PaddedPolicyRecord, narrow, wide) {
};

OC_STRUCT(, PaddedAtomicRecord, narrow, wide) {
};

OC_STRUCT(, DynamicRealRecord, value, direction, weights, basis, extra) {
};

OC_STRUCT(, TestRecord, lhs, rhs) {
};

static_assert(sizeof(TestRecord) == sizeof(float4) * 2,
              "TestRecord must stay tightly packed for SOA packing");

static bool close_float(float lhs, float rhs) {
    return std::abs(lhs - rhs) < 1e-5f;
}

static bool equal_float4(const float4 &lhs, const float4 &rhs) {
    return close_float(lhs.x, rhs.x) &&
           close_float(lhs.y, rhs.y) &&
           close_float(lhs.z, rhs.z) &&
           close_float(lhs.w, rhs.w);
}

static bool equal_record(const TestRecord &lhs, const TestRecord &rhs) {
    return equal_float4(lhs.lhs, rhs.lhs) && equal_float4(lhs.rhs, rhs.rhs);
}

static bool close_real(real lhs, real rhs, float eps) {
    return std::abs(static_cast<float>(lhs) - static_cast<float>(rhs)) < eps;
}

static float policy_epsilon(PrecisionPolicy policy) {
    return policy == PrecisionPolicy::force_f16 ? 1e-2f : 1e-5f;
}

static const char *policy_suffix(PrecisionPolicy policy) {
    return policy == PrecisionPolicy::force_f16 ? "f16" : "f32";
}

static DynamicRealRecord make_dynamic_real_record(float base) {
    return {
        .value = real{base + 0.25f},
        .direction = Vector<real, 3>{real{base + 1.0f}, real{base + 2.0f}, real{base + 3.0f}},
        .weights = {real{base + 4.0f}, real{base + 5.0f}, real{base + 6.0f}, real{base + 7.0f}},
        .basis = Matrix<real, 2, 2>{Vector<real, 2>{real{base + 8.0f}, real{base + 9.0f}},
                                    Vector<real, 2>{real{base + 10.0f}, real{base + 11.0f}}},
        .extra = base + 12.0f,
    };
}

static bool equal_dynamic_real_record(const DynamicRealRecord &lhs,
                                      const DynamicRealRecord &rhs,
                                      float eps) {
    if (!close_real(lhs.value, rhs.value, eps)) {
        return false;
    }
    for (size_t index = 0; index < 3u; ++index) {
        if (!close_real(lhs.direction[index], rhs.direction[index], eps)) {
            return false;
        }
    }
    for (size_t index = 0; index < 4u; ++index) {
        if (!close_real(lhs.weights[index], rhs.weights[index], eps)) {
            return false;
        }
    }
    for (size_t col = 0; col < 2u; ++col) {
        for (size_t row = 0; row < 2u; ++row) {
            if (!close_real(lhs.basis[col][row], rhs.basis[col][row], eps)) {
                return false;
            }
        }
    }
    return std::abs(lhs.extra - rhs.extra) < eps;
}

static bool equal_bytes(span<const std::byte> lhs, span<const std::byte> rhs) {
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

static TestRecord make_test_record(uint index) {
    float value = static_cast<float>(index);
    return {
        make_float4(value + 0.25f, value + 1.25f, value + 2.25f, value + 3.25f),
        make_float4(-value - 0.5f, -value - 1.5f, -value - 2.5f, -value - 3.5f)};
}

static PaddedPolicyRecord make_padded_policy_record(uint index) {
    float value = static_cast<float>(index * 10u);
    return {
        .narrow = real{value + 0.5f},
        .wide = value + 1.25f,
    };
}

static bool equal_padded_policy_record(const PaddedPolicyRecord &lhs,
                                       const PaddedPolicyRecord &rhs,
                                       float eps) {
    return close_real(lhs.narrow, rhs.narrow, eps) && std::abs(lhs.wide - rhs.wide) < eps;
}

static PaddedAtomicRecord make_padded_atomic_record(uint index) {
    return {
        .narrow = 100u + index * 7u,
        .wide = 1000ull + static_cast<ulong>(index) * 11ull,
    };
}

static bool equal_padded_atomic_record(const PaddedAtomicRecord &lhs,
                                       const PaddedAtomicRecord &rhs) {
    return lhs.narrow == rhs.narrow && lhs.wide == rhs.wide;
}

static vector<std::byte> pack_records_soa(const vector<TestRecord> &records) {
    size_t count = records.size();
    vector<std::byte> bytes(count * sizeof(TestRecord));
    size_t lhs_offset = 0u;
    size_t rhs_offset = count * sizeof(float4);
    for (size_t index = 0; index < count; ++index) {
        std::memcpy(bytes.data() + lhs_offset + index * sizeof(float4), &records[index].lhs, sizeof(float4));
        std::memcpy(bytes.data() + rhs_offset + index * sizeof(float4), &records[index].rhs, sizeof(float4));
    }
    return bytes;
}

static vector<TestRecord> unpack_records_soa(const vector<std::byte> &bytes, size_t count) {
    vector<TestRecord> records(count);
    size_t lhs_offset = 0u;
    size_t rhs_offset = count * sizeof(float4);
    for (size_t index = 0; index < count; ++index) {
        std::memcpy(&records[index].lhs, bytes.data() + lhs_offset + index * sizeof(float4), sizeof(float4));
        std::memcpy(&records[index].rhs, bytes.data() + rhs_offset + index * sizeof(float4), sizeof(float4));
    }
    return records;
}

static vector<std::byte> pack_padded_policy_records_soa(const vector<PaddedPolicyRecord> &records) {
    constexpr size_t narrow_size = sizeof(uint16_t);
    constexpr size_t wide_size = sizeof(float);
    size_t count = records.size();
    vector<std::byte> bytes(count * (narrow_size + wide_size));
    size_t narrow_offset = 0u;
    size_t wide_offset = count * narrow_size;
    for (size_t index = 0; index < count; ++index) {
        uint16_t narrow = float_to_half(static_cast<float>(records[index].narrow));
        std::memcpy(bytes.data() + narrow_offset + index * narrow_size, &narrow, narrow_size);
        std::memcpy(bytes.data() + wide_offset + index * wide_size, &records[index].wide, wide_size);
    }
    return bytes;
}

static vector<PaddedPolicyRecord> unpack_padded_policy_records_soa(const vector<std::byte> &bytes,
                                                                    size_t count) {
    constexpr size_t narrow_size = sizeof(uint16_t);
    constexpr size_t wide_size = sizeof(float);
    vector<PaddedPolicyRecord> records(count);
    size_t narrow_offset = 0u;
    size_t wide_offset = count * narrow_size;
    for (size_t index = 0; index < count; ++index) {
        uint16_t narrow_bits = 0u;
        std::memcpy(&narrow_bits, bytes.data() + narrow_offset + index * narrow_size, narrow_size);
        records[index].narrow = real{half_to_float(narrow_bits)};
        std::memcpy(&records[index].wide, bytes.data() + wide_offset + index * wide_size, wide_size);
    }
    return records;
}

static vector<std::byte> pack_padded_atomic_records_soa(const vector<PaddedAtomicRecord> &records) {
    constexpr size_t narrow_size = sizeof(uint);
    constexpr size_t wide_size = sizeof(ulong);
    size_t count = records.size();
    vector<std::byte> bytes(count * (narrow_size + wide_size));
    size_t narrow_offset = 0u;
    size_t wide_offset = count * narrow_size;
    for (size_t index = 0; index < count; ++index) {
        std::memcpy(bytes.data() + narrow_offset + index * narrow_size, &records[index].narrow, narrow_size);
        std::memcpy(bytes.data() + wide_offset + index * wide_size, &records[index].wide, wide_size);
    }
    return bytes;
}

static vector<PaddedAtomicRecord> unpack_padded_atomic_records_soa(const vector<std::byte> &bytes,
                                                                    size_t count) {
    constexpr size_t narrow_size = sizeof(uint);
    constexpr size_t wide_size = sizeof(ulong);
    vector<PaddedAtomicRecord> records(count);
    size_t narrow_offset = 0u;
    size_t wide_offset = count * narrow_size;
    for (size_t index = 0; index < count; ++index) {
        std::memcpy(&records[index].narrow, bytes.data() + narrow_offset + index * narrow_size, narrow_size);
        std::memcpy(&records[index].wide, bytes.data() + wide_offset + index * wide_size, wide_size);
    }
    return records;
}

static int test_buffer_storage_basic() {
    std::cout << "=== test_buffer_storage_basic ===" << std::endl;
    static_assert(sizeof(BufferStorage<ByteBufferVar>) >= sizeof(ByteBufferVar),
                  "BufferStorage must be large enough to hold ByteBufferVar");
    static_assert(alignof(BufferStorage<ByteBufferVar>) >= alignof(ByteBufferVar),
                  "BufferStorage must be correctly aligned for ByteBufferVar");
    std::cout << "  PASSED" << std::endl;
    return 0;
}

static int test_policy_aware_soa_layout() {
    std::cout << "=== test_policy_aware_soa_layout ===" << std::endl;
    int failures = 0;

    StoragePrecisionPolicy previous_policy = global_storage_policy();

    set_global_storage_policy(StoragePrecisionPolicy{.policy = PrecisionPolicy::force_f32,
                                                     .allow_real_in_storage = true});
    if (resolved_soa_type_size<PolicyRecord>() != sizeof(float) * 2u) {
        std::cerr << "  FAIL: force_f32 policy must resolve PolicyRecord to 8 bytes" << std::endl;
        ++failures;
    }
    if (resolved_soa_stride<PolicyRecord>() != sizeof(float) * 2u) {
        std::cerr << "  FAIL: force_f32 policy must resolve PolicyRecord stride to 8 bytes" << std::endl;
        ++failures;
    }
    if (resolved_soa_type_size<real>() != sizeof(float)) {
        std::cerr << "  FAIL: force_f32 policy must resolve real to 4 bytes" << std::endl;
        ++failures;
    }

    set_global_storage_policy(StoragePrecisionPolicy{.policy = PrecisionPolicy::force_f16,
                                                     .allow_real_in_storage = true});
    if (resolved_soa_type_size<PolicyRecord>() != sizeof(uint16_t) * 2u) {
        std::cerr << "  FAIL: force_f16 policy must resolve PolicyRecord to 4 bytes" << std::endl;
        ++failures;
    }
    if (resolved_soa_stride<PolicyRecord>() != sizeof(uint16_t) * 2u) {
        std::cerr << "  FAIL: force_f16 policy must resolve PolicyRecord stride to 4 bytes" << std::endl;
        ++failures;
    }
    if (resolved_soa_type_size<real>() != sizeof(uint16_t)) {
        std::cerr << "  FAIL: force_f16 policy must resolve real to 2 bytes" << std::endl;
        ++failures;
    }

    if (resolved_soa_type_size<PaddedPolicyRecord>() != sizeof(uint16_t) + sizeof(float)) {
        std::cerr << "  FAIL: force_f16 policy must use compact SOA size for padded struct" << std::endl;
        ++failures;
    }
    if (resolved_soa_stride<PaddedPolicyRecord>() != sizeof(uint16_t) + sizeof(float)) {
        std::cerr << "  FAIL: force_f16 policy must use compact SOA stride for padded struct" << std::endl;
        ++failures;
    }

    set_global_storage_policy(previous_policy);

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

static int test_dynamic_buffer_real_layout(Device &device) {
    std::cout << "=== test_dynamic_buffer_real_layout ===" << std::endl;
    int failures = 0;

    constexpr auto policies = std::array{PrecisionPolicy::force_f16, PrecisionPolicy::force_f32};
    vector<DynamicRealRecord> records = {make_dynamic_real_record(0.f),
                                         make_dynamic_real_record(20.f),
                                         make_dynamic_real_record(40.f)};

    for (auto policy : policies) {
        StoragePrecisionPolicy storage_policy{.policy = policy, .allow_real_in_storage = true};
        HostDynamicBuffer<DynamicRealRecord> host = HostDynamicBuffer<DynamicRealRecord>::create(storage_policy);
        host.write_all(span<const DynamicRealRecord>{records.data(), records.size()});

        for (size_t index = 0; index < records.size(); ++index) {
            if (!equal_dynamic_real_record(host.read(index), records[index], policy_epsilon(policy))) {
                std::cerr << "  FAIL: host dynamic buffer read mismatch for policy "
                          << policy_suffix(policy) << " at index " << index << std::endl;
                ++failures;
            }
        }

        auto buffer = device.create_dynamic_buffer<DynamicRealRecord>(storage_policy,
                                                                      records.size(),
                                                                      std::string{"test_soa_dynamic_buffer_"} +
                                                                          policy_suffix(policy));
        auto stats = buffer.sync_immediately(host);
        if (!host.bytes().empty() && stats.uploaded_bytes == 0u) {
            std::cerr << "  FAIL: expected sync upload activity for policy " << policy_suffix(policy) << std::endl;
            ++failures;
        }

        if (buffer.logical_type() != Type::of<DynamicRealRecord>()) {
            std::cerr << "  FAIL: logical type mismatch for policy " << policy_suffix(policy) << std::endl;
            ++failures;
        }
        if (buffer.policy().policy != storage_policy.policy) {
            std::cerr << "  FAIL: storage policy mismatch for policy " << policy_suffix(policy) << std::endl;
            ++failures;
        }
        if (buffer.policy().allow_real_in_storage != storage_policy.allow_real_in_storage) {
            std::cerr << "  FAIL: allow_real_in_storage mismatch for policy " << policy_suffix(policy) << std::endl;
            ++failures;
        }
        if (buffer.element_stride() != host.layout_plan().element_size_bytes()) {
            std::cerr << "  FAIL: element stride mismatch for policy " << policy_suffix(policy) << std::endl;
            ++failures;
        }

        vector<std::byte> downloaded(host.bytes().size());
        buffer.byte_view().download(downloaded.data(), 0u, false)->accept(*buffer.device()->command_visitor());
        if (!equal_bytes(host.bytes(), downloaded)) {
            std::cerr << "  FAIL: raw dynamic buffer byte roundtrip mismatch for policy "
                      << policy_suffix(policy) << std::endl;
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

static int test_buffer_read_write(Device &device, Stream &stream) {
    std::cout << "=== test_buffer_read_write ===" << std::endl;
    int failures = 0;

    constexpr uint count = 64u;
    auto src = device.create_buffer<float4>(count, "test_soa_buffer_src");
    auto dst = device.create_buffer<float4>(count, "test_soa_buffer_dst");

    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        float value = static_cast<float>(index);
        host_src[index] = make_float4(value, value + 1.f, value + 2.f, value + 3.f);
    }
    src.upload_immediately(host_src.data());

    Kernel kernel = [&](BufferVar<float4> input, BufferVar<float4> output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float4 value = input.read(index);
            output.write(index, value + make_float4(10.f, 20.f, 30.f, 40.f));
        };
    };

    auto shader = device.compile(kernel, "test_buffer_read_write");
    stream << shader(src, dst, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    for (uint index = 0; index < count; ++index) {
        float value = static_cast<float>(index);
        float4 expected = make_float4(value + 10.f, value + 21.f, value + 32.f, value + 43.f);
        if (!equal_float4(host_dst[index], expected)) {
            std::cerr << "  FAIL at index " << index << std::endl;
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

static int test_byte_buffer_read_write(Device &device, Stream &stream) {
    std::cout << "=== test_byte_buffer_read_write ===" << std::endl;
    int failures = 0;

    constexpr uint count = 32u;
    auto src = device.create_byte_buffer(count * sizeof(float4), "test_soa_byte_buffer_src");
    auto dst = device.create_byte_buffer(count * sizeof(float4), "test_soa_byte_buffer_dst");

    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        float value = static_cast<float>(index * 2u);
        host_src[index] = make_float4(value + 0.5f, value + 1.5f, value + 2.5f, value + 3.5f);
    }
    src.upload_immediately(host_src.data());

    Kernel kernel = [&](ByteBufferVar input, ByteBufferVar output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Uint offset = index * static_cast<uint>(sizeof(float4));
            Float4 value = input.load_as<float4>(offset);
            output.store(offset, value * 2.f);
        };
    };

    auto shader = device.compile(kernel, "test_byte_buffer_read_write");
    stream << shader(src, dst, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    for (uint index = 0; index < count; ++index) {
        float4 expected = host_src[index] * 2.f;
        if (!equal_float4(host_dst[index], expected)) {
            std::cerr << "  FAIL at index " << index << std::endl;
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

static int test_aos_view_var_read_write(Device &device, Stream &stream) {
    std::cout << "=== test_aos_view_var_read_write ===" << std::endl;
    int failures = 0;

    constexpr uint count = 24u;
    auto src = device.create_byte_buffer(count * sizeof(TestRecord), "test_soa_aos_src");
    auto dst = device.create_byte_buffer(count * sizeof(TestRecord), "test_soa_aos_dst");

    vector<TestRecord> host_src(count);
    vector<TestRecord> host_dst(count);
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_test_record(index);
    }
    src.upload_immediately(host_src.data());

    Kernel kernel = [&](ByteBufferVar input, ByteBufferVar output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            auto src_view = make_aos_view_var<TestRecord>(input);
            auto dst_view = make_aos_view_var<TestRecord>(output);
            Var<TestRecord> value = src_view.read(index);
            dst_view.write(index, value);
        };
    };

    auto shader = device.compile(kernel, "test_aos_view_var_read_write");
    stream << shader(src, dst, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    for (uint index = 0; index < count; ++index) {
        if (!equal_record(host_dst[index], host_src[index])) {
            std::cerr << "  FAIL at index " << index << std::endl;
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

static int test_soa_view_var_read_write(Device &device, Stream &stream) {
    std::cout << "=== test_soa_view_var_read_write ===" << std::endl;
    int failures = 0;

    constexpr uint count = 24u;
    auto src = device.create_byte_buffer(count * sizeof(TestRecord), "test_soa_soa_src");
    auto dst = device.create_byte_buffer(count * sizeof(TestRecord), "test_soa_soa_dst");

    vector<TestRecord> host_records(count);
    for (uint index = 0; index < count; ++index) {
        host_records[index] = make_test_record(index + 100u);
    }
    vector<std::byte> host_src = pack_records_soa(host_records);
    vector<std::byte> host_dst(count * sizeof(TestRecord));
    src.upload_immediately(host_src.data());

    Kernel kernel = [&](ByteBufferVar input, ByteBufferVar output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            auto src_view = make_soa_view_var<TestRecord>(input);
            auto dst_view = make_soa_view_var<TestRecord>(output);
            Var<TestRecord> value = src_view.read(index);
            dst_view.write(index, value);
        };
    };

    auto shader = device.compile(kernel, "test_soa_view_var_read_write");
    stream << shader(src, dst, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    vector<TestRecord> unpacked = unpack_records_soa(host_dst, count);
    for (uint index = 0; index < count; ++index) {
        if (!equal_record(unpacked[index], host_records[index])) {
            std::cerr << "  FAIL at index " << index << std::endl;
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

static int test_soa_view_var_compact_padded_struct(Device &device, Stream &stream) {
    std::cout << "=== test_soa_view_var_compact_padded_struct ===" << std::endl;
    int failures = 0;

    constexpr uint count = 16u;
    constexpr size_t compact_stride = sizeof(uint) + sizeof(ulong);

    static_assert(sizeof(PaddedAtomicRecord) > compact_stride,
                  "PaddedAtomicRecord must contain AoS padding so the SOA compactness test is meaningful");

    if (resolved_soa_type_size<PaddedAtomicRecord>() != compact_stride) {
        std::cerr << "  FAIL: resolved SOA type size should ignore AoS padding for PaddedAtomicRecord" << std::endl;
        ++failures;
    }
    if (resolved_soa_stride<PaddedAtomicRecord>() != compact_stride) {
        std::cerr << "  FAIL: resolved SOA stride should match compact member sum for PaddedAtomicRecord" << std::endl;
        ++failures;
    }

    auto src = device.create_byte_buffer(count * compact_stride, "test_soa_compact_padded_src");
    auto dst = device.create_byte_buffer(count * compact_stride, "test_soa_compact_padded_dst");

    vector<PaddedAtomicRecord> host_records(count);
    for (uint index = 0; index < count; ++index) {
        host_records[index] = make_padded_atomic_record(index);
    }
    vector<std::byte> host_src = pack_padded_atomic_records_soa(host_records);
    vector<std::byte> host_dst(count * compact_stride);
    src.upload_immediately(host_src.data());

    Kernel kernel = [&](ByteBufferVar input, ByteBufferVar output, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            auto src_view = make_soa_view_var<PaddedAtomicRecord>(input);
            auto dst_view = make_soa_view_var<PaddedAtomicRecord>(output);
            Var<PaddedAtomicRecord> value = src_view.read(index);
            dst_view.write(index, value);
        };
    };

    auto shader = device.compile(kernel, "test_soa_view_var_compact_padded_struct");
    stream << shader(src, dst, count).dispatch(count)
           << dst.download(host_dst.data())
           << synchronize()
           << commit();

    if (!equal_bytes(host_src, host_dst)) {
        std::cerr << "  FAIL: SOA byte layout is not compact for padded struct" << std::endl;
        ++failures;
    }

    vector<PaddedAtomicRecord> unpacked = unpack_padded_atomic_records_soa(host_dst, count);
    for (uint index = 0; index < count; ++index) {
        if (!equal_padded_atomic_record(unpacked[index], host_records[index])) {
            std::cerr << "  FAIL: unpacked record mismatch at index " << index << std::endl;
            ++failures;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int main(int argc, char *argv[]) {
    int total_failures = 0;

    total_failures += test_buffer_storage_basic();
    total_failures += test_policy_aware_soa_layout();

    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    Stream stream = device.create_stream();
    Env::printer().init(device);
    Env::debugger().init(device);

    total_failures += test_dynamic_buffer_real_layout(device);
    total_failures += test_buffer_read_write(device, stream);
    total_failures += test_byte_buffer_read_write(device, stream);
    total_failures += test_aos_view_var_read_write(device, stream);
    total_failures += test_soa_view_var_read_write(device, stream);
    total_failures += test_soa_view_var_compact_padded_struct(device, stream);

    std::cout << "\n==================" << std::endl;
    if (total_failures == 0) {
        std::cout << "ALL TESTS PASSED" << std::endl;
    } else {
        std::cout << total_failures << " FAILURE(S)" << std::endl;
    }
    return total_failures;
}