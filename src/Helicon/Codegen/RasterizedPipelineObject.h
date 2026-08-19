#pragma once
#include <Codegen/ParseHelper.h>
#include <Codegen/AST/AST.hpp>
#include <Codegen/AST/Parser.hpp>
#include <Codegen/ComputePipelineObject.h>
#include <source_location>
#include <Compiler/ShaderCodeCompiler.h>
#include <spirv-tools/linker.hpp>
#include <Compiler/ShaderCommon.h>

namespace EmbeddedShader
{
	class RasterizedPipelineObject final
	{
		RasterizedPipelineObject() = default;
	public:
		static RasterizedPipelineObject compile(auto&& vertexShaderCode, auto&& fragmentShaderCode, CompilerOption compilerOption = {}, std::source_location sourceLocation = std::source_location::current());
	    void updateAutoBind(bool bindless, ShaderCodeCompiler::ConditionInfo vertConditionInfo, ShaderCodeCompiler::ConditionInfo fragConditionInfo);
	    [[nodiscard]] static std::string getCombinedKey(const ShaderCodeCompiler::ConditionInfo& vertConditionInfo, const ShaderCodeCompiler::ConditionInfo& fragConditionInfo);
		std::unique_ptr<ShaderCodeCompiler> vertex;
		std::unique_ptr<ShaderCodeCompiler> fragment;
		std::vector<AutoBindEntry> autoBindEntries;
	private:
		static std::vector<Ast::ParseOutput> parse(auto&& vertexShaderCode, auto&& fragmentShaderCode);
	};

