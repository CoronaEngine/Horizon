//
// Created by Zero on 2024/7/26.
//

#pragma once

#include "core/stl.h"
#include "file_tool.h"
#include "rhi/context.h"
#include "core/runtime/dynamic_module.h"
#include "build_rules.h"
#include "cmd_process.h"

namespace vision::inline hotfix {

class Compiler {
public:
    using Creator = Compiler *();
    using Deleter = void(Compiler *);
    using Handle = unique_ptr<Compiler, Deleter *>;

public:
    virtual void compile(const CompileOptions &options) noexcept = 0;
    virtual void link(const LinkOptions &options, const Target &target,
                      const string &extension_objs, const CmdProcess::callback_t &callback) noexcept = 0;
    [[nodiscard]] static fs::path cli_path() noexcept;
    [[nodiscard]] virtual fs::path installation_directory() noexcept = 0;
    virtual void setup_environment() const {}
    [[nodiscard]] static Handle create(const string &name);
    [[nodiscard]] virtual bool is_working() const noexcept { return false; }
    [[nodiscard]] static Handle create();
    virtual ~Compiler() = default;
};

}// namespace vision::inline hotfix