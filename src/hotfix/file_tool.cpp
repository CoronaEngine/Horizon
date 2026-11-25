//
// Created by Zero on 2024/8/2.
//

#include "file_tool.h"
#include "module_interface.h"
#include "staged_module.h"

#include <algorithm>

namespace vision::inline hotfix {

ModuleInterface *Target::module_interface() const noexcept {
    if (!module_path.empty()) {
        return detail::load_staged_module(module_path);
    }
    return nullptr;
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
        target_map_.emplace(module_name, std::move(target));
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
    target_map_.emplace(module_name, std::move(target));
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
    for (auto &entry : target_map_) {
        auto &target = entry.second;
        Target candidate = target;
        for (auto &file : candidate.files) {
            const auto write_time = modification_time(file.path);
            if (write_time != file.write_time) {
                file.write_time = write_time;
                candidate.modified_files.push_back(file.path);
            }
        }
        if (!candidate.modified_files.empty()) {
            ret.push_back(std::move(candidate));
        }
    }
    return ret;
}

void FileTool::acknowledge_build(const Target &target) noexcept {
    auto registered = target_map_.find(target.name);
    if (registered == target_map_.end()) {
        return;
    }
    for (auto &file : registered->second.files) {
        const auto snapshot = std::ranges::find(target.files, file.path, &InspectedFile::path);
        if (snapshot != target.files.end()) {
            file.write_time = snapshot->write_time;
        }
    }
}
}// namespace vision::inline hotfix