    inline void RasterizedPipelineObject::updateAutoBind(bool bindless, ShaderCodeCompiler::ConditionInfo vertConditionInfo, ShaderCodeCompiler::ConditionInfo fragConditionInfo)
    {
        // Collect auto-bind entries from globalStatements (shared across VS/FS):
		// Walk all globally-defined textures and check if they have a back-pointer
		// to a proxy's boundResource_. Match against both vertex and fragment shader's bindInfoPool.
		{
			auto vsCodeModule = vertex->getShaderCode(ShaderLanguage::SpirV, bindless,vertConditionInfo);
			auto fsCodeModule = fragment->getShaderCode(ShaderLanguage::SpirV, bindless, fragConditionInfo);
			auto& globals = Ast::Parser::getGlobalStatements();
			for (auto& stmt : globals)
			{
				if (auto* def = dynamic_cast<Ast::DefineUniformVariate*>(stmt.get()))
				{
					if (def->variate && def->variate->boundValueRef)
					{
					    auto* vsInfo = vsCodeModule.shaderResources.findShaderBindInfo(def->variate->name);
					    auto* fsInfo = fsCodeModule.shaderResources.findShaderBindInfo(def->variate->name);
					    if (vsInfo || fsInfo)
					    {
					        auto* bindInfo = vsInfo ? vsInfo : fsInfo;
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
						auto effectiveBindType = [&](ShaderCodeModule::ShaderResources::BindType reflected) {
							if (reflected != ShaderCodeModule::ShaderResources::pushConstantMembers)
								return static_cast<int32_t>(reflected);
							auto* texType = dynamic_cast<Ast::TextureType*>(def->texture->type.get());
							const bool isSampled = texType != nullptr && texType->name.rfind("Sampler", 0) == 0;
							return static_cast<int32_t>(isSampled ? ShaderCodeModule::ShaderResources::sampledImages
							                                      : ShaderCodeModule::ShaderResources::storageTexture);
						};
					    auto* vsInfo = vsCodeModule.shaderResources.findShaderBindInfo(def->texture->name);
					    auto* fsInfo = fsCodeModule.shaderResources.findShaderBindInfo(def->texture->name);
						if (vsInfo || fsInfo)
						{
						    auto* bindInfo = vsInfo ? vsInfo : fsInfo;
							autoBindEntries.push_back({
								def->texture->boundResourceRef,
								bindInfo->byteOffset,
								bindInfo->typeSize,
								effectiveBindType(bindInfo->bindType),
								bindInfo->location,
							    nullptr,
							    0,
							    &def->texture->dirtyVersion
							});
						}
					}
				}

				// StructuredBuffer(EDSL 的 Array<T>)。bindless 下 handle 在 push
				// constant 里,反射报 pushConstantMembers,重映射为 storageBuffer。
				if (auto* def = dynamic_cast<Ast::DefineUniversalArray*>(stmt.get()))
				{
					if (def->array && def->array->boundResourceRef)
					{
						auto* vsInfo = vsCodeModule.shaderResources.findShaderBindInfo(def->array->name);
						auto* fsInfo = fsCodeModule.shaderResources.findShaderBindInfo(def->array->name);
						if (vsInfo || fsInfo)
						{
							auto* bindInfo = vsInfo ? vsInfo : fsInfo;
							int32_t arrayBindType = static_cast<int32_t>(bindInfo->bindType);
							if (bindInfo->bindType == ShaderCodeModule::ShaderResources::pushConstantMembers)
								arrayBindType = static_cast<int32_t>(ShaderCodeModule::ShaderResources::storageBuffer);
							autoBindEntries.push_back({
								def->array->boundResourceRef,
								bindInfo->byteOffset,
								bindInfo->typeSize,
								arrayBindType,
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

			// Collect render target auto-bind entries from operator() calls in FS.
			// Textures with renderTargetLocation >= 0 were used as render target outputs.
			for (auto& stmt : globals)
			{
				if (auto* def = dynamic_cast<Ast::DefineUniversalTexture*>(stmt.get()))
				{
					if (def->texture && def->texture->renderTargetLocation >= 0 && def->texture->boundResourceRef)
					{
						autoBindEntries.push_back({
							def->texture->boundResourceRef,
							0, 0,
							static_cast<int32_t>(ShaderCodeModule::ShaderResources::stageOutputs),
							static_cast<uint32_t>(def->texture->renderTargetLocation), nullptr, 0,
						    &def->texture->dirtyVersion
						});
					}
				}
			}
		}
    }

    inline std::string RasterizedPipelineObject::getCombinedKey(const ShaderCodeCompiler::ConditionInfo& vertConditionInfo, const ShaderCodeCompiler::ConditionInfo& fragConditionInfo)
    {
        return "vertex" + ShaderCodeCompiler::getCombinedKey(vertConditionInfo) + "fragment" + ShaderCodeCompiler::getCombinedKey(fragConditionInfo);
    }

    RasterizedPipelineObject RasterizedPipelineObject::compile(auto&& vertexShaderCode, auto&& fragmentShaderCode, CompilerOption compilerOption, std::source_location sourceLocation)
	{
		Ast::Parser::setBindless(false);
		auto outputs = parse(vertexShaderCode,fragmentShaderCode);

        auto& vertSlangModules = compilerOption.slangModules;
        vertSlangModules.insert(vertSlangModules.end(),outputs[0].sourceModule.begin(), outputs[0].sourceModule.end());

        auto fraqSlangModules = compilerOption.slangModules;
        fraqSlangModules.insert(fraqSlangModules.end(),outputs[1].sourceModule.begin(), outputs[1].sourceModule.end());

        RasterizedPipelineObject result;
	    compilerOption.slangModules.swap(vertSlangModules);
	    compilerOption.branches = outputs[0].branches;
	    compilerOption.typeHeader = outputs[0].typeHeader;
		result.vertex = std::make_unique<ShaderCodeCompiler>(outputs[0].output,ShaderStage::VertexShader, ShaderLanguage::Slang,compilerOption, sourceLocation);
	    compilerOption.slangModules.swap(fraqSlangModules);
	    compilerOption.branches = outputs[1].branches;
	    compilerOption.typeHeader = outputs[1].typeHeader;
		result.fragment = std::make_unique<ShaderCodeCompiler>(outputs[1].output,ShaderStage::FragmentShader, ShaderLanguage::Slang,compilerOption, sourceLocation);

		if (compilerOption.enableBindless)
		{
			Ast::Parser::setBindless(true);
			outputs = parse(std::forward<decltype(vertexShaderCode)>(vertexShaderCode),
							std::forward<decltype(fragmentShaderCode)>(fragmentShaderCode));
		    compilerOption.slangModules.swap(vertSlangModules);
		    compilerOption.branches = outputs[0].branches;
		    compilerOption.typeHeader = outputs[0].typeHeader;
			result.vertex->compile(outputs[0].output, ShaderStage::VertexShader, ShaderLanguage::Slang, compilerOption);
		    compilerOption.slangModules.swap(fraqSlangModules);
		    compilerOption.branches = outputs[1].branches;
		    compilerOption.typeHeader = outputs[1].typeHeader;
			result.fragment->compile(outputs[1].output, ShaderStage::FragmentShader, ShaderLanguage::Slang, compilerOption);
		}

		result.updateAutoBind(compilerOption.enableBindless, result.vertex->getCurrentConditionInfo(), result.fragment->getCurrentConditionInfo());

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
				if (auto tex2dType = std::dynamic_pointer_cast<Ast::TextureType>(member->type))
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


		for (auto& stmt : Ast::Parser::getGlobalStatements())
		{
			if (auto* def = dynamic_cast<Ast::DefineUniversalTexture*>(stmt.get()))
			{
				if (def->texture)
					def->texture->renderTargetLocation = -1;
			}
		}

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
