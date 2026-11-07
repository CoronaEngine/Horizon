#pragma once

#include "core/rtti.h"

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

#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
#include "core/concepts.h"
#include "core/concurrency/thread_safety.h"
#include "core/runtime/dynamic_module.h"
#include "core/util/hash.h"
#include "core/util/string_util.h"
#include "dsl/types/polymorphic.h"
#include "rhi/context.h"
#else
#include <fmt/format.h>
#endif

namespace vision {

using ocarina::RTTI;
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

#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
using ocarina::SP;
using ocarina::UP;
#else
template<typename T>
using SP = std::shared_ptr<T>;

template<typename... Ts>
using UP = std::unique_ptr<Ts...>;
#endif

using uint = std::uint32_t;
namespace fs = std::filesystem;

#if defined(HORIZON_HOTFIX_LEGACY_BUILD)
using ocarina::DynamicModule;
using ocarina::format;
using ocarina::Hashable;
using ocarina::parent_path;
using ocarina::Polymorphic;
using ocarina::ptr_t;
using ocarina::RHIContext;
using ocarina::thread_safety;
using ocarina::tuple;

namespace concepts = ocarina::concepts;
#else
using fmt::format;
using std::tuple;

[[nodiscard]] inline fs::path parent_path(fs::path path, int levels) {
    for (int i = 0; i < levels; ++i) {
        path = path.parent_path();
    }
    return path;
}
#endif

}// namespace vision
