//
// Created by Zero on 06/06/2022.
//

#include "dynamic_module.h"
#include "core/runtime/platform.h"
#include "core/util/logging.h"

namespace ocarina {

ocarina::vector<fs::path> &DynamicModule::search_path() {
    static ocarina::vector<fs::path> ret;
    return ret;
}

void DynamicModule::add_search_path(fs::path path) noexcept{
    path = fs::canonical(path);
    if (std::find(search_path().begin(), search_path().end(), path) == search_path().end()) {
        search_path().push_back(path);
    }
}

void DynamicModule::remove_search_path(fs::path path) noexcept{
    path = fs::canonical(path);
    auto iter = std::find(search_path().begin(), search_path().end(), path);
    if (iter != search_path().end()) {
        search_path().erase(iter);
    }
}

void DynamicModule::clear_search_path() noexcept {
    search_path().clear();
}

DynamicModule::DynamicModule(const string &name) noexcept {
    if (fs::path(name).is_absolute()) {
        handle_ = dynamic_module_load(name);
        if (handle_) {
            OC_DEBUG_FORMAT_WITH_LOCATION("load {} success", name);
            return;
        }
    } else {
        for (const auto &path : search_path()) {
            handle_ = dynamic_module_load(path / name);
            if (handle_) {
                OC_DEBUG_FORMAT_WITH_LOCATION("load {} success", (path/ name).string());
                return;
            }
        }
    }
    OC_ERROR_FORMAT("load {} fail!", name);
}

DynamicModule::DynamicModule(fs::path path, const string &name) noexcept {
    path = fs::canonical(path);
    handle_ = dynamic_module_load(path / name);
    if (handle_) {
        OC_INFO_FORMAT_WITH_LOCATION("load {} in {}", name, path.string());
    } else {
        OC_ERROR_FORMAT("load {} fail in", (path / name).string());
    }
}
void *DynamicModule::function_ptr(const string &func_name) const noexcept {
    return dynamic_module_find_symbol(const_cast<void*>(handle_), func_name);
}

}// namespace ocarina