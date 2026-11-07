#pragma once

#include <filesystem>

namespace vision::inline hotfix {

class ModuleInterface;

namespace detail {

[[nodiscard]] ModuleInterface *load_staged_module(const std::filesystem::path &path) noexcept;

}// namespace detail
}// namespace vision::inline hotfix
