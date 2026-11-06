//
// Created by Zero on 2024/7/26.
//

#pragma once

#include "core/stl.h"
#include "file_tool.h"
#include "rhi/context.h"
#include "core/runtime/dynamic_module.h"
#include "build_rules.h"

namespace vision::inline hotfix {

class Compiler {
public:
    using Creator = Compiler *();
    using Deleter = void(Compiler *);
    using Handle = unique_ptr<Compiler, Deleter *>;

public:
    [[nodiscard]] virtual bool compile(const CompileOptions &options) noexcept = 0;
    [[nodiscard]] virtual bool link(const LinkOptions &options, const Target &target,
                                    const string &extension_objs) noexcept = 0;
    [[nodiscard]] static fs::path cli_path() noexcept;
    [[nodiscard]] virtual fs::path installation_directory() noexcept = 0;
    [[nodiscard]] virtual bool setup_environment() { return true; }
    [[nodiscard]] static Handle create(const string &name);
    [[nodiscard]] static Handle create();
    virtual ~Compiler() = default;
};

}// namespace vision::inline hotfix
