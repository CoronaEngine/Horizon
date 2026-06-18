//
// Created by Zero on 2024/7/31.
//

#include "module_interface.h"
#include "hotfix/export.h"
#include "object.h"

namespace vision::inline hotfix {
using namespace ocarina;

ModuleInterface::ModuleInterface() = default;

ModuleInterface &ModuleInterface::instance() noexcept {
    static ModuleInterface s_module_interface;
    return s_module_interface;
}

void ModuleInterface::add_constructor(SP<const IObjectConstructor> constructor) noexcept {
    string_view name = constructor->class_name();
    constructor_map_.insert(make_pair(name, ocarina::move(constructor)));
}

const IObjectConstructor *ModuleInterface::constructor(const std::string &cls_name) const noexcept {
    OC_ASSERT(constructor_map_.contains(cls_name));
    return constructor_map_.at(cls_name).get();
}

void ModuleInterface::update(SP<const vision::IObjectConstructor> constructor) noexcept {
    string key(constructor->class_name());
    if (constructor_map_.contains(key)) {
        constructor_map_.erase(key);
    }
    constructor_map_.insert(make_pair(key, ocarina::move(constructor)));
}

void ModuleInterface::update_constructors(const ModuleInterface *other) noexcept {
    for (const auto &item : other->constructor_map_) {
        update(item.second);
    }
}

vector<const IObjectConstructor *> ModuleInterface::constructors() const noexcept {
    vector<const IObjectConstructor *> ret;
    for (const auto &item : constructor_map_) {
        ret.push_back(item.second.get());
    }
    return ret;
}

string_view ModuleInterface::src_path() noexcept {
    return __FILE__;
}

ModuleInterface::~ModuleInterface() {
    cout << "ModuleInterface  exit" << endl;
}

}// namespace vision::inline hotfix

VS_EXPORT_API vision::ModuleInterface *module_interface() {
    return &vision::ModuleInterface::instance();
}
