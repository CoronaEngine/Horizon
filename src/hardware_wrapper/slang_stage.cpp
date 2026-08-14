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

namespace Corona::Horizon
{
    EmbeddedShader::ShaderCodeModule compile_slang_stage(EmbeddedShader::ShaderStage stage,
                                                         EmbeddedShader::SlangModule& module,
                                                         EmbeddedShader::CompilerOption compiler_option)
    {
        EmbeddedShader::SlangCompileArgs2 args;
        args.sourceLanguage = EmbeddedShader::ShaderLanguage::Slang;
        args.targetLanguages = { EmbeddedShader::ShaderLanguage::SpirV };
        args.stage = stage;
        args.module = &module;
        args.deps = std::move(compiler_option.slangModules);
        args.enableReflection = true;

        EmbeddedShader::SlangCompileResult result =
            EmbeddedShader::ShaderLanguageConverter::slangCompilerWithModules(args);
        auto spirv = result.binaryTargets.find(EmbeddedShader::ShaderLanguage::SpirV);
        if (spirv == result.binaryTargets.end())
            throw std::runtime_error("Slang module compilation did not produce SPIR-V.");

        auto reflection = result.reflections.find(EmbeddedShader::ShaderLanguage::SpirV);
        EmbeddedShader::ShaderCodeModule::ShaderResources resources;
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
