#pragma once
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
	};

	class ComputePipelineObject
	{
	public:
		static ComputePipelineObject compile(auto&& computeShaderCode, ktm::uvec3 numthreads = ktm::uvec3(1), CompilerOption compilerOption = {}, std::source_location sourceLocation = std::source_location::current());
		std::unique_ptr<ShaderCodeCompiler> compute;
		std::vector<AutoBindEntry> autoBindEntries;
	private:
		static std::vector<Ast::ParseOutput> parse(auto&& computeShaderCode);
	};

	ComputePipelineObject ComputePipelineObject::compile(auto&& computeShaderCode, ktm::uvec3 numthreads,CompilerOption compilerOption,std::source_location sourceLocation)
	{
		Generator::SlangGenerator::numthreads = numthreads;
		Ast::Parser::setBindless(false);
		auto outputs = parse(computeShaderCode);
		std::vector<std::vector<uint32_t>> link;
		if (compilerOption.spvLinkBinary)
		{
			link = *compilerOption.spvLinkBinary;
		}
		for (auto spvSourcePtr : outputs[0].sourceSpv)
		{
			if (spvSourcePtr)
				link.push_back(*spvSourcePtr);
		}
		compilerOption.spvLinkBinary = &link;
		ComputePipelineObject result;
		result.compute = std::make_unique<ShaderCodeCompiler>(outputs[0].output, ShaderStage::ComputeShader, ShaderLanguage::Slang,compilerOption,sourceLocation);

		if (compilerOption.enableBindless)
		{
			Ast::Parser::setBindless(true);
			outputs = parse(std::forward<decltype(computeShaderCode)>(computeShaderCode));
			result.compute->compile(outputs[0].output, ShaderStage::ComputeShader, ShaderLanguage::Slang,compilerOption);
		}

		// Collect auto-bind entries from globalStatements:
		// Walk all globally-defined textures and check if they have a back-pointer
		// to a proxy's boundResource_. Filter by membership in the compiled shader's bindInfoPool.
		{
			auto codeModule = result.compute->getShaderCode(ShaderLanguage::SpirV, compilerOption.enableBindless);
			auto& globals = Ast::Parser::getGlobalStatements();
			for (auto& stmt : globals)
			{
				if (auto* def = dynamic_cast<Ast::DefineUniversalTexture2D*>(stmt.get()))
				{
					if (def->texture && def->texture->boundResourceRef)
					{
						if (auto* bindInfo = codeModule.shaderResources.findShaderBindInfo(def->texture->name))
						{
							result.autoBindEntries.push_back({
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
