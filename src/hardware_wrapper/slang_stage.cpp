// compile_slang_stage 的实现单独占一个 TU。
//
// 这个函数不是模板，把函数体留在 horizon.h 里的唯一代价是：公共头必须
// #include "Compiler/ShaderLanguageConverter.h"，于是 44 个包含 horizon.h 的
// TU 都要吃 slang-com-ptr.h / slang-com-helper.h。实现挪进来之后公共头只留声明。
//
// 注意：slang.h 本身仍会经 ShaderCommon.h → ComputePipelineObject.h 进入公共头，
// 这里省下的只是 COM 智能指针那一层，不是整个 Slang。

#include "horizon.h"

#include "Compiler/ShaderLanguageConverter.h"

#include <array>
#include <vector>

namespace Corona::Horizon
{
    EmbeddedShader::ShaderCodeModule compile_slang_stage(EmbeddedShader::ShaderStage stage,
                                                         EmbeddedShader::SlangModule& module)
    {
        using namespace EmbeddedShader;

        // 静态模块（离线 codegen 产出的 SlangModule）只编译一次，这里用低层 API
        // 建 session、load IR blob、link entry point；不经过运行时的条件组合路径。
        auto globalSession = ShaderLanguageConverter::getGlobalSession();
        slang::TargetDesc targetDesc {};
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = globalSession->findProfile("spirv_1_6");
        std::vector targets { targetDesc };
        std::array options = {
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::EmitSpirvDirectly,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
        };

        auto session = ShaderLanguageConverter::createSession(globalSession, targets, options);
        auto mod = ShaderLanguageConverter::loadModule(session, module);
        if (!mod)
            throw std::runtime_error("Failed to load serialized Slang module.");
        // 离线生成的 slangs 模块通常没有 [shader(...)] 注解，findEntryPointByName
        // 找不到 "main"；用 tryFindEntryPoint 让 Slang 按 stage 校验入口点。
        auto entryPoint = ShaderLanguageConverter::tryFindEntryPoint(mod, "main", stage);
        if (!entryPoint)
            throw std::runtime_error("Failed to find shader entry point in Slang module.");
        std::vector<slang::IComponentType*> components { mod.get(), entryPoint.get() };
        auto program = ShaderLanguageConverter::link(session, components);

        SlangCompileResult result;
        ShaderLanguageConverter::fillCompileResult(result, program, 0, SLANG_SPIRV, true);

        auto spirv = result.binaryTargets.find(ShaderLanguage::SpirV);
        if (spirv == result.binaryTargets.end())
            throw std::runtime_error("Slang module compilation did not produce SPIR-V.");

        auto reflection = result.reflections.find(ShaderLanguage::SpirV);
        ShaderCodeModule::ShaderResources resources;
        if (reflection != result.reflections.end())
            resources = std::move(reflection->second);

        // Slang 反射产出点分全名（如 global_ubo.field），下游 codegen/runtime 按短名查找，
        // 这里裁剪成 '.' 后的短段，与离线 codegen (tools/main.cpp) 的约定保持一致。
        for (auto& info : resources.bindInfoPool)
        {
            if (auto pos = info.variateName.find_last_of('.'); pos != std::string::npos)
                info.variateName = info.variateName.substr(pos + 1);
        }

        return EmbeddedShader::ShaderCodeModule(std::move(spirv->second), std::move(resources));
    }
}
