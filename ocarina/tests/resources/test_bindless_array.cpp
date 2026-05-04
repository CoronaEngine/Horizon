#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/base.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

struct TestRecord {
    float4 lhs{};
    float4 rhs{};

    TestRecord() = default;
    TestRecord(float4 lhs, float4 rhs) : lhs(lhs), rhs(rhs) {}
};

OC_STRUCT(, TestRecord, lhs, rhs) {
};

namespace {

struct CapturedLog {
    int level;
    string message;
};

static_assert(sizeof(TestRecord) == sizeof(float4) * 2,
              "TestRecord must stay tightly packed for SOA packing");

[[nodiscard]] bool close_float(float lhs, float rhs, float eps = 1e-5f) {
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool equal_float4(const float4 &lhs, const float4 &rhs, float eps = 1e-5f) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps) &&
           close_float(lhs.z, rhs.z, eps) &&
           close_float(lhs.w, rhs.w, eps);
}

[[nodiscard]] bool equal_record(const TestRecord &lhs, const TestRecord &rhs, float eps = 1e-5f) {
    return equal_float4(lhs.lhs, rhs.lhs, eps) &&
           equal_float4(lhs.rhs, rhs.rhs, eps);
}

[[nodiscard]] size_t pixel_index(uint2 res, uint x, uint y) {
    return static_cast<size_t>(y) * res.x + x;
}

[[nodiscard]] size_t voxel_index(uint3 res, uint x, uint y, uint z) {
    return (static_cast<size_t>(z) * res.y + y) * res.x + x;
}

[[nodiscard]] TestRecord make_test_record(uint index) {
    float value = static_cast<float>(index * 5u);
    return {
        make_float4(value + 1.f, value + 2.f, value + 3.f, value + 4.f),
        make_float4(-value - 5.f, -value - 6.f, -value - 7.f, -value - 8.f)};
}

