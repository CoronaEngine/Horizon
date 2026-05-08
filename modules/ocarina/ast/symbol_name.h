//
// Created by Zero on 30/07/2022.
//

#pragma once

#include "variable.h"
#include "core/util/string_util.h"

namespace ocarina::detail {

template<typename T>
[[nodiscard]] auto to_string(T &&t) noexcept {
    static thread_local ocarina::array<char, 128u> s{};
    auto [iter, size] = fmt::format_to_n(s.data(), s.size(), FMT_STRING("{}"), t);
    string ret(s.data(), size);
    return ret;
}

[[nodiscard]] inline string struct_name(uint64_t hash) {
    return "structure_" + ocarina::format("{:016x}", hash);
}

[[nodiscard]] inline string func_name(uint64_t hash) {
    return "function_" + ocarina::format("{:016x}", hash);
}

[[nodiscard]] inline string kernel_name(uint64_t hash, string desc = "") {
    return "kernel_" + ocarina::move(desc) + "_" + ocarina::format("{:016x}", hash);
}

[[nodiscard]] inline string raygen_name(uint64_t hash, string desc = "") {
    return "__raygen__" + ocarina::move(desc) + "_" + ocarina::format("{:016x}", hash);
}

[[nodiscard]] inline string member_name(uint index) {
    return "m" + to_string(index);
}

[[nodiscard]] inline string_view variable_prefix(Variable::Tag tag) {
    using Tag = Variable::Tag;
    switch (tag) {
        case Tag::REFERENCE:
        case Tag::LOCAL:
            return "v";
        case Tag::DISPATCH_IDX:
            return "d_idx";
        case Tag::DISPATCH_ID:
            return "d_id";
        case Tag::DISPATCH_DIM:
            return "d_dim";
        case Tag::THREAD_IDX:
            return "t_idx";
        case Tag::THREAD_ID:
            return "t_id";
        case Tag::BLOCK_IDX:
            return "b_idx";
        case Tag::BUFFER:
            return "b";
        case Tag::BYTE_BUFFER:
            return "bb";
        case Tag::TEXTURE3D:
            return "t3d";
        case Tag::TEXTURE2D:
            return "t2d";
        case Tag::ACCEL:
            return "acc";
        case Tag::BINDLESS_ARRAY:
            return "ra";
        default:
            break;
    }
    OC_ASSERT(0);
    return "";
}

}// namespace ocarina::detail