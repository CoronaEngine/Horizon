//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "core/concepts.h"

namespace ocarina {
class OC_CORE_API DynamicModule : public concepts::Noncopyable {
private:
    void *handle_{};
    static ocarina::vector<fs::path> &search_path();

public:
    static void add_search_path(fs::path path) noexcept;
    static void remove_search_path(fs::path path) noexcept;
    static void clear_search_path() noexcept;
    explicit DynamicModule(const string &name) noexcept;
    OC_MAKE_MEMBER_GETTER(handle, )
    DynamicModule(fs::path path, const string &name) noexcept;
    [[nodiscard]] void *function_ptr(const string &func_name) const noexcept;
    template<typename T>
    [[nodiscard]] T function(const string &func_name) const noexcept {
        return reinterpret_cast<T>(function_ptr(func_name));
    }
};
}// namespace ocarina