//
// Created by Zero on 2024/8/13.
//

#include "hotfix/export.h"
#include "hotfix/build_rules.h"
#include "hotfix/hotfix.h"

namespace vision::inline hotfix {
using namespace ocarina;

class NinjaParser : public BuildRules {
public:
    NinjaParser();
    void extract_compile_cmd(const std::string_view *lines);
    void extract_link_cmd(const std::string_view *lines);
    void parse(const std::string &content) override;
};

NinjaParser::NinjaParser() {
    std::ifstream fst;
    fs::path fn = fs::current_path().parent_path() / "build.ninja";
    string content = from_file(fn);
    parse(content);
}

namespace {
[[nodiscard]] string_view extract_args(const string_view &line,
                                       string arg) {
    arg += " = ";
    auto len = arg.length();
    auto index = line.find(arg);
    index += len;
    return line.substr(index);
};
}// namespace

void NinjaParser::extract_compile_cmd(const std::string_view *lines) {

    auto extract_src = [&] {
        string_view line = lines[0];
        constexpr auto compiler_token = "CXX_COMPILER__";
        auto index = line.find(compiler_token);
        uint start = index + strlen(compiler_token);
        auto space_index = line.find(' ', start);
        constexpr auto next_token = "||";
        auto next_token_index = line.find(next_token, space_index);
        auto ret = string(line.substr(space_index + 1, next_token_index - space_index - 2));
        ret = string_replace(ret, "$", "");
        return ret;
    };
    CompileOptions options;
    options.src_fn = extract_src();

    FileTool &tool = HotfixSystem::instance().file_tool();

    auto extract_dst = [&] {
        string_view line = lines[0];
        constexpr auto obj_token = ".cpp.obj";
        constexpr auto build_token = "build ";
        auto index = line.find(obj_token, 0);
        CompileOptions options;
        uint size_obj_token = strlen(obj_token);
        uint size_build_token = strlen(build_token);
        return line.substr(size_build_token, index + size_obj_token - size_build_token);
    };

    options.dst_fn = extract_dst();

    if (options.src_fn == ModuleInterface::src_path()) {
        cpp_to_obj_.insert(make_pair(ModuleInterface::src_path(), options.dst_fn.string()));
    }

#define NINJA_PARSE(field, MACRO)                  \
    if (string_contains(line, MACRO)) {            \
        options.field = extract_args(line, MACRO); \
    } else

    uint i = 0;
    string_view line;
    do {
        line = lines[i++];
        NINJA_PARSE(defines, "DEFINES")
        NINJA_PARSE(flags, "FLAGS")
        NINJA_PARSE(includes, "INCLUDES")
        {}
    } while (!line.empty() || i < 10);
    compile_map_.insert(make_pair(options.src_fn.string(), options));
}

namespace {
auto extract_file_paths(const std::string &input) {
    std::vector<fs::path> results;

    std::regex path_regex(R"((?:(?:[a-zA-Z]:[\\/]|\\\\|/)?(?:[\w.-]+[\\/])*[\w.-]+\.[\w]+))");
    std::sregex_iterator it(input.begin(), input.end(), path_regex);
    std::sregex_iterator end;

    while (it != end) {
        results.emplace_back(it->str());
        ++it;
    }
    return results;
}
}// namespace

void NinjaParser::extract_link_cmd(const std::string_view *lines) {
    LinkOptions options;
    auto extract_target = [&] {
        auto lst = extract_file_paths(string(lines[0]));
        return lst[0];
    };
    options.target_file = extract_target();

    auto &tool = HotfixSystem::instance().file_tool();

    auto extract_files = [&] {
        auto lst = extract_file_paths(string(lines[2]));
        auto stem = options.target_file.stem();
        for (const fs::path &p : lst) {
            if (p.stem() == stem) {
                continue;
            }
            if (p.extension() == ".obj") {
                options.obj_files.push_back(p);
            } else if (p.extension() == ".lib" || p.extension() == ".dll") {
                options.all_libraries.push_back(p);
            }
        }
    };

    uint i = 0;
    string_view line;
    do {
        line = lines[i++];
        NINJA_PARSE(compile_flags, "LANGUAGE_COMPILE_FLAGS")
        NINJA_PARSE(link_flags, "LINK_FLAGS")
        NINJA_PARSE(target_implib, "TARGET_IMPLIB")
        NINJA_PARSE(target_pdb, "TARGET_PDB")
        NINJA_PARSE(pre_link, "PRE_LINK")
        NINJA_PARSE(link_libraries, "LINK_LIBRARIES")
        {}
    } while (!line.empty() || i < 10);
#undef NINJA_PARSE
    extract_files();
    link_map_.insert(make_pair(options.target_file.string(), options));
}

void NinjaParser::parse(const std::string &content) {
    auto lines = string_split(content, '\n');

    auto is_compile_cmd = [&](uint i) {
        string_view &line = lines[i];
        bool cond0 = line.starts_with("build");
        if (!cond0) {
            return false;
        }
        for(uint j = 1; j < 4; ++j) {
            if (lines[i + j].starts_with("  DEFINES") && cond0) {
                return true;
            }
        }
        return false;
    };

    for (int i = 0; i < lines.size(); ++i) {
        string_view &line = lines[i];
        if (is_compile_cmd(i)) {
            extract_compile_cmd(addressof(line));
        } else if (line.starts_with("# Link the ")) {
            extract_link_cmd(addressof(line));
        }
    }
}

}// namespace vision::inline hotfix

VS_EXPORT_API vision::hotfix::BuildRules *create() {
    return ocarina::new_with_allocator<vision::hotfix::NinjaParser>();
}

VS_EXPORT_API void destroy(vision::hotfix::BuildRules *obj) {
    ocarina::delete_with_allocator(obj);
}