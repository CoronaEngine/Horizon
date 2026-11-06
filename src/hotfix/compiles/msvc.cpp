//
// Created by Zero on 2024/8/6.
//

#include "hotfix/compiler.h"
#include "hotfix/export.h"
#include "hotfix/build_system.h"
#include "hotfix/cmd_process.h"

namespace vision::inline hotfix {
using namespace ocarina;

class MSVCompiler : public Compiler {
private:
    static constexpr auto bat_dir = R"(VC\Auxiliary\Build\)";
    CmdProcess cmd_process_;
    fs::path env_path_;

public:
    MSVCompiler() {
        env_path_ = installation_directory() / bat_dir;
        if (!fs::exists(FileTool::intermediate_path())) {
            fs::create_directory(FileTool::intermediate_path());
        }
        clear_directory(FileTool::intermediate_path());
    }

    [[nodiscard]] fs::path installation_directory() noexcept override {
        return parent_path(cli_path(), 8);
    }

    bool setup_environment() override {
        const auto setup = ocarina::format("\"{}\" x86_amd64", (env_path_ / "Vcvarsall.bat").string());
        const auto directory = ocarina::format("cd /d \"{}\"", BuildSystem::directory().string());
        return cmd_process_.run(setup) && cmd_process_.run("chcp 65001") && cmd_process_.run(directory);
    }

    bool compile(const CompileOptions &options) noexcept override;

    bool link(const vision::LinkOptions &options, const Target &target,
              const string &extension_objs) noexcept override;
};

bool MSVCompiler::compile(const vision::CompileOptions &options) noexcept {
    /// defines includes flags obj cpp
    static constexpr string_view cmd_template = "cl /nologo /TP {} {} {} /Fo{} -c {}";
    string cmd = ocarina::format(cmd_template, options.defines, options.includes, options.flags,
                                 options.dst_fn.string(), options.src_fn.string());
    return cmd_process_.run(cmd);
}

bool MSVCompiler::link(const vision::LinkOptions &options,
                       const Target &target,
                       const string &extension_objs) noexcept {
    static constexpr string_view cmd_template = "link {} /out:{} /implib:{} /pdb:{} /dll {} {} {}";

    auto delete_def = [](string str) {
        std::regex def_regex(R"(/DEF:[^\s]+)");
        str = std::regex_replace(str, def_regex, "");
        return str;
    };
    string link_flag = options.link_flags;
    link_flag = delete_def(link_flag);
    string link_cmd = ocarina::format(cmd_template, options.obj_files_string() + extension_objs,
                                      target.dll_path().string(),
                                      target.lib_path().string(),
                                      target.pdb_path().string(),
                                      link_flag,
                                      options.link_path,
                                      options.link_libraries);
    return cmd_process_.run(link_cmd);
}

}// namespace vision::inline hotfix

VS_EXPORT_API vision::hotfix::MSVCompiler *create() {
    return ocarina::new_with_allocator<vision::hotfix::MSVCompiler>();
}

VS_EXPORT_API void destroy(vision::hotfix::MSVCompiler *obj) {
    ocarina::delete_with_allocator(obj);
}
