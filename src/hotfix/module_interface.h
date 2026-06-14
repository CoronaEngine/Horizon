//
// Created by Zero on 2024/7/31.
//

#pragma once

#include "core/stl.h"
#include "hotfix/export.h"
#include "object.h"

namespace vision::inline hotfix {

class ModuleInterface {
public:
    using creator_t = ModuleInterface *();

private:
    ModuleInterface();
    ~ModuleInterface();

private:
    map<string, SP<const IObjectConstructor>> constructor_map_;

public:
    ModuleInterface(const ModuleInterface &) = delete;
    ModuleInterface(ModuleInterface &&) = delete;
    ModuleInterface operator=(const ModuleInterface &) = delete;
    [[nodiscard]] static string_view src_path() noexcept;
    ModuleInterface operator=(ModuleInterface &&) = delete;
    [[nodiscard]] static ModuleInterface &instance() noexcept;
    void add_constructor(SP<const IObjectConstructor> constructor) noexcept;
    [[nodiscard]] const IObjectConstructor *constructor(const string &cls_name) const noexcept;
    void update(SP<const IObjectConstructor> constructor) noexcept;
    void update_constructors(const ModuleInterface *other) noexcept;
    [[nodiscard]] vector<const IObjectConstructor *> constructors() const noexcept;
    template<typename... Args>
    requires concepts::all_string_viewable<Args...>
    [[nodiscard]] vector<const IObjectConstructor *> constructors(Args &&...args) const noexcept {
        constexpr array<string_view, sizeof...(Args)> paths = {string_view{args}...};
        vector<const IObjectConstructor *> ret;
        for (const auto &filename : paths) {
            for (const auto &item : constructor_map_) {
                const IObjectConstructor *ptr = item.second.get();
                if (ptr->filename() == filename) {
                    ret.push_back(ptr);
                }
            }
        }
        return ret;
    }
    template<typename T>
    requires requires { string{} == T{}; }
    [[nodiscard]] vector<const IObjectConstructor *> constructors(const vector<T> &paths) const noexcept {
        vector<const IObjectConstructor *> ret;
        for (const auto &filename : paths) {
            for (const auto &item : constructor_map_) {
                const IObjectConstructor *ptr = item.second.get();
                if (ptr->filename() == filename) {
                    ret.push_back(ptr);
                }
            }
        }
        return ret;
    }
    template<typename T>
    [[nodiscard]] T *construct_raw(const char *type_name = nullptr) const noexcept {
        type_name = type_name ? type_name : type_string<T>();
        return constructor(type_name)->template construct_raw<T>();
    }
    template<typename T>
    [[nodiscard]] SP<T> construct_shared(const char *type_name = nullptr) const noexcept {
        type_name = type_name ? type_name : type_string<T>();
        return constructor(type_name)->template construct_shared<T>();
    }
    template<typename T>
    [[nodiscard]] UP<T> construct_unique(const char *type_name = nullptr) const noexcept {
        type_name = type_name ? type_name : type_string<T>();
        return constructor(type_name)->template construct_unique<T>();
    }
};

}// namespace vision::inline hotfix