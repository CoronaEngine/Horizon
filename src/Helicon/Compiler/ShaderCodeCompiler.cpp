
#include "ShaderCodeCompiler.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include <Codegen/AST/Parser.hpp>

#include "ShaderHardcodeManager.h"
#include "ShaderLanguageConverter.h"
#include <Compiler/ShaderCommon.h>
#include <shared_mutex>
#include <optional>
#include <ranges>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
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

        void reportSlangDiagnostics(slang::IBlob* diagnostics)
        {
            if (!diagnostics)
                return;
            std::cerr << static_cast<const char*>(diagnostics->getBufferPointer()) << std::endl;
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
        CompilerOption option, const std::source_location& sourceLocation)
    {
        sourceLocationStr = ShaderHardcodeManager::getSourceLocationString(sourceLocation);
        stage = enumToString(inputStage);
        compile(shaderCode, inputStage, option);
    }

    ShaderCodeModule ShaderCodeCompiler::getShaderCode(ShaderLanguage language, bool bindless, ConditionInfo conditionInfo)
    {
        if (!conditions.empty() && conditionInfo.empty())
            conditionInfo = getCurrentConditionInfo();

        std::string codeKey;
        std::string reflKey;
        if (!conditions.empty())
        {
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
            ShaderCodeModule result;
            result.shaderCode = std::get<1>(ShaderHardcodeManager::getHardcodeShader(stage, codeKey));
            result.shaderResources = std::get<0>(ShaderHardcodeManager::getHardcodeShader(stage, reflKey));
            return result;
        }
        catch (const std::runtime_error&)
        {
            // Fall through to per-instance outputs when hardcoded shaders are stale or incomplete.
        }
#endif

        auto findCached = [&]() -> std::optional<ShaderCodeModule> {
            auto code = compiledOutputs_.find(codeKey);
            if (code == compiledOutputs_.end())
                return std::nullopt;
            auto reflection = compiledOutputs_.find(reflKey);
            if (reflection == compiledOutputs_.end())
                return std::nullopt;

            const auto& storedCode = std::get<1>(code->second);
            ShaderCodeModule result;
            if (const auto* binary = std::get_if<std::vector<uint32_t>>(&storedCode))
                result.shaderCode = *binary;
            else
                result.shaderCode = std::get<std::string>(storedCode);
            result.shaderResources = std::get<0>(reflection->second);
            return result;
        };

        // 命中已编译变体时只读共享锁；未命中才升级为独占锁并真正 link。
        {
            std::shared_lock<std::shared_mutex> lock(threadMutex);
            if (auto cached = findCached())
                return *cached;
        }

        if (conditions.empty())
            throw std::runtime_error("Compiled shader code not found for key: " + codeKey);

        // combineLocked 会写 compiledOutputs_ 并递归读取已序列化的 branch/type-header
        // module，所以必须持有独占锁，且它内部不再加锁（见 getBranchModule 等）。
        {
            std::unique_lock<std::shared_mutex> lock(threadMutex);
            if (!findCached())
                combineLocked(bindless, conditionInfo, language, false);
        }

        {
            std::shared_lock<std::shared_mutex> lock(threadMutex);
            if (auto cached = findCached())
                return *cached;
        }

        throw std::runtime_error("Compiled shader code or reflection not found for key: " + codeKey);
    }

    void ShaderCodeCompiler::compile(const std::string& shaderCode, ShaderStage inputStage,
                                     CompilerOption option)
    {
        bool isCompilingBindless = Ast::Parser::getBindless();
        auto gs = ShaderLanguageConverter::getGlobalSession();
        std::vector<slang::TargetDesc> targetList;
        auto spv_1_6 = gs->findProfile("spirv_1_6");
        auto sm_6_6 = gs->findProfile("sm_6_6");
        auto sm_5_0 = gs->findProfile("sm_5_0");
        if (option.compileGLSL)
            targetList.push_back({.format = SLANG_GLSL,.profile = spv_1_6});
        if (option.compileDXIL)
            targetList.push_back({.format = SLANG_DXIL,.profile = sm_6_6});
        if (option.compileHLSL)
            targetList.push_back({.format = SLANG_HLSL,.profile = sm_6_6});
        if (option.compileSpirV)
            targetList.push_back({.format = SLANG_SPIRV,.profile = spv_1_6});
        if (option.compileDXBC && !isCompilingBindless)
            targetList.push_back({.format = SLANG_DXBC,.profile = sm_5_0});

        std::vector options = {
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::EmitSpirvDirectly,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
        };
        Slang::ComPtr<slang::ISession> currSession;
        SlangStageModules* currStageModules = nullptr;
        std::vector<SlangBranchModule>* currBranchModules = nullptr;
        if (!isCompilingBindless)
        {
            //bind
            targets = targetList;
            session = ShaderLanguageConverter::createSession(gs, targets, options, option.enableMatrixColumnMajor);
            currSession = session;
            currStageModules = &stageModules;
            currBranchModules = &branchModules;
        }
        else
        {
            //bindless
            bindlessTargets = targetList;
            bindlessSession = ShaderLanguageConverter::createSession(gs, bindlessTargets, options, option.enableMatrixColumnMajor);
            currSession = bindlessSession;
            currStageModules = &bindlessStageModules;
            currBranchModules = &bindlessBranchModules;
        }

        compilerOption = option;

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
                 << (isCompilingBindless ? "_bindless" : "") << ".slang";
            if (std::ofstream out{name.str()}; out)
                out << shaderCode;
        }

        // 仅 Debug 下把 module 源码/IR 写入离线 hardcode 文件；运行时不需要这些
        // 序列化 module（combine 直接使用 ComPtr），所以不再放进 compiledOutputs_。
        auto storeModule = [&](const Slang::ComPtr<slang::IModule>& module, const std::string& itemName) {
#ifdef CABBAGE_ENGINE_DEBUG
            ShaderHardcodeManager::addTarget(ShaderLanguageConverter::convertModule(module), stage, itemName);
#else
            (void)module;
            (void)itemName;
#endif
        };
        const std::string bindlessStr = isCompilingBindless ? "_Bindless" : "";

        // 加载额外 Slang Module Dependency（只加载一次，后续 link 复用 session 内的 module）。
        auto& currDependencies = isCompilingBindless ? bindlessDependencyModules : dependencyModules;
        currDependencies.clear();
        currDependencies.reserve(option.slangModules.size());
        for (auto dep : option.slangModules)
            currDependencies.push_back(ShaderLanguageConverter::loadModule(currSession, *dep));

        auto languageStr = "SlangModule";
        if (!option.branches.empty())
        {
            currStageModules->typeHeader = ShaderLanguageConverter::loadModule(currSession, "type_header", option.typeHeader);
            storeModule(currStageModules->typeHeader,
                        ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + "_Type_Header"));

            // currBranchModules 的下标必须与 conditions 的下标一致：
            // combineLocked 用 conditionInfo[i] 选择 branchModules[i] 的 true/false
            // module。若这里反向 push，fragment 的 branch_3 变体会被当成 branch_0
            // 链接，最终 isSkyBox=false 却链接了 skybox true。
            size_t index = 0;
            conditions.resize(option.branches.size());
            for (auto& branch : option.branches)
            {
                SlangBranchModule bm;
                auto modPrefix = "branch_" + std::to_string(index);

                auto trueB = ShaderLanguageConverter::loadModule(currSession, modPrefix + "_true", branch.trueBranch);
                auto falseB = ShaderLanguageConverter::loadModule(currSession, modPrefix + "_false", branch.falseBranch);

                auto branchName = "Branch_" + std::to_string(index);
                storeModule(trueB,
                            ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + branchName + "_True"));
                storeModule(falseB,
                            ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + branchName + "_False"));

                bm.branchTrue = trueB;
                bm.branchFalse = falseB;
                currBranchModules->push_back(bm);

                conditions[index] = branch.conditionDetector;
                ++index;
            }

            currStageModules->core = ShaderLanguageConverter::loadModule(currSession, "core_source", shaderCode);
            storeModule(currStageModules->core,
                        ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + bindlessStr + "_Branch_Core"));
        }
        else
        {
            currStageModules->core = ShaderLanguageConverter::loadModule(currSession, "core_source", shaderCode);
        }

        currStageModules->entryPoint = ShaderLanguageConverter::findEntryPoint(currStageModules->core, "main");

        const ConditionInfo info = conditions.empty() ? ConditionInfo {} : getCurrentConditionInfo();
        combineLocked(isCompilingBindless, info, std::nullopt, true);
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

    // 调用方必须持有 threadMutex 的独占锁。这里会写 compiledOutputs_。
    // 首次 compile() 已经把 core/type-header/branch module 和 entry point 加载进
    // session；运行时只对已有的 IModule 做 compose + link，不重新创建 session，
    // 也不重新从源码/序列化 blob 加载模块。
    void ShaderCodeCompiler::combineLocked(bool bindless, ConditionInfo conditionInfo,
                                           std::optional<ShaderLanguage> language, bool writeHardcode)
    {
        SlangStageModules& modules = bindless ? bindlessStageModules : stageModules;
        const std::vector<SlangBranchModule>& branches = bindless ? bindlessBranchModules : branchModules;
        const std::vector<Slang::ComPtr<slang::IModule>>& dependencies =
            bindless ? bindlessDependencyModules : dependencyModules;
        const std::vector<slang::TargetDesc>& currentTargets = bindless ? bindlessTargets : targets;
        Slang::ComPtr<slang::ISession>& currentSession = bindless ? bindlessSession : session;

        if (!currentSession || !modules.core || !modules.entryPoint)
            throw std::runtime_error("ShaderCodeCompiler: Slang modules are not loaded.");
        if (conditionInfo.size() != branches.size())
            throw std::runtime_error("ShaderCodeCompiler: branch condition count mismatch.");

        std::vector<slang::IComponentType*> components;
        components.reserve(3 + dependencies.size() + branches.size());
        components.push_back(modules.core.get());
        components.push_back(modules.entryPoint.get());
        if (modules.typeHeader)
            components.push_back(modules.typeHeader.get());
        for (const auto& dependency : dependencies)
            components.push_back(dependency.get());

        for (size_t i = 0; i < branches.size(); ++i)
        {
            if (!branches[i].branchTrue || !branches[i].branchFalse)
                throw std::runtime_error("ShaderCodeCompiler: branch module is missing.");
            components.push_back(conditionInfo[i] ? branches[i].branchTrue.get()
                                                  : branches[i].branchFalse.get());
        }

        Slang::ComPtr<slang::IComponentType> composedProgram;
        Slang::ComPtr<slang::IBlob> diagnostics;
        SlangResult result = currentSession->createCompositeComponentType(
            components.data(), static_cast<SlangInt>(components.size()),
            composedProgram.writeRef(), diagnostics.writeRef());
        reportSlangDiagnostics(diagnostics);
        if (SLANG_FAILED(result) || !composedProgram)
            throw std::runtime_error("Failed to compose Slang branch program.");

        Slang::ComPtr<slang::IComponentType> linkedProgram;
        Slang::ComPtr<slang::IBlob> linkDiagnostics;
        result = composedProgram->link(linkedProgram.writeRef(), linkDiagnostics.writeRef());
        reportSlangDiagnostics(linkDiagnostics);
        if (SLANG_FAILED(result) || !linkedProgram)
            throw std::runtime_error("Failed to link Slang branch program.");

        SlangCompileResult compiled;
        for (size_t i = 0; i < currentTargets.size(); ++i)
        {
            if (language &&
                ShaderLanguageConverter::convertShaderLanguage(currentTargets[i].format) != *language)
            {
                continue;
            }
            ShaderLanguageConverter::fillCompileResult(
                compiled, linkedProgram, static_cast<SlangInt>(i), currentTargets[i].format, true);
        }

        storeCompileResultLocked(bindless, conditionInfo, compiled, writeHardcode);
    }

    void ShaderCodeCompiler::storeCompileResultLocked(bool bindless,
                                                      const ConditionInfo& conditionInfo,
                                                      const SlangCompileResult& result,
                                                      bool writeHardcode)
    {
        // Store per-instance outputs; only the initial compile writes the offline
        // hardcode sources. Runtime condition switches must not touch those files.
        auto storeCode = [&](const auto& code, const std::string& itemName) {
            compiledOutputs_[itemName] = code;
#ifdef CABBAGE_ENGINE_DEBUG
            if (writeHardcode)
                ShaderHardcodeManager::addTarget(code, stage, itemName);
#endif
        };
        auto storeReflection = [&](const ShaderCodeModule::ShaderResources& res, const std::string& itemName) {
            compiledOutputs_[itemName] = res;
#ifdef CABBAGE_ENGINE_DEBUG
            if (writeHardcode)
                ShaderHardcodeManager::addTarget(res, stage, itemName);
#endif
        };

        const std::vector<uint32_t>* spirvTarget = nullptr;
        if (auto spirv = result.binaryTargets.find(ShaderLanguage::SpirV); spirv != result.binaryTargets.end())
            spirvTarget = &spirv->second;

        const std::string_view bindlessStr = bindless ? "_Bindless" : "";
        auto makeKeys = [&](ShaderLanguage language, std::string& codeKey, std::string& reflKey) {
            if (!conditions.empty())
            {
                codeKey = getCurrentCombinationKey(language, bindless, false, conditionInfo);
                reflKey = getCurrentCombinationKey(language, bindless, true, conditionInfo);
            }
            else
            {
                const auto languageStr = enumToString(language);
                codeKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + std::string(bindlessStr));
                reflKey = ShaderHardcodeManager::getItemName(sourceLocationStr, languageStr + "_Reflection" + std::string(bindlessStr));
            }
        };

        std::string codeKey;
        std::string reflKey;
        for (auto& stringTarget : result.stringTargets)
        {
            makeKeys(stringTarget.first, codeKey, reflKey);
            storeCode(stringTarget.second, codeKey);
            storeReflection(reflectionForTarget(stringTarget.first, spirvTarget, result.reflections), reflKey);
        }

        for (auto& binaryTarget : result.binaryTargets)
        {
            makeKeys(binaryTarget.first, codeKey, reflKey);
            storeCode(binaryTarget.second, codeKey);
            storeReflection(reflectionForTarget(binaryTarget.first, spirvTarget, result.reflections), reflKey);
        }
    }
}
