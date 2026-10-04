
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
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace EmbeddedShader
{
    std::shared_mutex threadMutex;

    namespace
    {
        // 将 Slang 反射产出的点分全名（如 "global_ubo.viewMatrix"）裁剪为下游约定的短名
        // （"viewMatrix"）。codegen 侧按 EDSL 名字用 findShaderBindInfo 查找、runtime 侧的
        // 硬编码名字比较都依赖短名。此逻辑与离线 codegen tools/main.cpp 保持一致。
        void stripReflectionMemberPrefixes(ShaderCodeModule::ShaderResources& resources)
        {
            for (auto& info : resources.bindInfoPool)
            {
                if (auto pos = info.variateName.find_last_of('.'); pos != std::string::npos)
                    info.variateName = info.variateName.substr(pos + 1);
            }
        }

        // 反射源统一走 Slang（result.reflections），不再使用 SPIRV-Cross。
        // spirv 参数在移除 SPIRV-Cross 后已无消费者，保留仅为暂不改动调用点；Phase 3 清理。
        ShaderCodeModule::ShaderResources reflectionForTarget(
            ShaderLanguage targetLanguage,
            const std::vector<uint32_t>* /*spirv*/,
            const std::unordered_map<ShaderLanguage, ShaderCodeModule::ShaderResources>& slangReflections)
        {
            auto slangReflection = slangReflections.find(targetLanguage);
            if (slangReflection == slangReflections.end())
                return {};

            auto reflection = slangReflection->second;
            // SpirV target 此前经 SPIRV-Cross 产出短名；切到 Slang 后需同样裁剪以保持契约。
            // GLSL/HLSL target 维持原有（点分全名）行为不变。
            if (targetLanguage == ShaderLanguage::SpirV)
                stripReflectionMemberPrefixes(reflection);

            return reflection;
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

    ShaderCodeModule ShaderCodeCompiler::getShaderCode(ShaderLanguage language, bool bindless, ConditionInfo conditionInfo)
    {
        std::shared_lock<std::shared_mutex> lock(threadMutex);

        ShaderCodeModule result;
        std::string codeKey;
        std::string reflKey;
        if (!conditions.empty())
        {
            if (conditionInfo.empty())
                conditionInfo = getCurrentConditionInfo();
            codeKey = getCurrentCombinationKey(language, bindless, false, conditionInfo);
            reflKey = getCurrentCombinationKey(language, bindless, true, conditionInfo);
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
        {
            if (!conditions.empty())
            {
                combine(false, conditionInfo);
                if (compilerOption.enableBindless)
                {
                    combine(true, conditionInfo);
                }

                it = compiledOutputs_.find(codeKey);
                if (it != compiledOutputs_.end())
                {
                    result.shaderCode = std::get<1>(it->second);
                }
                else
                {
                    throw std::runtime_error("Compiled shader code not found for key: " + codeKey);
                }
            }
            else
            {
                throw std::runtime_error("Compiled shader code not found for key: " + codeKey);
            }
        }

        if (auto it = compiledOutputs_.find(reflKey); it != compiledOutputs_.end())
            result.shaderResources = std::get<0>(it->second);
        else
            throw std::runtime_error("Compiled shader reflection not found for key: " + reflKey);

        return result;
    }

    void ShaderCodeCompiler::compile(const std::string& shaderCode, ShaderStage inputStage, ShaderLanguage language,
                                     CompilerOption option)
    {
        bool isCompilingBindless = Ast::Parser::getBindless();
        auto gs = ShaderLanguageConverter::getGlobalSession();
        std::vector<slang::TargetDesc> targets;
        auto spv_1_6 = gs->findProfile("spirv_1_6");
        auto sm_6_6 = gs->findProfile("sm_6_6");
        auto sm_5_0 = gs->findProfile("sm_5_0");
        if (option.compileGLSL)
            targets.push_back({.format = SLANG_GLSL,.profile = spv_1_6});
        if (option.compileDXIL)
            targets.push_back({.format = SLANG_DXIL,.profile = sm_6_6});
        if (option.compileHLSL)
            targets.push_back({.format = SLANG_HLSL,.profile = sm_6_6});
        if (option.compileSpirV)
            targets.push_back({.format = SLANG_SPIRV,.profile = spv_1_6});
        if (option.compileDXBC && !isCompilingBindless)
            targets.push_back({.format = SLANG_DXBC,.profile = sm_5_0});

        std::vector options = {
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::EmitSpirvDirectly,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
        };
        Slang::ComPtr<slang::ISession> currSession;
        std::vector<SlangBranchModule>* currBranchModules;
        if (!isCompilingBindless)
        {
            //bind
            session = ShaderLanguageConverter::createSession(gs, targets, options);
            currSession = session;
            currBranchModules = &branchModules;
        }
        else
        {
            //bindless
            bindlessSession = ShaderLanguageConverter::createSession(gs, targets, options);
            currSession = bindlessSession;
            currBranchModules = &bindlessBranchModules;
        }


        compilerOption = option;
        sourceStage = inputStage;

        // HORIZON_SLANG_DUMP=<dir>：把生成的 Slang 源码逐 stage 落盘。
        // EDSL 生成的代码平时无从查看（Release 下没有 ShaderHardcodeManager），
        // 排查"编译通过但渲染不对"时这是唯一能看到真实产物的手段。
        if (const char* dumpDir = std::getenv("HORIZON_SLANG_DUMP"))
        {
            static std::atomic<uint32_t> dumpCounter{0};
            const uint32_t seq = dumpCounter++;
            std::error_code ec;
            std::filesystem::create_directories(dumpDir, ec);
            std::ostringstream name;
            name << dumpDir << "/" << std::setw(3) << std::setfill('0') << seq << "_"
                 << "stage" << static_cast<int>(inputStage)
                 << (Ast::Parser::getBindless() ? "_bindless" : "") << ".slang";
            if (std::ofstream out{name.str()}; out)
                out << shaderCode;
        }

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

        ConditionInfo info;
        SlangCompileResult result;

        //加载额外Slang Module Dependency
        for (auto dep : option.slangModules)
        {
            ShaderLanguageConverter::loadModule(currSession, *dep);
        }

        if (!option.branches.empty())
        {
            auto languageStr = "SlangModule";

            auto typeHeaderModule = ShaderLanguageConverter::loadModule(currSession,"type_header",option.typeHeader);
            auto binTypeHeader = ShaderLanguageConverter::convertModule(typeHeaderModule);
            storeCode(binTypeHeader, ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + "_Type_Header"));

            size_t index = option.branches.size() - 1;
            conditions.resize(option.branches.size());
            for (auto i = option.branches.rbegin(); i != option.branches.rend(); ++i)
            {
                auto& branch = *i;
                SlangBranchModule bm;
                auto modPrefix = "branch_" + std::to_string(index);

                auto trueB = ShaderLanguageConverter::loadModule(currSession, modPrefix + "_true", branch.trueBranch);
                auto falseB = ShaderLanguageConverter::loadModule(currSession, modPrefix + "_false", branch.falseBranch);

                auto binTrue = ShaderLanguageConverter::convertModule(trueB);
                auto binFalse = ShaderLanguageConverter::convertModule(falseB);

                auto branchName = "Branch_" + std::to_string(index);

                storeCode(binTrue, ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + branchName + "_True"));
                storeCode(binFalse, ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + branchName + "_False"));

                bm.branchTrue = trueB;
                bm.branchFalse = falseB;

                currBranchModules->push_back(bm);

                conditions[index] = branch.conditionDetector;

                --index;
            }
            info = getCurrentConditionInfo();

            auto core = ShaderLanguageConverter::loadModule(currSession, "core_source", shaderCode);
            auto binCore = ShaderLanguageConverter::convertModule(core);
            storeCode(binCore, ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + "_Branch_Core"));

            auto ep = ShaderLanguageConverter::findEntryPoint(core, "main");
            std::vector<slang::IComponentType*> composeComponents = {core, ep ,typeHeaderModule};
            auto branches = getSlangBranchModules(isCompilingBindless, info);
            composeComponents.insert(composeComponents.end(), branches.begin(), branches.end());
            auto linkedProgram = ShaderLanguageConverter::link(currSession, composeComponents);

            for (size_t i = 0; i < targets.size(); ++i)
                ShaderLanguageConverter::fillCompileResult(result,linkedProgram,i,targets[i].format,true);
        }
        else
        {
            auto core = ShaderLanguageConverter::loadModule(currSession, "core_source", shaderCode);
            auto ep = ShaderLanguageConverter::findEntryPoint(core, "main");
            std::vector<slang::IComponentType*> composeComponents = {core, ep};
            auto linkedProgram = ShaderLanguageConverter::link(currSession, composeComponents);

            for (size_t i = 0; i < targets.size(); ++i)
                ShaderLanguageConverter::fillCompileResult(result,linkedProgram,i,targets[i].format,true);
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
                codeKey = getCurrentCombinationKey(stringTarget.first, Ast::Parser::getBindless(), false, info);
                reflKey = getCurrentCombinationKey(stringTarget.first, Ast::Parser::getBindless(), true, info);
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
                codeKey = getCurrentCombinationKey(binaryTarget.first, Ast::Parser::getBindless(), false, info);
                reflKey = getCurrentCombinationKey(binaryTarget.first, Ast::Parser::getBindless(), true, info);
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

    CompilerOption ShaderCodeCompiler::getCompilerOption() const
    {
        return compilerOption;
    }

    std::string ShaderCodeCompiler::getCombinedKey(const ConditionInfo& conditionInfo)
    {
        std::string result;
        for (const auto value : conditionInfo)
        {
            result += value ? "1" : "0";
        }
        return result;
    }

    std::string ShaderCodeCompiler::getCurrentCombinationKey(ShaderLanguage language, bool bindless, bool reflection, ConditionInfo conditionInfo) const
    {
        std::string bindlessStr = bindless ? "_Bindless" : "";
        std::string reflectionStr = reflection ? "_Reflection" : "";
        auto languageStr = enumToString(language);
        auto prefixStr = languageStr + reflectionStr + bindlessStr;
        for (size_t i = 0; i < conditionInfo.size(); ++i)
        {
            auto conditionStr = conditionInfo[i] ? "_True" : "_False";
            auto branchName = "Branch_" + std::to_string(i);
            prefixStr += "_" + branchName + "_" + conditionStr;
        }
        return ShaderHardcodeManager::getItemName(sourceLocationStr, prefixStr);
    }

    ShaderCodeCompiler::ConditionInfo ShaderCodeCompiler::getCurrentConditionInfo() const
    {
        ConditionInfo info(conditions.size());
        for (size_t i = 0; i < info.size(); ++i)
        {
            info[i] = conditions[i]();
        }
        return info;
    }

    void ShaderCodeCompiler::combine(bool bindless, ConditionInfo conditionInfo)
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

        SlangCompileArgs2 compileArgs2;
        compileArgs2.module = getCoreBranchModule(bindless);
        compileArgs2.stage = sourceStage;
        compileArgs2.sourceLanguage = sourceLanguage;
        compileArgs2.deps = compilerOption.slangModules;
        compileArgs2.deps.push_back(getTypeHeaderModule(bindless));
        compileArgs2.matrixMajor = compilerOption.enableMatrixColumnMajor ? SlangMatrixMajor::ColumnMajor : SlangMatrixMajor::RowMajor;

        //branches
        auto branches = getBranchModules(bindless, conditionInfo);
        compileArgs2.deps.insert(compileArgs2.deps.end(),branches.begin(), branches.end());

        compileArgs2.enableReflection = true;
        if (compilerOption.compileGLSL)
            compileArgs2.targetLanguages.push_back(ShaderLanguage::GLSL);
        if (compilerOption.compileHLSL)
            compileArgs2.targetLanguages.push_back(ShaderLanguage::HLSL);
        if (compilerOption.compileSpirV)
            compileArgs2.targetLanguages.push_back(ShaderLanguage::SpirV);
        if (compilerOption.compileDXIL)
            compileArgs2.targetLanguages.push_back(ShaderLanguage::DXIL);
        if (compilerOption.compileDXBC && !Ast::Parser::getBindless())
            compileArgs2.targetLanguages.push_back(ShaderLanguage::DXBC);
        auto result = ShaderLanguageConverter::slangCompilerWithModules(compileArgs2);

        const std::vector<uint32_t>* spirvTarget = nullptr;
        if (auto spirv = result.binaryTargets.find(ShaderLanguage::SpirV); spirv != result.binaryTargets.end())
            spirvTarget = &spirv->second;

        std::string codeKey;
        std::string reflKey;

        //string targets
        for (auto& stringTarget : result.stringTargets)
        {
            codeKey = getCurrentCombinationKey(stringTarget.first, Ast::Parser::getBindless(), false, conditionInfo);
            reflKey = getCurrentCombinationKey(stringTarget.first, Ast::Parser::getBindless(), true, conditionInfo);

            storeCode(stringTarget.second,codeKey);
            storeReflection(reflectionForTarget(stringTarget.first, spirvTarget, result.reflections), reflKey);
        }

        //binary targets
        for (auto& binaryTarget : result.binaryTargets)
        {
            codeKey = getCurrentCombinationKey(binaryTarget.first, Ast::Parser::getBindless(), false, conditionInfo);
            reflKey = getCurrentCombinationKey(binaryTarget.first, Ast::Parser::getBindless(), true, conditionInfo);

            storeCode(binaryTarget.second,codeKey);
            storeReflection(reflectionForTarget(binaryTarget.first, spirvTarget, result.reflections), reflKey);
        }
    }

    std::vector<SlangModule*> ShaderCodeCompiler::getBranchModules(bool bindless, ConditionInfo conditionInfo) const
    {
        std::vector<SlangModule*> result;
        for (int i = conditionInfo.size() - 1; i >= 0; --i)
        {
            result.emplace_back(getBranchModule(i, conditionInfo[i], bindless));
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

    SlangModule* ShaderCodeCompiler::getCoreBranchModule(bool bindless) const
    {
        std::shared_lock<std::shared_mutex> lock(threadMutex);
        SlangModule* result = nullptr;
        std::string bindlessStr = bindless ? "_Bindless" : "";
        auto languageStr = "SlangModule";

        auto branchKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + "_Branch_Core");

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

    SlangModule* ShaderCodeCompiler::getTypeHeaderModule(bool bindless) const
    {
        std::shared_lock<std::shared_mutex> lock(threadMutex);
        SlangModule* result = nullptr;
        std::string bindlessStr = bindless ? "_Bindless" : "";
        auto languageStr = "SlangModule";

        auto branchKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + "_Type_Header");;

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

    std::vector<slang::IComponentType*> ShaderCodeCompiler::getSlangBranchModules(bool bindless, ConditionInfo conditionInfo) const
    {
        const std::vector<SlangBranchModule>* currBranchModules;
        if (!bindless)
        {
            currBranchModules = &branchModules;
        }
        else
        {
            currBranchModules = &bindlessBranchModules;
        }

        std::vector<slang::IComponentType*> result;
        for (size_t i = 0; i < conditionInfo.size(); ++i)
        {
            bool condition = conditionInfo[i];
            const auto& branch = currBranchModules->at(i);
            if (condition)
                result.push_back(branch.branchTrue);
            else
                result.push_back(branch.branchFalse);
        }

        return result;
    }
}
