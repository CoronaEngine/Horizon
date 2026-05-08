#pragma once
#include <Codegen/ParseHelper.h>
#include <Codegen/AST/AST.hpp>
#include <Codegen/AST/Parser.hpp>
#include <Codegen/ComputePipelineObject.h>
#include <source_location>
#include <Compiler/ShaderCodeCompiler.h>
#include <spirv-tools/linker.hpp>

namespace EmbeddedShader
{
	class RasterizedPipelineObject final
	{
		RasterizedPipelineObject() = default;
	public:
		static RasterizedPipelineObject compile(auto&& vertexShaderCode, auto&& fragmentShaderCode, CompilerOption compilerOption = {}, std::source_location sourceLocation = std::source_location::current());
		std::unique_ptr<ShaderCodeCompiler> vertex;
		std::unique_ptr<ShaderCodeCompiler> fragment;
		std::vector<AutoBindEntry> autoBindEntries;
	private:
		static std::vector<Ast::ParseOutput> parse(auto&& vertexShaderCode, auto&& fragmentShaderCode);
	};

	RasterizedPipelineObject RasterizedPipelineObject::compile(auto&& vertexShaderCode, auto&& fragmentShaderCode, CompilerOption compilerOption, std::source_location sourceLocation)
	{
		Ast::Parser::setBindless(false);
		auto outputs = parse(vertexShaderCode,fragmentShaderCode);
		std::vector<std::vector<uint32_t>> link[2];
		if (compilerOption.spvLinkBinary)
		{
			link[0] = *compilerOption.spvLinkBinary;
			link[1] = *compilerOption.spvLinkBinary;
		}
		for (auto spvSourcePtr : outputs[0].sourceSpv)
		{
			if (spvSourcePtr)
				link[0].push_back(*spvSourcePtr);
		}
		for (auto spvSourcePtr : outputs[1].sourceSpv)
		{
			if (spvSourcePtr)
				link[1].push_back(*spvSourcePtr);
		}
		RasterizedPipelineObject result;
		compilerOption.spvLinkBinary = &link[0];
		result.vertex = std::make_unique<ShaderCodeCompiler>(outputs[0].output,ShaderStage::VertexShader, ShaderLanguage::Slang,compilerOption, sourceLocation);
		compilerOption.spvLinkBinary = &link[1];
		result.fragment = std::make_unique<ShaderCodeCompiler>(outputs[1].output,ShaderStage::FragmentShader, ShaderLanguage::Slang,compilerOption, sourceLocation);

		if (compilerOption.enableBindless)
		{
			Ast::Parser::setBindless(true);
			outputs = parse(std::forward<decltype(vertexShaderCode)>(vertexShaderCode),
							std::forward<decltype(fragmentShaderCode)>(fragmentShaderCode));
			compilerOption.spvLinkBinary = &link[0];
			result.vertex->compile(outputs[0].output, ShaderStage::VertexShader, ShaderLanguage::Slang, compilerOption);
			compilerOption.spvLinkBinary = &link[1];
			result.fragment->compile(outputs[1].output, ShaderStage::FragmentShader, ShaderLanguage::Slang, compilerOption);
		}

		// Collect auto-bind entries from globalStatements (shared across VS/FS):
		// Walk all globally-defined textures and check if they have a back-pointer
		// to a proxy's boundResource_. Match against both vertex and fragment shader's bindInfoPool.
		{
			auto vsCodeModule = result.vertex->getShaderCode(ShaderLanguage::SpirV, compilerOption.enableBindless);
			auto fsCodeModule = result.fragment->getShaderCode(ShaderLanguage::SpirV, compilerOption.enableBindless);
			auto& globals = Ast::Parser::getGlobalStatements();
			for (auto& stmt : globals)
			{
				if (auto* def = dynamic_cast<Ast::DefineUniversalTexture2D*>(stmt.get()))
				{
					if (def->texture && def->texture->boundResourceRef)
					{
						if (auto* bindInfo = vsCodeModule.shaderResources.findShaderBindInfo(def->texture->name))
						{
							result.autoBindEntries.push_back({
								def->texture->boundResourceRef,
								bindInfo->byteOffset,
								bindInfo->typeSize,
								static_cast<int32_t>(bindInfo->bindType),
								bindInfo->location
							});
						}
						if (auto* bindInfo = fsCodeModule.shaderResources.findShaderBindInfo(def->texture->name))
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

			// Collect render target auto-bind entries from operator() calls in FS.
			// Textures with renderTargetLocation >= 0 were used as render target outputs.
			for (auto& stmt : globals)
			{
				if (auto* def = dynamic_cast<Ast::DefineUniversalTexture2D*>(stmt.get()))
				{
					if (def->texture && def->texture->renderTargetLocation >= 0 && def->texture->boundResourceRef)
					{
						result.autoBindEntries.push_back({
							def->texture->boundResourceRef,
							0, 0,
							static_cast<int32_t>(ShaderCodeModule::ShaderResources::stageOutputs),
							static_cast<uint32_t>(def->texture->renderTargetLocation)
						});
					}
				}
			}
		}

		return result;
	}

	// Helper: emit output variates for a fragment shader return value.
	// If the return type is an AggregateType, flatten its members into individual
	// DefineOutputVariate entries with incrementing SV_TARGET locations (MRT).
	// If Texture2DType member, extract the texelType as the output type.
	// Otherwise, emit a single DefineOutputVariate at location 0.
	template<typename FsOutput>
	static void handleFragmentOutput(FsOutput& fsOutput)
	{
		auto* variate = reinterpret_cast<Ast::Variate*>(fsOutput.node.get());
		auto type = variate->type;

		if (auto aggregateType = std::dynamic_pointer_cast<Ast::AggregateType>(type))
		{
			// MRT: flatten each member to an individual output with SV_TARGET{location}
			size_t location = 0;
			for (auto& member : aggregateType->members)
			{
				// For Texture2DType members, the output type is the texel type (e.g., float4)
				std::shared_ptr<Ast::Type> outputType;
				if (auto tex2dType = std::dynamic_pointer_cast<Ast::Texture2DType>(member->type))
					outputType = tex2dType->texelType;
				else
					outputType = member->type;

				auto outputVar = Ast::AST::defineOutputVariate(outputType, location);
				auto memberAccess = Ast::AST::access(fsOutput.node, member->name, member->type);
				Ast::AST::assign(outputVar, memberAccess);
				++location;
			}
		}
		else
		{
			// Single output (existing behavior)
			auto outputVar = Ast::AST::defineOutputVariate(type, 0);
			Ast::AST::assign(outputVar, fsOutput.node);
		}
	}

	std::vector<Ast::ParseOutput> RasterizedPipelineObject::parse(auto&& vertexShaderCode, auto&& fragmentShaderCode)
	{
		auto vsFunc = std::function(std::forward<decltype(vertexShaderCode)>(vertexShaderCode));
		auto fsFunc = std::function(std::forward<decltype(fragmentShaderCode)>(fragmentShaderCode));

		static_assert(ParseHelper::isMatchInputAndOutput(vsFunc,fsFunc), "The output of the vertex shader and the input of the fragment shader must match!");

		Ast::Parser::beginShaderParse(Ast::ShaderStage::Vertex);
		auto vsParams = ParseHelper::createParamTuple(vsFunc);
		if constexpr (ParseHelper::hasReturnValue(vsFunc))
		{
			auto vsOutput = ParseHelper::callLambda(vsFunc,std::move(vsParams));

			static_assert(ParseHelper::isReturnVariateProxy(vsFunc), "The output of the shader must be a proxy!");
			auto outputVar = Ast::AST::defineOutputVariate(reinterpret_cast<Ast::Variate*>(vsOutput.node.get())->type,0);
			Ast::AST::assign(outputVar,vsOutput.node);

			Ast::Parser::beginShaderParse(Ast::ShaderStage::Fragment);
			auto fsParam = ParseHelper::createParam(fsFunc);
			if constexpr (!ParseHelper::hasReturnValue(fsFunc))
				ParseHelper::callLambda(fsFunc, std::move(fsParam));
			else
			{
				auto fsOutput = ParseHelper::callLambda(fsFunc, std::move(fsParam));
				static_assert(ParseHelper::isReturnVariateProxy(fsFunc), "The output of the shader must be a proxy!");
				handleFragmentOutput(fsOutput);
			}
		}
		else
		{
			ParseHelper::callLambda(vsFunc,std::move(vsParams));
			Ast::Parser::beginShaderParse(Ast::ShaderStage::Fragment);
			if constexpr (!ParseHelper::hasReturnValue(fsFunc))
				ParseHelper::callLambda(fsFunc);
			else
			{
				auto fsOutput = ParseHelper::callLambda(fsFunc);
				static_assert(ParseHelper::isReturnVariateProxy(fsFunc), "The output of the shader must be a proxy!");
				handleFragmentOutput(fsOutput);
			}
		}
		return Ast::Parser::endPipelineParse();
	}
}
