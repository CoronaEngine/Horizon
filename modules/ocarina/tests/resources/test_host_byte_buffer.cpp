//
// Created by GitHub Copilot on 12/04/2026.
//

#include <iostream>

#include "core/dynamic_buffer/host_byte_buffer.h"
#include "math/basic_types.h"

using namespace ocarina;

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

struct PackedRecord {
    uint id;
    float2 uv;
    int value;
};

static_assert(std::is_trivially_copyable_v<PackedRecord>);

[[nodiscard]] bool test_default_state() {
    HostByteBuffer buffer;
    CHECK(buffer.empty());
    CHECK(buffer.size() == 0u);
    CHECK(buffer.bytes_span().size() == 0u);
    return true;
}

[[nodiscard]] bool test_resize_reserve_reset() {
    HostByteBuffer buffer;
    buffer.reserve(32u);
    CHECK(buffer.capacity() >= 32u);

    buffer.resize(12u);
    CHECK(buffer.size() == 12u);
    CHECK(!buffer.empty());

    buffer.reset(6u);
    CHECK(buffer.size() == 6u);

    buffer.clear();
    CHECK(buffer.empty());
    CHECK(buffer.size() == 0u);
    return true;
}

[[nodiscard]] bool test_copy_round_trip() {
    HostByteBuffer buffer(8u);
    const uint src[2] = {7u, 11u};
    uint dst[2] = {};

    buffer.copy_from(src, sizeof(src));
    buffer.copy_to(dst, sizeof(dst));

    CHECK(dst[0] == 7u);
    CHECK(dst[1] == 11u);
    return true;
}

[[nodiscard]] bool test_store_load_scalars() {
    HostByteBuffer buffer(sizeof(uint) + sizeof(float) + sizeof(float2));

    buffer.store<uint>(0u, 42u);
    buffer.store<float>(sizeof(uint), 1.5f);
    float2 uv = make_float2(0.25f, 0.75f);
    buffer.store<float2>(sizeof(uint) + sizeof(float), uv);

    CHECK(buffer.load<uint>(0u) == 42u);
    CHECK(buffer.load<float>(sizeof(uint)) == 1.5f);
    auto loaded_uv = buffer.load<float2>(sizeof(uint) + sizeof(float));
    CHECK(loaded_uv.x == uv.x);
    CHECK(loaded_uv.y == uv.y);
    return true;
}

[[nodiscard]] bool test_store_load_struct() {
    HostByteBuffer buffer(sizeof(PackedRecord));
    PackedRecord src{.id = 9u, .uv = make_float2(0.5f, 0.75f), .value = -3};

    buffer.store<PackedRecord>(0u, src);
    PackedRecord dst = buffer.load<PackedRecord>(0u);

    CHECK(dst.id == src.id);
    CHECK(dst.uv.x == src.uv.x);
    CHECK(dst.uv.y == src.uv.y);
    CHECK(dst.value == src.value);
    return true;
}

[[nodiscard]] bool test_offset_operations() {
    HostByteBuffer buffer(sizeof(uint) * 4u);
    buffer.store<uint>(sizeof(uint), 123u);
    buffer.store<uint>(sizeof(uint) * 2u, 456u);

    CHECK(buffer.load<uint>(sizeof(uint)) == 123u);
    CHECK(buffer.load<uint>(sizeof(uint) * 2u) == 456u);

    auto sub = buffer.subspan(sizeof(uint), sizeof(uint) * 2u);
    CHECK(sub.size() == sizeof(uint) * 2u);
    CHECK(buffer.load<uint>(sizeof(uint)) == 123u);
    return true;
}

[[nodiscard]] bool test_data_as() {
    HostByteBuffer buffer(sizeof(uint) * 3u);
    auto *typed = buffer.data_as<uint>();
    typed[0] = 5u;
    typed[1] = 6u;
    typed[2] = 7u;

    CHECK(buffer.load<uint>(0u) == 5u);
    CHECK(buffer.load<uint>(sizeof(uint)) == 6u);
    CHECK(buffer.load<uint>(sizeof(uint) * 2u) == 7u);
    return true;
}

}// namespace

int main() {
    bool passed = true;
    passed = test_default_state() && passed;
    passed = test_resize_reserve_reset() && passed;
    passed = test_copy_round_trip() && passed;
    passed = test_store_load_scalars() && passed;
    passed = test_store_load_struct() && passed;
    passed = test_offset_operations() && passed;
    passed = test_data_as() && passed;

    if (!passed) {
        std::cerr << "host byte buffer test failed" << std::endl;
        return 1;
    }
    std::cout << "host byte buffer test passed" << std::endl;
    return 0;
}