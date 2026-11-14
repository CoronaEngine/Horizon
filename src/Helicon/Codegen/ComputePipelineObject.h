#pragma once
#include <Compiler/ShaderCommon.h>

#include <functional>
#include <Codegen/ParseHelper.h>
#include <Codegen/AST/AST.hpp>
#include <Codegen/AST/Parser.hpp>
#include <Codegen/AST/Node.hpp>
#include <Compiler/ShaderCodeCompiler.h>

namespace EmbeddedShader
{
	// Entry for auto-binding: maps an EDSL proxy's resource pointer to pre-resolved bind metadata.
	struct AutoBindEntry
	{
		void** boundResourceRef;   // → points to proxy's boundResource_ (void*)

		// Direct-access metadata (resolved at compile time)
		uint64_t byteOffset = 0;
		uint32_t typeSize = 0;
		int32_t  bindType = -1;    // -1 = no metadata
		uint32_t location = 0;
		const void* boundValueRef = nullptr;
		uint32_t boundValueSize = 0;
	    size_t* dirtyVersion;
	    size_t currentDirtyVersion = 0;

	    // boundResourceRef 指向的是 HardwareBuffer* 而非 HardwareImage*。
	    // 消费端(bind_auto_resources)靠它选 set_resource_direct 的重载 ——
	    // 不靠 bindType 隐式判别,免得以后 bindType 的映射一改就静默转错类型。
	    bool boundResourceIsBuffer = false;
	};

	class ComputePipelineObject
	{
	public:
		static ComputePipelineObject compile(auto&& computeShaderCode, ktm::uvec3 numthreads = ktm::uvec3(1), CompilerOption compilerOption = {}, std::source_location sourceLocation = std::source_location::current());
	    [[nodiscard]] static std::string getCombinedKey(const ShaderCodeCompiler::ConditionInfo& conditionInfo);
	    void updateAutoBind(const ShaderCodeCompiler::ConditionInfo& compConditionInfo);
		std::unique_ptr<ShaderCodeCompiler> compute;
		std::vector<AutoBindEntry> autoBindEntries;
	private:
		std::unordered_set<std::string> sampledTextures;
		std::vector<Ast::ParseOutput> parse(auto&& computeShaderCode);
	};

    inline std::string ComputePipelineObject::getCombinedKey(const ShaderCodeCompiler::ConditionInfo& conditionInfo)
    {
        return "compute" + ShaderCodeCompiler::getCombinedKey(conditionInfo);
    }

    inline void ComputePipelineObject::updateAutoBind(const ShaderCodeCompiler::ConditionInfo& compConditionInfo)
    {
        // 分支组合变化时重新反射；清空旧表，避免旧 offset 残留。
        autoBindEntries.clear();

        // Collect auto-bind entries from globalStatements:
        // Walk all globally-defined textures and check if they have a back-pointer
        // to a proxy's boundResource_. Filter by membership in the compiled shader's bindInfoPool.
        {
            auto codeModule = compute->getShaderCode(ShaderLanguage::SpirV, compConditionInfo);
            auto& globals = Ast::Parser::getGlobalStatements();
            for (auto& stmt : globals)
            {
                if (auto* def = dynamic_cast<Ast::DefineUniformVariate*>(stmt.get()))
                {
                    if (def->variate && def->variate->boundValueRef)
                    {
                        if (auto* bindInfo = codeModule.shaderResources.findShaderBindInfo(def->variate->name))
                        {
                            autoBindEntries.push_back({
                                nullptr,
                                bindInfo->byteOffset,
                                bindInfo->typeSize,
                                static_cast<int32_t>(bindInfo->bindType),
                                bindInfo->location,
                                def->variate->boundValueRef,
                                def->variate->boundValueSize,
                                &def->variate->dirtyVersion
                            });
                        }
                    }
                }

                if (auto* def = dynamic_cast<Ast::DefineUniversalTexture*>(stmt.get()))
                {
                    if (def->texture && def->texture->boundResourceRef)
                    {
                        if (auto* bindInfo = codeModule.shaderResources.findShaderBindInfo(def->texture->name))
                        {
                            const auto imageType = sampledTextures.contains(def->texture->name)
                                ? ShaderCodeModule::ShaderResources::sampledImages : ShaderCodeModule::ShaderResources::storageTexture;
                            autoBindEntries.push_back({
                                def->texture->boundResourceRef,
                                bindInfo->byteOffset,
                                bindInfo->typeSize,
                                imageType,
                                bindInfo->location,
                                nullptr,
                                0,
                                &def->texture->dirtyVersion
                            });
                        }
                    }
                }

                if (auto* def = dynamic_cast<Ast::DefineUniversalArray*>(stmt.get()))
                {
                    if (def->array && def->array->boundResourceRef)
                    {
                        if (auto* bindInfo = codeModule.shaderResources.findShaderBindInfo(def->array->name))
                        {
                            autoBindEntries.push_back({
                                def->array->boundResourceRef,
                                bindInfo->byteOffset,
                                bindInfo->typeSize,
                                ShaderCodeModule::ShaderResources::storageBuffer,
                                bindInfo->location,
                                nullptr,
                                0,
                                &def->array->dirtyVersion,
                                0,
                                true
                            });
                        }
                    }
                }
            }
        }
    }

    ComputePipelineObject ComputePipelineObject::compile(auto&& computeShaderCode, ktm::uvec3 numthreads,CompilerOption compilerOption,std::source_location sourceLocation)
	{
		// DXBC shader model 5 has no descriptor heap handles.
		compilerOption.compileDXBC = false;
		Generator::SlangGenerator::numthreads = numthreads;
		ComputePipelineObject result;
		auto outputs = result.parse(std::forward<decltype(computeShaderCode)>(computeShaderCode));

	    compilerOption.slangModules.insert(compilerOption.slangModules.end(),outputs[0].sourceModule.begin(), outputs[0].sourceModule.end());

	    compilerOption.branches.swap(outputs[0].branches);
	    compilerOption.typeHeader = outputs[0].typeHeader;
		result.compute = std::make_unique<ShaderCodeCompiler>(outputs[0].output, ShaderStage::ComputeShader, compilerOption, sourceLocation);

		result.updateAutoBind(result.compute->getCurrentConditionInfo());

		return result;
	}

	std::vector<Ast::ParseOutput> ComputePipelineObject::parse(auto&& computeShaderCode)
	{
		auto csFunc = std::function(std::forward<decltype(computeShaderCode)>(computeShaderCode));

		Ast::Parser::beginShaderParse(Ast::ShaderStage::Compute);
		if (ParseHelper::hasReturnValue(csFunc))
			throw std::logic_error("Compute shader function doesn't have return value");
		auto csParams = ParseHelper::createParamTuple(csFunc);
		ParseHelper::callLambda(csFunc,std::move(csParams));
		return Ast::Parser::endPipelineParse(sampledTextures);
	}
}
