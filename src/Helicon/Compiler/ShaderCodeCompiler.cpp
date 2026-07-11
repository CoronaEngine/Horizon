
#include "ShaderCodeCompiler.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include <Codegen/AST/Parser.hpp>

#include "ShaderHardcodeManager.h"
#include "ShaderLanguageConverter.h"
#include <Compiler/ShaderCommon.h>
#include <shared_mutex>
#include <ranges>

namespace EmbeddedShader
{
    std::shared_mutex threadMutex;

    namespace
    {
        ShaderCodeModule::ShaderResources reflectionForTarget(
            ShaderLanguage targetLanguage,
            const std::vector<uint32_t>* spirv,
            const std::unordered_map<ShaderLanguage, ShaderCodeModule::ShaderResources>& slangReflections)
        {
            auto slangReflection = slangReflections.find(targetLanguage);
            if (targetLanguage == ShaderLanguage::SpirV && spirv != nullptr && !spirv->empty())
            {
                auto reflection = ShaderLanguageConverter::spirvCrossReflectedBindInfo(*spirv, ShaderLanguage::HLSL);
                if (slangReflection != slangReflections.end())
                    reflection.entryPointInfoPool = slangReflection->second.entryPointInfoPool;
                return reflection;
            }

            if (slangReflection != slangReflections.end())
                return slangReflection->second;

            if (spirv != nullptr && !spirv->empty())
                return ShaderLanguageConverter::spirvCrossReflectedBindInfo(*spirv, ShaderLanguage::HLSL);

            return {};
        }
    }

    std::string enumToString(ShaderLanguage language) {
        switch (language)
        {
            case ShaderLanguage::GLSL:
                return "GLSL";
            case ShaderLanguage::HLSL:
                return "HLSL";
            case ShaderLanguage::SpirV:
                return "SpirV";
            case ShaderLanguage::Slang:
                return "Slang";
            default:break;
        }
        return "Unknown";
    }

    std::string enumToString(ShaderStage stage)
    {
        switch (stage)
        {
            case ShaderStage::VertexShader:
                return "VertexShader";
            case ShaderStage::FragmentShader:
                return "FragmentShader";
            case ShaderStage::ComputeShader:
                return "ComputeShader";
            default:break;
        }
        return "Unknown";
    }

    ShaderCodeCompiler::ShaderCodeCompiler(const std::string& shaderCode, ShaderStage inputStage,
        ShaderLanguage language, CompilerOption option, const std::source_location& sourceLocation)
    {
        sourceLocationStr = ShaderHardcodeManager::getSourceLocationString(sourceLocation);
        stage = enumToString(inputStage);
        compile(shaderCode,inputStage,language,option);
    }

    ShaderCodeModule ShaderCodeCompiler::getShaderCode(ShaderLanguage language, bool bindless) const
    {
        std::shared_lock<std::shared_mutex> lock(threadMutex);

        std::string bindlessStr = bindless ? "_Bindless" : "";
        ShaderCodeModule result;
        auto languageStr = enumToString(language);

        auto codeKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr);
        auto reflKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + "_Reflection" + bindlessStr);

#if HELICON_HAS_HARDCODE_SHADERS
        try
        {
            result.shaderCode = std::get<1>(ShaderHardcodeManager::getHardcodeShader(stage, codeKey));
            result.shaderResources = std::get<0>(ShaderHardcodeManager::getHardcodeShader(stage, reflKey));
            return result;
        }
        catch (const std::runtime_error&)
        {
            // Fall through to per-instance outputs when hardcoded shaders are stale or incomplete.
        }