[[nodiscard]] vector<std::byte> pack_records_soa(const vector<TestRecord> &records) {
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

[[nodiscard]] vector<TestRecord> unpack_records_soa(const vector<std::byte> &bytes, size_t count) {
    vector<TestRecord> records(count);
    size_t lhs_offset = 0u;
    size_t rhs_offset = count * sizeof(float4);
    for (size_t index = 0; index < count; ++index) {
        std::memcpy(&records[index].lhs, bytes.data() + lhs_offset + index * sizeof(float4), sizeof(float4));
        std::memcpy(&records[index].rhs, bytes.data() + rhs_offset + index * sizeof(float4), sizeof(float4));
    }
    return records;
}

[[nodiscard]] float4 make_buffer_value(uint index) {
    float value = static_cast<float>(index * 3u);
    return make_float4(value + 1.f, value + 2.f, value + 3.f, value + 4.f);
}

[[nodiscard]] float4 transform_buffer_value(float4 value) {
    return value + make_float4(10.f, 20.f, 30.f, 40.f);
}

[[nodiscard]] float4 make_byte_buffer_value(uint index) {
    float value = static_cast<float>(index * 4u);
    return make_float4(value + 2.f, value + 4.f, value + 6.f, value + 8.f);
}

[[nodiscard]] float4 transform_byte_buffer_value(float4 value) {
    return value * 2.f + make_float4(1.f, 3.f, 5.f, 7.f);
}

[[nodiscard]] TestRecord transform_record(const TestRecord &value) {
    return {
        value.lhs + make_float4(2.f, 4.f, 6.f, 8.f),
        value.rhs - make_float4(1.f, 3.f, 5.f, 7.f)};
}

[[nodiscard]] float4 make_texture2d_value(uint x, uint y) {
    return make_float4(static_cast<float>(x + 1u),
                       static_cast<float>(y + 11u),
                       static_cast<float>(x + y + 21u),
                       static_cast<float>(100u + x * 10u + y));
}

[[nodiscard]] float4 make_texture3d_value(uint x, uint y, uint z) {
    return make_float4(static_cast<float>(x + 1u),
                       static_cast<float>(y + 21u),
                       static_cast<float>(z + 41u),
                       static_cast<float>(x + y * 10u + z * 100u + 7u));
}

[[nodiscard]] string format_log_message(string_view label, uint index, float4 value) {
    return ocarina::format("{} {} {} {} {} {}",
                           label,
                           index,
                           static_cast<int>(value.x),
                           static_cast<int>(value.y),
                           static_cast<int>(value.z),
                           static_cast<int>(value.w));
}

[[nodiscard]] string format_record_log_message(string_view label, uint index, const TestRecord &value) {
    return ocarina::format("{} {} {} {} {} {} {} {} {} {}",
                           label,
                           index,
                           static_cast<int>(value.lhs.x),
                           static_cast<int>(value.lhs.y),
                           static_cast<int>(value.lhs.z),
                           static_cast<int>(value.lhs.w),
                           static_cast<int>(value.rhs.x),
                           static_cast<int>(value.rhs.y),
                           static_cast<int>(value.rhs.z),
                           static_cast<int>(value.rhs.w));
}

[[nodiscard]] bool contains_log_message(const vector<CapturedLog> &logs, string_view expected) {
    return std::find_if(logs.begin(), logs.end(), [&](const CapturedLog &log) {
               return log.message == expected;
           }) != logs.end();
}

void capture_logs(Stream &stream, vector<CapturedLog> &logs) {
    stream << Env::printer().retrieve([&](int level, const char *message) {
        logs.emplace_back(level, string{message});
    });
}

template<typename Shader, typename... Args>
[[nodiscard]] vector<CapturedLog> run_and_capture(Stream &stream, Shader &shader, uint dispatch_size, Args &&...args) {
    vector<CapturedLog> logs;
    stream << shader(OC_FORWARD(args)...).dispatch(dispatch_size)
           << Env::printer().retrieve([&](int level, const char *message) {
               logs.emplace_back(level, string{message});
           })
           << synchronize()
           << commit();
    return logs;
}

int verify_logs(const char *label, const vector<CapturedLog> &logs, std::initializer_list<string> expected_logs) {
    int failures = 0;
    for (const auto &expected : expected_logs) {
        if (!contains_log_message(logs, expected)) {
            std::cerr << "  FAIL [" << label << "] missing printer log: " << expected << std::endl;
            ++failures;
        }
    }
    return failures;
}

void log_float4_mismatch(const char *label, uint index, float4 actual, float4 expected) {
    std::cerr << "  FAIL [" << label << "] at index " << index
              << " expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ", " << expected.w << ")"
              << " actual=(" << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w << ")"
              << std::endl;
}

void log_record_mismatch(const char *label, uint index, const TestRecord &actual, const TestRecord &expected) {
    std::cerr << "  FAIL [" << label << "] at index " << index
              << " expected_lhs=(" << expected.lhs.x << ", " << expected.lhs.y << ", " << expected.lhs.z << ", " << expected.lhs.w << ")"
              << " actual_lhs=(" << actual.lhs.x << ", " << actual.lhs.y << ", " << actual.lhs.z << ", " << actual.lhs.w << ")"
              << " expected_rhs=(" << expected.rhs.x << ", " << expected.rhs.y << ", " << expected.rhs.z << ", " << expected.rhs.w << ")"
              << " actual_rhs=(" << actual.rhs.x << ", " << actual.rhs.y << ", " << actual.rhs.z << ", " << actual.rhs.w << ")"
              << std::endl;
}

