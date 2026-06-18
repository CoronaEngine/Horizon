//
// Created by Zero on 2024/8/2.
//

#include "file_tool.h"
#include <windows.h>
#include "rhi/context.h"
#include "module_interface.h"

namespace vision::inline hotfix {

fs::path Target::temp_directory() const noexcept {
    return FileTool::intermediate_path() / fs::path(name).stem();
}

fs::path Target::target_path(std::string extension) const noexcept {
    return temp_directory() / (target_stem().string() + std::move(extension));
}

const DynamicModule *Target::obtain_cur_module() const noexcept {
    return RHIContext::instance().obtain_module(dll_path().string());
}

ModuleInterface *Target::module_interface() const noexcept {
    const DynamicModule *module = obtain_cur_module();
    auto creator = module->function<ModuleInterface::creator_t *>("module_interface");
    return creator();
}

void Version::merge_constructor(const vision::IObjectConstructor *input) noexcept {
    for (const IObjectConstructor *item : constructors) {
        if (item->class_name() == input->class_name()) {
            return ;
        }
    }
    constructors.push_back(input);
}

void Version::merge_constructors(const vector<const vision::IObjectConstructor *> &input) noexcept {
    for (const IObjectConstructor *item : input) {
        merge_constructor(item);
    }
}

void FileTool::add_inspected(const fs::path &path, string_view module_name, bool recursive) {
    if (target_map_.contains(module_name) || !fs::exists(path)) {
        return;
    }
    auto is_directory = fs::is_directory(path);
    recursive = is_directory && recursive;

    auto add_file = [this](Target &module, const InspectedFile &inspected) {
        if (inspected.path.extension() != ".cpp")
            return;
        module.files.push_back(inspected);
        files_.insert(inspected.path.string());
    };

    Target target;
    target.name = module_name;
    if (!is_directory) {
        InspectedFile inspected(path);
        add_file(target, inspected);
        target_map_.insert(std::make_pair(module_name, target));
        return;
    }

    auto func = [&](const fs::directory_entry &entry) {
        if (entry.exists() && entry.is_regular_file()) {
            auto f = InspectedFile(entry.path());
            add_file(target, f);
        }
    };

    if (recursive) {
        for (const auto &entry : fs::recursive_directory_iterator(path)) {
            func(entry);
        }
    } else {
        for (const auto &entry : fs::directory_iterator(path)) {
            func(entry);
        }
    }
    target_map_.insert(std::make_pair(module_name, target));
}

void FileTool::remove_inspected(const fs::path &path, bool recursive) noexcept {
    string key = path.string();
    if (!target_map_.contains(key)) {
        return;
    }
    target_map_.erase(key);
}

vector<Target> FileTool::get_modified_targets() noexcept {
    vector<Target> ret;

    auto is_modified = [&](Target &target) {
        bool modified = false;
        target.modified_files.clear();
        std::for_each(target.files.begin(), target.files.end(), [&](InspectedFile &file) {
            FileTime write_time = modification_time(file.path);
            if (write_time > file.write_time) {
                file.write_time = write_time;
                target.modified_files.push_back(file.path);
                modified = true;
            }
        });
        return modified;
    };

    for (auto &it : target_map_) {
        const string_view &key = it.first;
        Target &target = it.second;
        if (is_modified(target)) {
            target.increase_count();
            ret.push_back(target);
        }
        target.modified_files.clear();
    }
    return ret;
}
}// namespace vision::inline hotfix