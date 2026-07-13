#pragma once

#include "Codegen/AST/Struct.hpp"
#include "ktm/type_vec.h"
#include "spirv-tools/libspirv.hpp"

#include <cstdint>
#include <mutex>
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
                ShaderStage stage;
                ktm::uvec3 numthreads;
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

    struct CompilerOption
    {
        bool compileGLSL = true;
        bool compileHLSL = true;
        bool compileDXIL = false;
        bool compileDXBC = false;
        bool compileSpirV = true;
        bool enableBindless = true;
        std::vector<SlangModule*> slangModules;
        std::vector<Ast::BranchOutput> branches;
        std::string typeHeader;
    };

    struct ShaderCodeCompiler
    {
    public:
        // ShaderCodeCompiler(const std::string &shaderCode, ShaderStage inputStage, ShaderLanguage language = ShaderLanguage::GLSL, const std::source_location &sourceLocation = std::source_location::current());
        // ShaderCodeCompiler(const std::vector<uint32_t> &shaderCode, ShaderStage inputStage, ShaderLanguage language = ShaderLanguage::GLSL, const std::source_location &sourceLocation = std::source_location::current());



        ShaderCodeCompiler(const std::string &shaderCode, ShaderStage inputStage, ShaderLanguage language = {}, CompilerOption option = {}, const std::source_location &sourceLocation = std::source_location::current());
        ~ShaderCodeCompiler() = default;

        [[nodiscard]] ShaderCodeModule getShaderCode(ShaderLanguage language, bool bindless = false) const;
        void compile(const std::string& shaderCode, ShaderStage inputStage, ShaderLanguage language = {}, CompilerOption option = {});
    private:
        std::vector<SlangModule*> getCurrentBranchModules(bool bindless) const;
        SlangModule* getBranchModule(size_t index, bool condition, bool bindless) const;
        std::string sourceLocationStr;
        std::string stage;
        std::vector<std::function<bool()>> conditions;

        // Per-instance compiled output storage (replaces debugHardcodeShaders)
        using CompiledVariant = std::variant<ShaderCodeModule::ShaderResources, std::variant<std::vector<uint32_t>, std::string, SlangModule>>;
        mutable std::unordered_map<std::string, CompiledVariant> compiledOutputs_;
    };
}
