//
// Created by Zero on 30/07/2022.
//

#pragma once

#include "variable.h"
#include "core/util/string_util.h"

namespace horizon::ast::detail {
using namespace horizon::core;
using namespace horizon::math;

template<typename T>
[[nodiscard]] auto to_string(T &&t) noexcept {
    static thread_local horizon::ast::array<char, 128u> s{};
    auto [iter, size] = fmt::format_to_n(s.data(), s.size(), FMT_STRING("{}"), t);
    string ret(s.data(), size);
    return ret;
}

[[nodiscard]] inline string struct_name(uint64_t hash) {
    return "structure_" + horizon::ast::format("{:016x}", hash);
}

[[nodiscard]] inline string func_name(uint64_t hash) {
    return "function_" + horizon::ast::format("{:016x}", hash);
}

[[nodiscard]] inline string kernel_name(uint64_t hash, string desc = "") {
    return "kernel_" + horizon::ast::move(desc) + "_" + horizon::ast::format("{:016x}", hash);
}

[[nodiscard]] inline string raygen_name(uint64_t hash, string desc = "") {
    return "__raygen__" + horizon::ast::move(desc) + "_" + horizon::ast::format("{:016x}", hash);
}

[[nodiscard]] inline string member_name(uint index) {
    return "m" + to_string(index);
}

[[nodiscard]] inline string_view variable_prefix(Variable::Tag tag) {
    using Tag = Variable::Tag;
    switch (tag) {
        case Tag::Reference:
        case Tag::Local:
            return "v";
        case Tag::DispatchIdx:
            return "d_idx";
        case Tag::DispatchId:
            return "d_id";
        case Tag::DispatchDim:
            return "d_dim";
        case Tag::ThreadIdx:
            return "t_idx";
        case Tag::ThreadId:
            return "t_id";
        case Tag::BlockIdx:
            return "b_idx";
        case Tag::Buffer:
            return "b";
        case Tag::ByteBuffer:
            return "bb";
        case Tag::Texture3D:
            return "t3d";
        case Tag::Texture2D:
            return "t2d";
        case Tag::Accel:
            return "acc";
        case Tag::BindlessArray:
            return "ra";
        default:
            break;
    }
    OC_ASSERT(0);
    return "";
}

}// namespace horizon::ast::detail