int test_bindless_buffer_read_write(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_buffer_read_write ===" << std::endl;
    int failures = 0;

    constexpr uint count = 16u;
    auto src = device.create_buffer<float4>(count, "test_bindless_buffer_read_write_src");
    auto dst = device.create_buffer<float4>(count, "test_bindless_buffer_read_write_dst");
    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_buffer_value(index);
    }
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float4 value = bindless.buffer_var<float4>(src_slot).read(index);
            Float4 transformed = value + make_float4(10.f, 20.f, 30.f, 40.f);
            bindless.buffer_var<float4>(dst_slot).write(index, transformed);
        };
    };

    auto shader = device.compile(kernel, "test_bindless_buffer_read_write");
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data());
    stream << synchronize() << commit();

    for (uint index = 0; index < count; ++index) {
        float4 expected = transform_buffer_value(host_src[index]);
        if (!equal_float4(host_dst[index], expected)) {
            log_float4_mismatch("bindless_buffer", index, host_dst[index], expected);
            ++failures;
        }
    }

    Env::printer().reset();
    Kernel print_kernel = [&](BufferVar<float4> input, Uint n) {
        Uint index = dispatch_id();
        $if(index < n && (index == 0u || index == n / 2u || index + 1u == n)) {
            Float4 value = input.read(index);
            Env::printer().info("bindless_buffer {} {} {} {} {}",
                                index,
                                value.x.cast<int>(),
                                value.y.cast<int>(),
                                value.z.cast<int>(),
                                value.w.cast<int>());
        };
    };
    auto print_shader = device.compile(print_kernel, "test_bindless_buffer_read_write_printer");
    Stream print_stream = device.create_stream();
    auto logs = run_and_capture(print_stream, print_shader, count, dst, count);
    failures += verify_logs("bindless_buffer_printer", logs, {
        format_log_message("bindless_buffer", 0u, transform_buffer_value(host_src.front())),
        format_log_message("bindless_buffer", count / 2u, transform_buffer_value(host_src[count / 2u])),
        format_log_message("bindless_buffer", count - 1u, transform_buffer_value(host_src.back()))});

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_buffer_inline_retrieve_regression(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_buffer_inline_retrieve_regression ===" << std::endl;
    int failures = 0;

    constexpr uint count = 16u;
    constexpr uint iterations = 32u;
    Kernel kernel = [&](BindlessArrayVar ba, Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Float4 value = ba.buffer_var<float4>(0u).read(index);
            Float4 transformed = value + make_float4(10.f, 20.f, 30.f, 40.f);
            ba.buffer_var<float4>(1u).write(index, transformed);
            $if(index == 0u || index + 1u == n) {
                Env::printer().info("bindless_buffer_inline {} {} {} {} {}",
                                    index,
                                    transformed.x.cast<int>(),
                                    transformed.y.cast<int>(),
                                    transformed.z.cast<int>(),
                                    transformed.w.cast<int>());
            };
        };
    };

    auto shader = device.compile(kernel, "test_bindless_buffer_inline_retrieve_regression");
    for (uint iteration = 0; iteration < iterations; ++iteration) {
        auto src = device.create_buffer<float4>(count, "test_bindless_buffer_inline_retrieve_src");
        auto dst = device.create_buffer<float4>(count, "test_bindless_buffer_inline_retrieve_dst");
        vector<float4> host_src(count);
        vector<float4> host_dst(count, make_float4(0.f));
        for (uint index = 0; index < count; ++index) {
            host_src[index] = make_buffer_value(index + iteration * count);
        }
        src.upload_immediately(host_src.data());

        BindlessArray bindless = device.create_bindless_array();
        (void)bindless.emplace(src);
        (void)bindless.emplace(dst);

        vector<CapturedLog> logs;
        Env::printer().reset();
        stream << bindless->upload_buffer_handles(false)
             << shader(bindless, count).dispatch(count)
               << dst.download(host_dst.data())
               << Env::printer().retrieve([&](int level, const char *message) {
                   logs.emplace_back(level, string{message});
               })
               << synchronize()
               << commit();

        for (uint index = 0; index < count; ++index) {
            float4 expected = transform_buffer_value(host_src[index]);
            if (!equal_float4(host_dst[index], expected)) {
                std::cerr << "  FAIL [bindless_buffer_inline_retrieve] iteration=" << iteration << std::endl;
                log_float4_mismatch("bindless_buffer_inline_retrieve", index, host_dst[index], expected);
                ++failures;
                break;
            }
        }
        if (failures != 0) {
            break;
        }

        failures += verify_logs("bindless_buffer_inline_retrieve_printer", logs, {
            format_log_message("bindless_buffer_inline", 0u, transform_buffer_value(host_src.front())),
            format_log_message("bindless_buffer_inline", count - 1u, transform_buffer_value(host_src.back()))});
        if (failures != 0) {
            std::cerr << "  FAIL [bindless_buffer_inline_retrieve] iteration=" << iteration << std::endl;
            break;
        }
    }

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_byte_buffer_read_write(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_byte_buffer_read_write ===" << std::endl;
    int failures = 0;

    constexpr uint count = 12u;
    auto src = device.create_byte_buffer(count * sizeof(float4), "test_bindless_byte_buffer_read_write_src");
    auto dst = device.create_byte_buffer(count * sizeof(float4), "test_bindless_byte_buffer_read_write_dst");
    vector<float4> host_src(count);
    vector<float4> host_dst(count, make_float4(0.f));
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_byte_buffer_value(index);
    }
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            Uint offset = index * static_cast<uint>(sizeof(float4));
            Float4 value = bindless.byte_buffer_var(src_slot).load_as<float4>(offset);
            Float4 transformed = value * 2.f + make_float4(1.f, 3.f, 5.f, 7.f);
            bindless.byte_buffer_var(dst_slot).store(offset, transformed);
        };
    };

    auto shader = device.compile(kernel, "test_bindless_byte_buffer_read_write");
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data());
    stream << synchronize() << commit();

    for (uint index = 0; index < count; ++index) {
        float4 expected = transform_byte_buffer_value(host_src[index]);
        if (!equal_float4(host_dst[index], expected)) {
            log_float4_mismatch("bindless_byte_buffer", index, host_dst[index], expected);
            ++failures;
        }
    }

    Env::printer().reset();
    Kernel print_kernel = [&](ByteBufferVar input) {
        Uint index = dispatch_id();
        Uint offset = index * static_cast<uint>(sizeof(float4));
        $if(index == 0u || index == count / 2u || index + 1u == count) {
            Float4 value = input.load_as<float4>(offset);
            Env::printer().info("byte_buffer {} {} {} {} {}",
                                index,
                                value.x.cast<int>(),
                                value.y.cast<int>(),
                                value.z.cast<int>(),
                                value.w.cast<int>());
        };
    };
    auto print_shader = device.compile(print_kernel, "test_bindless_byte_buffer_read_write_printer");
    Stream print_stream = device.create_stream();
    auto logs = run_and_capture(print_stream, print_shader, count, dst);
    failures += verify_logs("bindless_byte_buffer_printer", logs, {
        "byte_buffer 0 5 11 17 23",
        format_log_message("byte_buffer", count / 2u, transform_byte_buffer_value(host_src[count / 2u])),
        "byte_buffer 11 93 99 105 111"});

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_aos_view_read_write(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_aos_view_read_write ===" << std::endl;
    int failures = 0;

    constexpr uint count = 10u;
    auto src = device.create_byte_buffer(count * sizeof(TestRecord), "test_bindless_aos_view_read_write_src");
    auto dst = device.create_byte_buffer(count * sizeof(TestRecord), "test_bindless_aos_view_read_write_dst");
    vector<TestRecord> host_src(count);
    vector<TestRecord> host_dst(count);
    for (uint index = 0; index < count; ++index) {
        host_src[index] = make_test_record(index);
    }
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            auto src_view = bindless.byte_buffer_var(src_slot).template aos_view_var<TestRecord>();
            auto dst_view = bindless.byte_buffer_var(dst_slot).template aos_view_var<TestRecord>();
            Var<TestRecord> value = src_view.read(index);
            value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
            value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
            dst_view.write(index, value);
        };
    };

    auto shader = device.compile(kernel, "test_bindless_aos_view_read_write");
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data());
    stream << synchronize() << commit();

    for (uint index = 0; index < count; ++index) {
        TestRecord expected = transform_record(host_src[index]);
        if (!equal_record(host_dst[index], expected)) {
            log_record_mismatch("bindless_aos", index, host_dst[index], expected);
            ++failures;
        }
    }

    Env::printer().reset();
    Kernel print_kernel = [&](ByteBufferVar input, Uint n) {
        Uint index = dispatch_id();
        $if(index < n && (index == 0u || index == n / 2u || index + 1u == n)) {
            auto view = input.aos_view_var<TestRecord>();
            Var<TestRecord> value = view.read(index);
            Env::printer().info("bindless_aos {} {} {} {} {} {} {} {} {}",
                                index,
                                value.lhs.x.cast<int>(),
                                value.lhs.y.cast<int>(),
                                value.lhs.z.cast<int>(),
                                value.lhs.w.cast<int>(),
                                value.rhs.x.cast<int>(),
                                value.rhs.y.cast<int>(),
                                value.rhs.z.cast<int>(),
                                value.rhs.w.cast<int>());
        };
    };
    auto print_shader = device.compile(print_kernel, "test_bindless_aos_view_read_write_printer");
    Stream print_stream = device.create_stream();
    auto logs = run_and_capture(print_stream, print_shader, count, dst, count);
    failures += verify_logs("bindless_aos_printer", logs, {
        format_record_log_message("bindless_aos", 0u, transform_record(host_src.front())),
        format_record_log_message("bindless_aos", count / 2u, transform_record(host_src[count / 2u])),
        format_record_log_message("bindless_aos", count - 1u, transform_record(host_src.back()))});

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_soa_view_read_write(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_soa_view_read_write ===" << std::endl;
    int failures = 0;

    constexpr uint count = 10u;
    auto src = device.create_byte_buffer(count * sizeof(TestRecord), "test_bindless_soa_view_read_write_src");
    auto dst = device.create_byte_buffer(count * sizeof(TestRecord), "test_bindless_soa_view_read_write_dst");
    vector<TestRecord> host_records(count);
    for (uint index = 0; index < count; ++index) {
        host_records[index] = make_test_record(index + 20u);
    }
    vector<std::byte> host_src = pack_records_soa(host_records);
    vector<std::byte> host_dst(count * sizeof(TestRecord));
    src.upload_immediately(host_src.data());

    BindlessArray bindless = device.create_bindless_array();
    uint src_slot = bindless.emplace(src);
    uint dst_slot = bindless.emplace(dst);

    Kernel kernel = [&](Uint n) {
        Uint index = dispatch_id();
        $if(index < n) {
            auto src_view = bindless.byte_buffer_var(src_slot).template soa_view_var<TestRecord>();
            auto dst_view = bindless.byte_buffer_var(dst_slot).template soa_view_var<TestRecord>();
            Var<TestRecord> value = src_view.read(index);
            value.lhs += make_float4(2.f, 4.f, 6.f, 8.f);
            value.rhs -= make_float4(1.f, 3.f, 5.f, 7.f);
            dst_view.write(index, value);
        };
    };

    auto shader = device.compile(kernel, "test_bindless_soa_view_read_write");
    stream << bindless->upload_buffer_handles(false)
           << shader(count).dispatch(count)
           << dst.download(host_dst.data());
    stream << synchronize() << commit();

    vector<TestRecord> unpacked = unpack_records_soa(host_dst, count);
    for (uint index = 0; index < count; ++index) {
        TestRecord expected = transform_record(host_records[index]);
        if (!equal_record(unpacked[index], expected)) {
            log_record_mismatch("bindless_soa", index, unpacked[index], expected);
            ++failures;
        }
    }

    Env::printer().reset();
    Kernel print_kernel = [&](ByteBufferVar input, Uint n) {
        Uint index = dispatch_id();
        $if(index < n && (index == 0u || index == n / 2u || index + 1u == n)) {
            auto view = input.soa_view_var<TestRecord>();
            Var<TestRecord> value = view.read(index);
            Env::printer().info("bindless_soa {} {} {} {} {} {} {} {} {}",
                                index,
                                value.lhs.x.cast<int>(),
                                value.lhs.y.cast<int>(),
                                value.lhs.z.cast<int>(),
                                value.lhs.w.cast<int>(),
                                value.rhs.x.cast<int>(),
                                value.rhs.y.cast<int>(),
                                value.rhs.z.cast<int>(),
                                value.rhs.w.cast<int>());
        };
    };
    auto print_shader = device.compile(print_kernel, "test_bindless_soa_view_read_write_printer");
    Stream print_stream = device.create_stream();
    auto logs = run_and_capture(print_stream, print_shader, count, dst, count);
    failures += verify_logs("bindless_soa_printer", logs, {
        format_record_log_message("bindless_soa", 0u, transform_record(host_records.front())),
        format_record_log_message("bindless_soa", count / 2u, transform_record(host_records[count / 2u])),
        format_record_log_message("bindless_soa", count - 1u, transform_record(host_records.back()))});

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_texture2d_sampling(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_texture2d_sampling ===" << std::endl;
    int failures = 0;

    constexpr uint2 res = make_uint2(5u, 4u);
    constexpr uint count = res.x * res.y;
    vector<float4> host_texels(count);
    vector<float4> host_out(count, make_float4(0.f));
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            host_texels[pixel_index(res, x, y)] = make_texture2d_value(x, y);
        }
    }

    auto tex = device.create_texture2d(res, PixelStorage::FLOAT4, "test_bindless_texture2d");
    auto out = device.create_buffer<float4>(count, "test_bindless_texture2d_out");
    tex.upload_immediately(host_texels.data());

    BindlessArray bindless = device.create_bindless_array();
    uint tex_slot = bindless.emplace(tex);
    if (bindless.texture2d_num() != 1u || bindless.texture3d_num() != 0u) {
        std::cerr << "  FAIL [bindless_texture2d] unexpected slot counts: texture2d_num="
                  << bindless.texture2d_num() << " texture3d_num=" << bindless.texture3d_num() << std::endl;
        return 1;
    }

    Kernel kernel = [&](BufferVar<float4> output, Uint total) {
        Uint2 xy = dispatch_idx().xy();
        Uint2 dim = dispatch_dim().xy();
        Uint linear = xy.y * dim.x + xy.x;
        Float2 uv = (make_float2(cast<float>(xy.x), cast<float>(xy.y)) + 0.5f) /
                    make_float2(cast<float>(dim.x), cast<float>(dim.y));
        Float4 value = bindless.tex2d_var(tex_slot).sample(4, uv).as_vec4();
        output.write(linear, value);
    };

    auto shader = device.compile(kernel, "test_bindless_texture2d_sampling");
    stream << bindless->upload_texture2d_handles(true)
           << synchronize()
           << shader(out, count).dispatch(res)
           << out.download(host_out.data());
    stream << synchronize() << commit();

    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            uint index = static_cast<uint>(pixel_index(res, x, y));
            float4 expected = host_texels[index];
            if (!equal_float4(host_out[index], expected, 1e-4f)) {
                log_float4_mismatch("bindless_tex2d", index, host_out[index], expected);
                ++failures;
            }
        }
    }

    Env::printer().reset();
    Kernel print_kernel = [&](BufferVar<float4> input, Uint n) {
        Uint index = dispatch_id();
        $if(index < n && (index == 0u || index == n / 2u || index + 1u == n)) {
            Float4 value = input.read(index);
            Env::printer().info("bindless_tex2d {} {} {} {} {}",
                                index,
                                value.x.cast<int>(),
                                value.y.cast<int>(),
                                value.z.cast<int>(),
                                value.w.cast<int>());
        };
    };
    auto print_shader = device.compile(print_kernel, "test_bindless_texture2d_sampling_printer");
    Stream print_stream = device.create_stream();
    auto logs = run_and_capture(print_stream, print_shader, count, out, count);
    failures += verify_logs("bindless_tex2d_printer", logs, {
        format_log_message("bindless_tex2d", 0u, host_texels.front()),
        format_log_message("bindless_tex2d", count / 2u, host_texels[count / 2u]),
        format_log_message("bindless_tex2d", count - 1u, host_texels.back())});

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_bindless_texture3d_sampling(Device &device, Stream &stream) {
    std::cout << "=== test_bindless_texture3d_sampling ===" << std::endl;
    int failures = 0;

    constexpr uint3 res = make_uint3(4u, 3u, 2u);
    constexpr uint count = res.x * res.y * res.z;
    vector<float4> host_texels(count);
    vector<float4> host_out(count, make_float4(0.f));
    for (uint z = 0; z < res.z; ++z) {
        for (uint y = 0; y < res.y; ++y) {
            for (uint x = 0; x < res.x; ++x) {
                host_texels[voxel_index(res, x, y, z)] = make_texture3d_value(x, y, z);
            }
        }
    }

    auto tex = device.create_texture3d(res, PixelStorage::FLOAT4, "test_bindless_texture3d");
    auto out = device.create_buffer<float4>(count, "test_bindless_texture3d_out");
    tex.upload_immediately(host_texels.data());

    BindlessArray bindless = device.create_bindless_array();
    uint tex_slot = bindless.emplace(tex);
    if (bindless.texture3d_num() != 1u) {
        std::cerr << "  FAIL [bindless_texture3d] unexpected texture3d_num=" << bindless.texture3d_num() << std::endl;
        return 1;
    }

    Kernel kernel = [&](BufferVar<float4> output, Uint total) {
        Uint3 xyz = dispatch_idx();
        Uint3 dim = dispatch_dim();
        Uint linear = (xyz.z * dim.y + xyz.y) * dim.x + xyz.x;
        Float3 uvw = (make_float3(cast<float>(xyz.x), cast<float>(xyz.y), cast<float>(xyz.z)) + 0.5f) /
                     make_float3(cast<float>(dim.x), cast<float>(dim.y), cast<float>(dim.z));
        Float4 value = bindless.tex3d_var(tex_slot).sample(4, uvw).as_vec4();
        output.write(linear, value);
    };

    auto shader = device.compile(kernel, "test_bindless_texture3d_sampling");
    stream << bindless->upload_texture3d_handles(true)
           << synchronize()
           << shader(out, count).dispatch(res)
           << out.download(host_out.data());
    stream << synchronize() << commit();

    for (uint z = 0; z < res.z; ++z) {
        for (uint y = 0; y < res.y; ++y) {
            for (uint x = 0; x < res.x; ++x) {
                uint index = static_cast<uint>(voxel_index(res, x, y, z));
                float4 expected = host_texels[index];
                if (!equal_float4(host_out[index], expected, 1e-4f)) {
                    log_float4_mismatch("bindless_tex3d", index, host_out[index], expected);
                    ++failures;
                }
            }
        }
    }

    Env::printer().reset();
    Kernel print_kernel = [&](BufferVar<float4> input, Uint n) {
        Uint index = dispatch_id();
        $if(index < n && (index == 0u || index == n / 2u || index + 1u == n)) {
            Float4 value = input.read(index);
            Env::printer().info("bindless_tex3d {} {} {} {} {}",
                                index,
                                value.x.cast<int>(),
                                value.y.cast<int>(),
                                value.z.cast<int>(),
                                value.w.cast<int>());
        };
    };
    auto print_shader = device.compile(print_kernel, "test_bindless_texture3d_sampling_printer");
    Stream print_stream = device.create_stream();
    auto logs = run_and_capture(print_stream, print_shader, count, out, count);
    failures += verify_logs("bindless_tex3d_printer", logs, {
        format_log_message("bindless_tex3d", 0u, host_texels.front()),
        format_log_message("bindless_tex3d", count / 2u, host_texels[count / 2u]),
        format_log_message("bindless_tex3d", count - 1u, host_texels.back())});

    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

}// namespace

int main() {
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    Stream stream = device.create_stream();
    Env::printer().init(device);

    int total_failures = 0;
    total_failures += test_bindless_buffer_read_write(device, stream);
    total_failures += test_bindless_buffer_inline_retrieve_regression(device, stream);
    total_failures += test_bindless_byte_buffer_read_write(device, stream);
    total_failures += test_bindless_aos_view_read_write(device, stream);
    total_failures += test_bindless_soa_view_read_write(device, stream);
    total_failures += test_bindless_texture2d_sampling(device, stream);
    total_failures += test_bindless_texture3d_sampling(device, stream);

    if (total_failures != 0) {
        std::cerr << "[test-bindless-array] failed with " << total_failures << " issue(s)" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[test-bindless-array] all checks passed" << std::endl;
    return EXIT_SUCCESS;
}