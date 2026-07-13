
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

        ShaderCodeModule result;
        std::string codeKey;
        std::string reflKey;
        if (!conditions.empty())
        {
            codeKey = getCurrentCombinationKey(language, bindless, false);
            reflKey = getCurrentCombinationKey(language, bindless, true);
        }
        else
        {
            std::string bindlessStr = bindless ? "_Bindless" : "";
            auto languageStr = enumToString(language);
            codeKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr);
            reflKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + "_Reflection" + bindlessStr);
        }



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
        CompilerOption option)
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

        std::vector<SlangModule> declares;
        std::vector<SlangModule*> pDeclares;
        std::vector<SlangModule> trueBs;
        std::vector<SlangModule> falseBs;
        SlangModule typeHeaderModule;

        SlangCompileResult result;
        if (!option.branches.empty())
        {
            auto languageStr = "SlangModule";

            std::vector<SlangModule*> pTrueBs;
            std::vector<SlangModule*> pFalseBs;
            SlangModuleCompileArgs compileArgs;
            compileArgs.sourceLanguage = language;
            compileArgs.shaderCode = option.typeHeader;
            compileArgs.moduleName = "type_header";
            typeHeaderModule = ShaderLanguageConverter::slangModuleCompiler(compileArgs);
            option.slangModules.push_back(&typeHeaderModule);

            size_t index = option.branches.size() - 1;
            conditions.resize(option.branches.size());
            for (auto i = option.branches.rbegin(); i != option.branches.rend(); ++i)
            {
                auto& branch = *i;
                compileArgs.moduleName = "branch_" + std::to_string(index);

                compileArgs.shaderCode = branch.declareBranch;
                compileArgs.deps = option.slangModules;
                compileArgs.deps.insert(compileArgs.deps.end(),pDeclares.begin(), pDeclares.end());
                auto declare = ShaderLanguageConverter::slangModuleCompiler(compileArgs);

                compileArgs.shaderCode = branch.trueBranch;
                compileArgs.deps = option.slangModules;
                compileArgs.deps.insert(compileArgs.deps.end(),pTrueBs.begin(), pTrueBs.end());
                auto trueB = ShaderLanguageConverter::slangModuleCompiler(compileArgs);

                compileArgs.shaderCode = branch.falseBranch;
                compileArgs.deps = option.slangModules;
                compileArgs.deps.insert(compileArgs.deps.end(),pFalseBs.begin(), pFalseBs.end());
                auto falseB = ShaderLanguageConverter::slangModuleCompiler(compileArgs);

                auto branchName = "Branch_" + std::to_string(index);

                storeCode(trueB, ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + branchName + "_True"));
                storeCode(falseB, ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + branchName + "_False"));

                declares.emplace_back(std::move(declare));
                trueBs.emplace_back(std::move(trueB));
                falseBs.emplace_back(std::move(falseB));

                pDeclares.resize(declares.size());
                pTrueBs.resize(trueBs.size());
                pFalseBs.resize(falseBs.size());

                for (size_t i = 0; i < declares.size(); ++i)
                {
                    pDeclares[i] = &declares[i];
                    pTrueBs[i] = &trueBs[i];
                    pFalseBs[i] = &falseBs[i];
                }

                conditions[index] = branch.conditionDetector;

                --index;
            }

            compileArgs.shaderCode = shaderCode;
            compileArgs.moduleName = "";
            compileArgs.deps = option.slangModules;
            compileArgs.deps.insert(compileArgs.deps.end(),pDeclares.begin(), pDeclares.end());
            auto core = ShaderLanguageConverter::slangModuleCompiler(compileArgs);
            storeCode(core, ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + "_Branch_Core"));

            SlangCompileArgs2 compileArgs2;
            compileArgs2.module = &core;
            compileArgs2.stage = inputStage;
            compileArgs2.sourceLanguage = language;
            compileArgs2.deps.swap(option.slangModules);

            //branches
            auto branches = getCurrentBranchModules(option.enableBindless);
            compileArgs2.deps.insert(compileArgs2.deps.end(),branches.begin(), branches.end());

            compileArgs2.enableReflection = true;
            if (option.compileGLSL)
                compileArgs2.targetLanguages.push_back(ShaderLanguage::GLSL);
            if (option.compileHLSL)
                compileArgs2.targetLanguages.push_back(ShaderLanguage::HLSL);
            if (option.compileSpirV)
                compileArgs2.targetLanguages.push_back(ShaderLanguage::SpirV);
            if (option.compileDXIL)
                compileArgs2.targetLanguages.push_back(ShaderLanguage::DXIL);
            if (option.compileDXBC && !Ast::Parser::getBindless())
                compileArgs2.targetLanguages.push_back(ShaderLanguage::DXBC);
            result = ShaderLanguageConverter::slangCompilerWithModules(compileArgs2);
        }
        else
        {
            SlangCompileArgs compileArgs;
            compileArgs.source = shaderCode;
            compileArgs.stage = inputStage;
            compileArgs.sourceLanguage = language;
            compileArgs.deps.swap(option.slangModules);
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
            result = ShaderLanguageConverter::slangCompilerWithModules(compileArgs);
        }

        const std::vector<uint32_t>* spirvTarget = nullptr;
        if (auto spirv = result.binaryTargets.find(ShaderLanguage::SpirV); spirv != result.binaryTargets.end())
            spirvTarget = &spirv->second;

        std::string codeKey;
        std::string reflKey;

        //string targets
        for (auto& stringTarget : result.stringTargets)
        {
            if (!conditions.empty())
            {
                codeKey = getCurrentCombinationKey(stringTarget.first, option.enableBindless, false);
                reflKey = getCurrentCombinationKey(stringTarget.first, option.enableBindless, true);
            }
            else
            {
                auto languageStr = enumToString(stringTarget.first);
                codeKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr);
                reflKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + "_Reflection" + bindlessStr);
            }

            storeCode(stringTarget.second,codeKey);
            storeReflection(reflectionForTarget(stringTarget.first, spirvTarget, result.reflections), reflKey);
        }

        //binary targets
        for (auto& binaryTarget : result.binaryTargets)
        {
            if (!conditions.empty())
            {
                codeKey = getCurrentCombinationKey(binaryTarget.first, option.enableBindless, false);
                reflKey = getCurrentCombinationKey(binaryTarget.first, option.enableBindless, true);
            }
            else
            {
                auto languageStr = enumToString(binaryTarget.first);
                codeKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr);
                reflKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + "_Reflection" + bindlessStr);
            }
            storeCode(binaryTarget.second,codeKey);
            storeReflection(reflectionForTarget(binaryTarget.first, spirvTarget, result.reflections), reflKey);
        }
    }

    std::string ShaderCodeCompiler::getCurrentCombinationKey(ShaderLanguage language, bool bindless, bool reflection) const
    {
        std::string bindlessStr = bindless ? "_Bindless" : "";
        std::string reflectionStr = reflection ? "_Reflection" : "";
        auto languageStr = enumToString(language);
        auto prefixStr = languageStr + reflectionStr + bindlessStr;
        for (size_t i = 0; i < conditions.size(); ++i)
        {
            auto conditionStr = conditions[i]() ? "_True" : "_False";
            auto branchName = "Branch_" + std::to_string(i);
            prefixStr += "_" + branchName + "_" + conditionStr;
        }
        return ShaderHardcodeManager::getItemName(sourceLocationStr, prefixStr);
    }

    std::vector<SlangModule*> ShaderCodeCompiler::getCurrentBranchModules(bool bindless) const
    {
        std::vector<SlangModule*> result;
        for (int i = conditions.size() - 1; i >= 0; --i)
        {
            result.emplace_back(getBranchModule(i, conditions[i](), bindless));
        }
        return result;
    }

    SlangModule* ShaderCodeCompiler::getBranchModule(size_t index, bool condition, bool bindless) const
    {
        std::shared_lock<std::shared_mutex> lock(threadMutex);
        SlangModule* result = nullptr;
        std::string bindlessStr = bindless ? "_Bindless" : "";
        auto languageStr = "SlangModule";

        auto branchName = "Branch_" + std::to_string(index);

        auto conditionStr = condition ? "_True" : "_False";

        auto branchKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + branchName + conditionStr);

#if HELICON_HAS_HARDCODE_SHADERS
        try
        {
            return &std::get<2>(std::get<1>(ShaderHardcodeManager::getHardcodeShader(stage, codeKey)));
        }
        catch (const std::runtime_error&)
        {
            // Fall through to per-instance outputs when hardcoded shaders are stale or incomplete.
        }
#endif

        if (auto it = compiledOutputs_.find(branchKey); it != compiledOutputs_.end())
            result = &std::get<2>(std::get<1>(it->second));
        else
            throw std::runtime_error("Compiled shader code not found for key: " + branchKey);

        return result;
    }
}
