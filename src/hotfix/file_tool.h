//
// Created by Zero on 2024/8/2.
//

#pragma once

#include "hotfix/support.h"

#include <chrono>
#include <stdexcept>

namespace vision::inline hotfix {
namespace {
using FileTime = fs::file_time_type;
inline FileTime modification_time(const fs::path &file_path) {
    if (!fs::exists(file_path) || !fs::is_regular_file(file_path)) {
        throw std::runtime_error("File does not exist or is not a regular file.");
    }
    FileTime f_time = fs::last_write_time(file_path);
    return f_time;
}
}// namespace

struct InspectedFile {
public:
    fs::path path{};
    FileTime write_time{};
    InspectedFile() = default;
    explicit InspectedFile(const fs::path &p)
        : path(p), write_time(modification_time(p)) {}
};

class ModuleInterface;

struct Target {
    string name;
    fs::path module_path; // Versioned copy supplied by a build backend.
    vector<InspectedFile> files;
    vector<fs::path> modified_files;
    [[nodiscard]] ModuleInterface *module_interface() const noexcept;
};

class IObjectConstructor;

struct Version {
    vector<pair<bool, Target>> targets;
    vector<const IObjectConstructor *> constructors;
    void merge_constructors(const vector<const IObjectConstructor *> &input) noexcept;
    void merge_constructor(const IObjectConstructor *input) noexcept;
};

class FileTool {
public:
    /// key: name, value : Target
    using map_type = map<string, Target, std::less<>>;

private:
    map_type target_map_;
    set<string> files_;

public:
    FileTool() = default;
    void add_inspected(const fs::path &path, string_view module_name, bool recursive = true);
    void remove_inspected(const fs::path &path, bool recursive = true) noexcept;
    [[nodiscard]] vector<Target> get_modified_targets() noexcept;
    void acknowledge_build(const Target &target) noexcept;
    [[nodiscard]] const Target &get_target(string_view name) const noexcept {
        return target_map_.at(string{name});
    }
    [[nodiscard]] bool has_target(string_view target_name) noexcept {
        return target_map_.contains(target_name);
    }
    [[nodiscard]] bool has_file(const string &fn) noexcept {
        return files_.contains(fn);
    }
};
}// namespace vision::inline hotfix
