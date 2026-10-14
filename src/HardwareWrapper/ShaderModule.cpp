#include "Horizon_old.h"

#include "Compiler/ShaderLanguageConverter.h"
#include <stdexcept>

EmbeddedShader::ShaderCodeModule compileLegacyShaderModule(
    EmbeddedShader::ShaderStage stage, const EmbeddedShader::SlangModule& module)
{
    auto input = module;
    EmbeddedShader::SlangCompileArgs2 args;
    args.sourceLanguage = EmbeddedShader::ShaderLanguage::Slang;
    args.targetLanguages = {EmbeddedShader::ShaderLanguage::SpirV};
    args.stage = stage;
    args.module = &input;
    args.enableReflection = true;

    auto result = EmbeddedShader::ShaderLanguageConverter::slangCompilerWithModules(args);
    auto binary = result.binaryTargets.find(EmbeddedShader::ShaderLanguage::SpirV);
    auto reflection = result.reflections.find(EmbeddedShader::ShaderLanguage::SpirV);
    if (binary == result.binaryTargets.end() || reflection == result.reflections.end())
        throw std::runtime_error("Legacy pipeline requires SPIR-V and Slang reflection.");

    for (auto& info : reflection->second.bindInfoPool)
    {
        if (auto pos = info.variateName.find_last_of('.'); pos != std::string::npos)
            info.variateName = info.variateName.substr(pos + 1);
    }
    return {std::move(binary->second), std::move(reflection->second)};
}
