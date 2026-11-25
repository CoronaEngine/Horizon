#pragma once

#include "core/rtti.h"
#include "core/smart_ptr.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace vision {

using ocarina::RTTI;
using ocarina::SP;
using ocarina::UP;
using std::array;
using std::function;
using std::make_pair;
using std::make_shared;
using std::map;
using std::optional;
using std::pair;
using std::queue;
using std::set;
using std::string;
using std::string_view;
using std::unique_ptr;
using std::vector;

using uint = std::uint32_t;
namespace fs = std::filesystem;

using fmt::format;

}// namespace vision

namespace vision::inline hotfix::hotfix_detail {

[[nodiscard]] inline fs::path parent_path(fs::path path, int levels) {
    for (int i = 0; i < levels; ++i) {
        path = path.parent_path();
    }
    return path;
}

}// namespace vision::inline hotfix::hotfix_detail
