#pragma once

#include "Codegen/AST/Struct.hpp"
#include "ktm/type_vec.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <slang-com-ptr.h>
#include <source_location>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace EmbeddedShader
{
enum class ShaderLanguage : uint16_t;
enum class ShaderStage : uint16_t;

    struct SlangModule
    {
        std::string name;
        std::string path;
        std::vector<uint8_t> binData;
    };

struct ShaderCodeModule
    {
        struct ShaderResources
        {
            enum BindType
            {
                none = -1,
                pushConstantMembers = 0,
                stageInputs = 1,
                stageOutputs = 2,
                uniformBuffers = 3,
                sampledImages = 4,

                texture,
                sampler,
                rawBuffer,
                storageTexture,
                storageBuffer,
                uniformBufferMembers,
            };
            struct ShaderBindInfo
            {
                uint32_t set = 0;
                uint32_t binding = 0;
                uint32_t location = 0;
                std::string semantic;

                std::string variateName;
                std::string typeName;
                uint64_t elementCount = 0;
                uint32_t typeSize = 0;
                uint64_t byteOffset = 0;

                BindType bindType = none;
            };

            struct EntryPointInfo
            {
                std::string name;
                ShaderStage stage {};
                ktm::uvec3 numthreads { 1, 1, 1 };
            };

            uint32_t pushConstantSize = 0;
            std::string pushConstantName;

            uint32_t uniformBufferSize = 0;
            std::string uniformBufferName;

            std::vector<ShaderBindInfo> bindInfoPool;
            std::vector<EntryPointInfo> entryPointInfoPool;

            // 按 bindType 分组遍历
            template<typename Fn>
            void forEachBind(BindType type, Fn&& fn) const {
                for (auto& info : bindInfoPool)
                    if (info.bindType == type) fn(info);
            }

            // 仅在编译期使用：按 variateName 线性搜索
            ShaderBindInfo *findShaderBindInfo(const std::string &resourceName)
            {
                for (auto &info : bindInfoPool)
                {
                    if (info.variateName == resourceName)
                        return &info;
                }
                return nullptr;
            }
        } shaderResources;

        ShaderCodeModule() = default;
        ~ShaderCodeModule() = default;

        ShaderCodeModule(std::string shaderCode)
            : shaderCode(std::move(shaderCode))
        {
        }
        ShaderCodeModule(std::vector<uint32_t> shaderCode)
            : shaderCode(std::move(shaderCode))
        {
        }
        ShaderCodeModule(SlangModule shaderCode)
            : shaderCode(shaderCode)
        {
        }

        ShaderCodeModule(std::string shaderCode,ShaderResources shaderResources)
            : shaderResources(std::move(shaderResources)),shaderCode(std::move(shaderCode))
        {
        }

        ShaderCodeModule(std::vector<uint32_t> shaderCode,ShaderResources shaderResources)
            : shaderResources(std::move(shaderResources)),shaderCode(std::move(shaderCode))
        {
        }

        ShaderCodeModule(SlangModule shaderCode,ShaderResources shaderResources)
            : shaderResources(std::move(shaderResources)),shaderCode(shaderCode)
        {
        }

        operator std::string()
        {
            return std::get<std::string>(shaderCode);
        }

        operator std::vector<uint32_t>()
        {
            return std::get<std::vector<uint32_t>>(shaderCode);
        }

        operator SlangModule() const
        {
            return std::get<SlangModule>(shaderCode);
        }

        std::variant<std::vector<uint32_t>, std::string, SlangModule> shaderCode;
    };

    struct SlangCompileResult
    {
        using BinaryTarget = std::vector<uint32_t>;
        using StringTarget = std::string;
        std::unordered_map<ShaderLanguage, BinaryTarget> binaryTargets;
        std::unordered_map<ShaderLanguage, StringTarget> stringTargets;
        std::unordered_map<ShaderLanguage, ShaderCodeModule::ShaderResources> reflections;
    };

    struct CompilerOption
    {
        bool compileGLSL = false;
        bool compileHLSL = true;
        bool compileDXIL = false;
        bool compileDXBC = false;
        bool compileSpirV = true;
        bool enableBindless = true;
        bool enableMatrixColumnMajor = false;
        std::vector<SlangModule*> slangModules;
        std::vector<Ast::BranchOutput> branches;
        std::string typeHeader;
    };

    struct SlangBranchModule
    {
        Slang::ComPtr<slang::IModule> branchTrue;
        Slang::ComPtr<slang::IModule> branchFalse;
    };

    // 首次 compile() 时在 session 里加载好的 module。运行时条件切换只从
    // 这些已经加载/验证过的 IModule 重新 compose + link，不再新建 session，
    // 也不再从源码/序列化 blob 重新 load。
    struct SlangStageModules
    {
        Slang::ComPtr<slang::IModule> typeHeader;
        Slang::ComPtr<slang::IModule> core;
        Slang::ComPtr<slang::IEntryPoint> entryPoint;
    };

    struct ShaderCodeCompiler
    {
    public:
        using ConditionInfo = std::vector<bool>;
        // ShaderCodeCompiler(const std::string &shaderCode, ShaderStage inputStage, ShaderLanguage language = ShaderLanguage::GLSL, const std::source_location &sourceLocation = std::source_location::current());
        // ShaderCodeCompiler(const std::vector<uint32_t> &shaderCode, ShaderStage inputStage, ShaderLanguage language = ShaderLanguage::GLSL, const std::source_location &sourceLocation = std::source_location::current());



        ShaderCodeCompiler(const std::string &shaderCode, ShaderStage inputStage, CompilerOption option = {}, const std::source_location &sourceLocation = std::source_location::current());
        ~ShaderCodeCompiler() = default;

        [[nodiscard]] ShaderCodeModule getShaderCode(ShaderLanguage language, bool bindless = false, ConditionInfo conditionInfo = {});
        void compile(const std::string& shaderCode, ShaderStage inputStage, CompilerOption option = {});
        CompilerOption getCompilerOption() const;
        static std::string getCombinedKey(const ConditionInfo& conditionInfo);

        ConditionInfo getCurrentConditionInfo() const;

    private:
        std::string getCurrentCombinationKey(ShaderLanguage language, bool bindless, bool reflection, ConditionInfo conditionInfo) const;

        // 用首次编译时缓存的 session / IModule / IEntryPoint 重新 compose + link
        // 指定条件组合，编译所有 target 并写入 compiledOutputs_。
        // 调用方必须持有 threadMutex 独占锁。writeHardcode 仅在首次 compile()
        // 时为 true；运行时切分支不再往 HardcodeShaders 源码追加字面量。
        void combineLocked(bool bindless, ConditionInfo conditionInfo, bool writeHardcode);

        // 把一次 link 的结果按 target language/条件组合写入 compiledOutputs_。
        void storeCompileResultLocked(bool bindless, const ConditionInfo& conditionInfo,
                                      const SlangCompileResult& result, bool writeHardcode);

        std::string sourceLocationStr;
        std::string stage;
        std::vector<std::function<bool()>> conditions;
        CompilerOption compilerOption;

        Slang::ComPtr<slang::ISession> session;
        Slang::ComPtr<slang::ISession> bindlessSession;

        SlangStageModules stageModules;
        SlangStageModules bindlessStageModules;
        std::vector<slang::TargetDesc> targets;
        std::vector<slang::TargetDesc> bindlessTargets;

        std::vector<SlangBranchModule> branchModules;
        std::vector<SlangBranchModule> bindlessBranchModules;
        std::vector<Slang::ComPtr<slang::IModule>> dependencyModules;
        std::vector<Slang::ComPtr<slang::IModule>> bindlessDependencyModules;

        // Per-instance compiled output storage (replaces debugHardcodeShaders)
        using CompiledVariant = std::variant<ShaderCodeModule::ShaderResources, std::variant<std::vector<uint32_t>, std::string>>;
        mutable std::unordered_map<std::string, CompiledVariant> compiledOutputs_;
    };
}
