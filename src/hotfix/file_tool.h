//
// Created by Zero on 2024/8/2.
//

#pragma once

#include "core/stl.h"
#include "core/util/string_util.h"
#include "core/runtime/dynamic_module.h"
#include "hotfix/support.h"

namespace vision::inline hotfix {
static constexpr const char *debug_dir = "hotfix_debug";

namespace {
using FileTime = std::chrono::time_point<std::chrono::file_clock>;
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
private:
    mutable uint build_count{0u};

public:
    string name;
    vector<InspectedFile> files;
    vector<fs::path> modified_files;
    [[nodiscard]] fs::path temp_directory() const noexcept;
    [[nodiscard]] fs::path target_path(string extension) const noexcept;
    [[nodiscard]] const DynamicModule *obtain_cur_module() const noexcept;
    [[nodiscard]] ModuleInterface *module_interface() const noexcept;
    [[nodiscard]] fs::path target_stem() const noexcept {
        return ocarina::format("module_{}", build_count);
    }
    [[nodiscard]] fs::path dll_path() const noexcept {
        return target_path(".dll");
    }
    [[nodiscard]] fs::path lib_path() const noexcept {
        return target_path(".lib");
    }
    [[nodiscard]] fs::path pdb_path() const noexcept {
        return target_path(".pdb");
    }
    void increase_count() const noexcept { ++build_count; }
    void decrease_count() const noexcept { --build_count; }
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
    using map_type = map<string_view, Target>;

private:
    map_type target_map_;
    set<string> files_;

public:
    FileTool() = default;
    void add_inspected(const fs::path &path, string_view module_name, bool recursive = true);
    void remove_inspected(const fs::path &path, bool recursive = true) noexcept;
    [[nodiscard]] vector<Target> get_modified_targets() noexcept;
    [[nodiscard]] const Target &get_target(string_view name) const noexcept {
        return target_map_.at(name);
    }
    [[nodiscard]] bool has_target(string_view target_name) noexcept {
        return target_map_.contains(target_name);
    }
    [[nodiscard]] bool has_file(const string &fn) noexcept {
        return files_.contains(fn);
    }
    [[nodiscard]] static string files_string(const vector<string> &files) noexcept {
        string ret;
        for (const auto &item : files) {
            ret += item + " ";
        }
        return ret;
    }
    [[nodiscard]] static fs::path project_path() noexcept {
        return parent_path(__FILE__, 3);
    }
    [[nodiscard]] static fs::path project_src_path() noexcept {
        return parent_path(__FILE__, 2);
    }
    [[nodiscard]] static fs::path intermediate_path() noexcept {
        return fs::current_path() / debug_dir;
    }
};
}// namespace vision::inline hotfix