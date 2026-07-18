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
	};

	class ComputePipelineObject
	{
	public:
		static ComputePipelineObject compile(auto&& computeShaderCode, ktm::uvec3 numthreads = ktm::uvec3(1), CompilerOption compilerOption = {}, std::source_location sourceLocation = std::source_location::current());
	    [[nodiscard]] std::string getCombinedKey() const;
	    void updateAutoBind(bool bindless);
		std::unique_ptr<ShaderCodeCompiler> compute;
		std::vector<AutoBindEntry> autoBindEntries;
	private:
		static std::vector<Ast::ParseOutput> parse(auto&& computeShaderCode);
	};

    inline std::string ComputePipelineObject::getCombinedKey() const
    {
        return "compute" + compute->getCombinedKey();
    }

    inline void ComputePipelineObject::updateAutoBind(bool bindless)
    {
        // Collect auto-bind entries from globalStatements:
        // Walk all globally-defined textures and check if they have a back-pointer
        // to a proxy's boundResource_. Filter by membership in the compiled shader's bindInfoPool.
        {
            auto codeModule = compute->getShaderCode(ShaderLanguage::SpirV, bindless);
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
                                def->variate->boundValueSize
                            });
                        }
                    }
                }

                if (auto* def = dynamic_cast<Ast::DefineUniversalTexture2D*>(stmt.get()))
                {
                    if (def->texture && def->texture->boundResourceRef)
                    {
                        if (auto* bindInfo = codeModule.shaderResources.findShaderBindInfo(def->texture->name))
                        {
                            autoBindEntries.push_back({
                                def->texture->boundResourceRef,
                                bindInfo->byteOffset,
                                bindInfo->typeSize,
                                static_cast<int32_t>(bindInfo->bindType),
                                bindInfo->location
                            });
                        }
                    }
                }
            }
        }
    }

    ComputePipelineObject ComputePipelineObject::compile(auto&& computeShaderCode, ktm::uvec3 numthreads,CompilerOption compilerOption,std::source_location sourceLocation)
	{
		Generator::SlangGenerator::numthreads = numthreads;
		Ast::Parser::setBindless(false);
		auto outputs = parse(computeShaderCode);
	    compilerOption.slangModules.insert(compilerOption.slangModules.end(),outputs[0].sourceModule.begin(), outputs[0].sourceModule.end());

	    compilerOption.branches.swap(outputs[0].branches);
	    compilerOption.typeHeader = outputs[0].typeHeader;
		ComputePipelineObject result;
		result.compute = std::make_unique<ShaderCodeCompiler>(outputs[0].output, ShaderStage::ComputeShader, ShaderLanguage::Slang,compilerOption,sourceLocation);

		if (compilerOption.enableBindless)
		{
			Ast::Parser::setBindless(true);
			outputs = parse(std::forward<decltype(computeShaderCode)>(computeShaderCode));
            compilerOption.branches.swap(outputs[0].branches);
            compilerOption.typeHeader = outputs[0].typeHeader;
            result.compute->compile(outputs[0].output, ShaderStage::ComputeShader, ShaderLanguage::Slang,compilerOption);
		}

		result.updateAutoBind(compilerOption.enableBindless);

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
		return Ast::Parser::endPipelineParse();
	}
}
