//
// Created by GitHub Copilot on 2026/4/14.
//

#include <cstring>
#include <iostream>

#include "ast/function.h"
#include "rhi/resources/shader.h"

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

template<typename T>
[[nodiscard]] T load_value(span<const std::byte> bytes, size_t offset) {
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

[[nodiscard]] bool equal_float4(float4 lhs, float4 rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

[[nodiscard]] bool equal_uint2(uint2 lhs, uint2 rhs) {
    return lhs.x == rhs.x && lhs.y == rhs.y;
}

[[nodiscard]] bool test_exact_boundary() {
    Function function(Function::Tag::KERNEL);
    ShaderArgumentPack pack(&function);

    for (uint index = 0u; index < 16u; ++index) {
        pack << make_float4(static_cast<float>(index),
                            static_cast<float>(index) + 1.f,
                            static_cast<float>(index) + 2.f,
                            static_cast<float>(index) + 3.f);
    }

    CHECK(pack.blocks().size() == 16u);
    pack.move_argument_data();

    auto bytes = pack.argument_data();
    size_t payload_size = structure_size(pack.blocks());
    size_t expected_size = mem_offset(payload_size, alignof(uint3)) + sizeof(uint3);
    CHECK(bytes.size() == expected_size);
    CHECK(equal_float4(load_value<float4>(bytes, 0u), make_float4(0.f, 1.f, 2.f, 3.f)));
    CHECK(equal_float4(load_value<float4>(bytes, 15u * sizeof(float4)),
                       make_float4(15.f, 16.f, 17.f, 18.f)));
    CHECK(pack.ptr().size() == pack.blocks().size());
    return true;
}

[[nodiscard]] bool test_dynamic_overflow_storage() {
    Function function(Function::Tag::KERNEL);
    ShaderArgumentPack pack(&function);

    for (uint index = 0u; index < 17u; ++index) {
        pack << make_float4(static_cast<float>(index) + 0.25f,
                            static_cast<float>(index) + 1.25f,
                            static_cast<float>(index) + 2.25f,
                            static_cast<float>(index) + 3.25f);
    }

    CHECK(pack.blocks().size() == 17u);
    pack.move_argument_data();

    auto bytes = pack.argument_data();
    size_t payload_size = structure_size(pack.blocks());
    size_t expected_size = mem_offset(payload_size, alignof(uint3)) + sizeof(uint3);
    CHECK(bytes.size() == expected_size);
    CHECK(equal_float4(load_value<float4>(bytes, 0u), make_float4(0.25f, 1.25f, 2.25f, 3.25f)));
    CHECK(equal_float4(load_value<float4>(bytes, 16u * sizeof(float4)),
                       make_float4(16.25f, 17.25f, 18.25f, 19.25f)));
    CHECK(pack.ptr().size() == pack.blocks().size());
    return true;
}

}// namespace

int main() {
    bool passed = true;
    passed = test_exact_boundary() && passed;
    passed = test_dynamic_overflow_storage() && passed;
    if (!passed) {
        return 1;
    }
    std::cout << "shader argument pack test passed" << std::endl;
    return 0;
}