#endif

        if (auto it = compiledOutputs_.find(codeKey); it != compiledOutputs_.end())
            result.shaderCode = std::get<1>(it->second);
        else
            throw std::runtime_error("Compiled shader code not found for key: " + codeKey);

        if (auto it = compiledOutputs_.find(reflKey); it != compiledOutputs_.end())
            result.shaderResources = std::get<0>(it->second);
        else
            throw std::runtime_error("Compiled shader reflection not found for key: " + reflKey);

        return result;
    }

    void ShaderCodeCompiler::compile(const std::string& shaderCode, ShaderStage inputStage, ShaderLanguage language,
        CompilerOption option) const
    {
        // Store per-instance outputs; Debug also writes hardcode shader sources for pre-generation.
        auto storeCode = [&](const auto& code, const std::string& itemName) {
            compiledOutputs_[itemName] = code;
#ifdef CABBAGE_ENGINE_DEBUG
            ShaderHardcodeManager::addTarget(code, stage, itemName);
#endif
        };
        auto storeReflection = [&](const ShaderCodeModule::ShaderResources& res, const std::string& itemName) {
            compiledOutputs_[itemName] = res;
#ifdef CABBAGE_ENGINE_DEBUG
            ShaderHardcodeManager::addTarget(res, stage, itemName);
#endif
        };
        std::string bindlessStr = Ast::Parser::getBindless() ? "_Bindless" : "";

        if (!option.branches.empty())
        {
            SlangModuleCompileArgs compileArgs;
            compileArgs.sourceLanguage = language;
            std::vector<SlangModule> declares;
            std::vector<SlangModule*> pDeclares;
            std::vector<SlangModule> trueBs;
            std::vector<SlangModule*> pTrueBs;
            std::vector<SlangModule> falseBs;
            std::vector<SlangModule*> pFalseBs;
            size_t index = option.branches.size() - 1;
            for (auto i = option.branches.rbegin(); i != option.branches.rend(); ++i)
            {
                auto& branch = *i;

                compileArgs.shaderCode = branch.declareBranch;
                compileArgs.deps.swap(pDeclares);
                auto declare = ShaderLanguageConverter::slangModuleCompiler(compileArgs);
                compileArgs.deps.swap(pDeclares);

                compileArgs.shaderCode = branch.trueBranch;
                compileArgs.deps.swap(pTrueBs);
                auto trueB = ShaderLanguageConverter::slangModuleCompiler(compileArgs);
                compileArgs.deps.swap(pTrueBs);

                compileArgs.shaderCode = branch.falseBranch;
                compileArgs.deps.swap(pFalseBs);
                auto falseB = ShaderLanguageConverter::slangModuleCompiler(compileArgs);
                compileArgs.deps.swap(pFalseBs);

                auto languageStr = "SlangModule";
                auto branchName = "Branch_" + std::to_string(index);
                // storeCode(trueB, ShaderHardcodeManager::getItemName(branchName + "_True" + sourceLocationStr, languageStr + bindlessStr));
                // storeCode(falseB, ShaderHardcodeManager::getItemName(branchName + "_False" + sourceLocationStr, languageStr + bindlessStr));

                declares.emplace_back(std::move(declare));
                pDeclares.emplace_back(&declares.back());
                trueBs.emplace_back(std::move(trueB));
                pTrueBs.emplace_back(&trueBs.back());
                falseBs.emplace_back(std::move(falseB));
                pFalseBs.emplace_back(&falseBs.back());
            }
        }

        SlangCompileArgs compileArgs;
        compileArgs.source = shaderCode;
        compileArgs.stage = inputStage;
        compileArgs.sourceLanguage = language;
        compileArgs.deps = std::move(option.slangModules);
        compileArgs.enableReflection = true;
        if (option.compileGLSL)
            compileArgs.targetLanguages.push_back(ShaderLanguage::GLSL);
        if (option.compileHLSL)
            compileArgs.targetLanguages.push_back(ShaderLanguage::HLSL);
        if (option.compileSpirV)
            compileArgs.targetLanguages.push_back(ShaderLanguage::SpirV);
        if (option.compileDXIL)
            compileArgs.targetLanguages.push_back(ShaderLanguage::DXIL);
        if (option.compileDXBC && !Ast::Parser::getBindless())
            compileArgs.targetLanguages.push_back(ShaderLanguage::DXBC);
        auto result = ShaderLanguageConverter::slangCompilerWithModules(compileArgs);
        const std::vector<uint32_t>* spirvTarget = nullptr;
        if (auto spirv = result.binaryTargets.find(ShaderLanguage::SpirV); spirv != result.binaryTargets.end())
            spirvTarget = &spirv->second;

        //string targets
        for (auto& stringTarget : result.stringTargets)
        {
            auto languageStr = enumToString(stringTarget.first);
            storeCode(stringTarget.second,ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr));
            storeReflection(reflectionForTarget(stringTarget.first, spirvTarget, result.reflections),
                            ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + "_Reflection" + bindlessStr));
        }

        //binary targets
        for (auto& binaryTargets : result.binaryTargets)
        {
            auto languageStr = enumToString(binaryTargets.first);
            storeCode(binaryTargets.second,ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr));
            storeReflection(reflectionForTarget(binaryTargets.first, spirvTarget, result.reflections),
                            ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + "_Reflection" + bindlessStr));
        }
    }
}
