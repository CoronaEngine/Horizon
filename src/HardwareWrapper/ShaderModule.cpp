#include "Horizon_old.h"

#include "Compiler/ShaderLanguageConverter.h"
#include <array>
#include <stdexcept>
#include <vector>

EmbeddedShader::ShaderCodeModule compileLegacyShaderModule(
    EmbeddedShader::ShaderStage stage, const EmbeddedShader::SlangModule& module)
{
    using EmbeddedShader::ShaderLanguageConverter;

    auto globalSession = ShaderLanguageConverter::getGlobalSession();
    slang::TargetDesc targetDesc {};
    targetDesc.format = SLANG_SPIRV;
    targetDesc.profile = globalSession->findProfile("spirv_1_6");
    std::array targets { targetDesc };
    std::array options {
        slang::CompilerOptionEntry{
            slang::CompilerOptionName::EmitSpirvDirectly,
            {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
        },
    };

    auto session = ShaderLanguageConverter::createSession(globalSession, targets, options);
    auto mod = ShaderLanguageConverter::loadModule(session, module);
    if (!mod)
        throw std::runtime_error("Failed to load serialized Slang module.");
    // Offline modules may omit [shader(...)], so validate main against the requested stage.
    auto entryPoint = ShaderLanguageConverter::tryFindEntryPoint(mod, "main", stage);
    if (!entryPoint)
        throw std::runtime_error("Failed to find shader entry point in Slang module.");
    std::vector<slang::IComponentType*> components { mod.get(), entryPoint.get() };
    auto program = ShaderLanguageConverter::link(session, components);

    EmbeddedShader::SlangCompileResult result;
    ShaderLanguageConverter::fillCompileResult(result, program, 0, SLANG_SPIRV, true);
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
