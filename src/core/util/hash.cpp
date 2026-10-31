//
// Created by Zero on 30/04/2022.
//

#include "hash.h"
#include "core/stl.h"
#include "fmt/core.h"

namespace ocarina {

string_view hash_to_string(uint64_t hash) noexcept {
    static thread_local ocarina::array<char, 16u> temp;
    fmt::format_to_n(temp.data(), temp.size(), "{:016X}", hash);
    return string_view{temp.data(), temp.size()};
}

}// namespace ocarina