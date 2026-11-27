#pragma once
#include <Codegen/ParseHelper.h>
#include <Codegen/AST/AST.hpp>
#include <Codegen/AST/Parser.hpp>
#include <Codegen/ComputePipelineObject.h>
#include <concepts>
#include <source_location>
#include <stdexcept>
#include <Compiler/ShaderCodeCompiler.h>
#include <spirv-tools/linker.hpp>
#include <Compiler/ShaderCommon.h>

namespace EmbeddedShader
{
    template<class V, class F>
    concept RasterStagePairMismatch = requires(V& vertex, F& fragment) {
        requires (!decltype(ParseHelper::isMatchInputAndOutput(std::function{vertex}, std::function{fragment}))::value);
    };

    template<ShaderStage Stage>
    struct RasterEntry
    {
        static_assert(Stage == ShaderStage::VertexShader || Stage == ShaderStage::FragmentShader);
        const ShaderCodeModule* const module{};

        template<class Tag> requires requires {
            requires Tag::shaderStage == Stage;
            { Tag::shaderModule } -> std::same_as<const ShaderCodeModule&>;
        }
        explicit RasterEntry(const Tag&) : module(&Tag::shaderModule) {}

        template<class F> requires requires(F& shader) { std::function{shader}; }
        RasterEntry(F&& shader)
            : record_(std::make_shared<Record>([shader = std::forward<F>(shader)]() mutable -> std::shared_ptr<Ast::Value> {
                // ParseHelper copies the function. Keep condition detectors attached to this closure.
                decltype(std::function{shader}) function = std::ref(shader);
                auto parameters = ParseHelper::createParamTuple(function);
                if constexpr (ParseHelper::hasReturnValue(function)) {
                    static_assert(ParseHelper::isReturnVariateProxy(function), "The output of the shader must be a proxy!");
                    return ParseHelper::callLambda(function, std::move(parameters)).node;
                } else {
                    ParseHelper::callLambda(function, std::move(parameters));
                    return {};
                }
            })) {}

    private:
        friend class RasterPipelineObject;
        using Record = std::function<std::shared_ptr<Ast::Value>()>;
        std::shared_ptr<Record> record_;
    };

    using VertexEntry = RasterEntry<ShaderStage::VertexShader>;
    using FragmentEntry = RasterEntry<ShaderStage::FragmentShader>;

	class RasterPipelineObject final
	{
		RasterPipelineObject() = default;
	public:
        static RasterPipelineObject compile(VertexEntry vertex, FragmentEntry fragment, CompilerOption compilerOption = {},
                                            std::source_location sourceLocation = std::source_location::current());
        template<class V, class F> requires RasterStagePairMismatch<V, F>
        static RasterPipelineObject compile(V&&, F&&, CompilerOption = {}, std::source_location = std::source_location::current()) = delete;
	    void updateAutoBind(ShaderCodeCompiler::ConditionInfo vertConditionInfo, ShaderCodeCompiler::ConditionInfo fragConditionInfo);
	    [[nodiscard]] static std::string getCombinedKey(const ShaderCodeCompiler::ConditionInfo& vertConditionInfo, const ShaderCodeCompiler::ConditionInfo& fragConditionInfo);
		std::unique_ptr<ShaderCodeCompiler> vertex;
		std::unique_ptr<ShaderCodeCompiler> fragment;
		std::vector<AutoBindEntry> autoBindEntries;
	private:
		std::vector<AutoBindEntry> outputBindings;
		std::unordered_set<std::string> sampledTextures;
		std::vector<Ast::ParseOutput> parse(const VertexEntry& vertex, const FragmentEntry& fragment);
	};

    inline void RasterPipelineObject::updateAutoBind(ShaderCodeCompiler::ConditionInfo vertConditionInfo, ShaderCodeCompiler::ConditionInfo fragConditionInfo)
    {
        // 每次分支组合变化都会重新反射并重建这张表；先清空，避免旧变体的
        // offset/bindType 残留（否则切回旧条件时 bind_auto_resources 会写两套）。
        autoBindEntries.clear();

        // Collect auto-bind entries from globalStatements (shared across VS/FS):
		// Walk all globally-defined textures and check if they have a back-pointer
		// to a proxy's boundResource_. Match against both vertex and fragment shader's bindInfoPool.
		{
			auto vsCodeModule = vertex->getShaderCode(ShaderLanguage::SpirV, vertConditionInfo);
			auto fsCodeModule = fragment->getShaderCode(ShaderLanguage::SpirV, fragConditionInfo);
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
					    auto* vsInfo = vsCodeModule.shaderResources.findShaderBindInfo(def->texture->name);
					    auto* fsInfo = fsCodeModule.shaderResources.findShaderBindInfo(def->texture->name);
						if (vsInfo || fsInfo)
						{
						    auto* bindInfo = vsInfo ? vsInfo : fsInfo;
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
						auto* vsInfo = vsCodeModule.shaderResources.findShaderBindInfo(def->array->name);
						auto* fsInfo = fsCodeModule.shaderResources.findShaderBindInfo(def->array->name);
						if (vsInfo || fsInfo)
						{
							auto* bindInfo = vsInfo ? vsInfo : fsInfo;
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

            autoBindEntries.insert(autoBindEntries.end(), outputBindings.begin(), outputBindings.end());
		}
    }

    inline std::string RasterPipelineObject::getCombinedKey(const ShaderCodeCompiler::ConditionInfo& vertConditionInfo, const ShaderCodeCompiler::ConditionInfo& fragConditionInfo)
    {
        return "vertex" + ShaderCodeCompiler::getCombinedKey(vertConditionInfo) + "fragment" + ShaderCodeCompiler::getCombinedKey(fragConditionInfo);
    }

    inline RasterPipelineObject RasterPipelineObject::compile(VertexEntry vertex, FragmentEntry fragment,
                                                               CompilerOption compilerOption, std::source_location sourceLocation)
	{
		// Indirect raster shaders read draw packets through Vulkan device addresses.
		compilerOption.compileSpirV = true;
		compilerOption.compileGLSL = compilerOption.compileHLSL = false;
		compilerOption.compileDXIL = compilerOption.compileDXBC = false;
		RasterPipelineObject result;
		if (!vertex.record_ || !fragment.record_)
			throw std::invalid_argument("EDSL compilation requires vertex and fragment shader callbacks");
		auto outputs = result.parse(vertex, fragment);

		auto compileStage = [&](Ast::ParseOutput& output, ShaderStage stage, const auto& record) {
			auto options = compilerOption;
			options.slangModules.insert(options.slangModules.end(), output.sourceModule.begin(), output.sourceModule.end());
			for (auto& branch : output.branches)
				branch.conditionDetector = [record, detect = std::move(branch.conditionDetector)] { return detect(); };
			options.branches = std::move(output.branches);
			options.typeHeader = std::move(output.typeHeader);
			return std::make_unique<ShaderCodeCompiler>(output.output, stage, std::move(options), sourceLocation);
		};
		result.vertex = compileStage(outputs[0], ShaderStage::VertexShader, vertex.record_);
		result.fragment = compileStage(outputs[1], ShaderStage::FragmentShader, fragment.record_);

		// Capture output locations before another pipeline reuses the shared proxies.
		for (auto& stmt : Ast::Parser::getGlobalStatements())
		{
			if (auto* def = dynamic_cast<Ast::DefineUniversalTexture*>(stmt.get()))
			{
				if (def->texture && def->texture->renderTargetLocation >= 0 && def->texture->boundResourceRef)
				{
					result.outputBindings.push_back({
						def->texture->boundResourceRef,
						0, 0,
						static_cast<int32_t>(ShaderCodeModule::ShaderResources::stageOutputs),
						static_cast<uint32_t>(def->texture->renderTargetLocation), nullptr, 0,
					    &def->texture->dirtyVersion
					});
				}
			}
		}
		result.updateAutoBind(result.vertex->getCurrentConditionInfo(), result.fragment->getCurrentConditionInfo());

		return result;
	}

	// Helper: emit output variates for a fragment shader return value.
	// If the return type is an AggregateType, flatten its members into individual
	// DefineOutputVariate entries with incrementing SV_TARGET locations (MRT).
	// If Texture2DType member, extract the texelType as the output type.
	// Otherwise, emit a single DefineOutputVariate at location 0.
	inline void handleFragmentOutput(const std::shared_ptr<Ast::Value>& fsOutput)
	{
		const auto& type = fsOutput->type;

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
				auto memberAccess = Ast::AST::access(fsOutput, member->name, member->type);
				Ast::AST::assign(outputVar, memberAccess);
				++location;
			}
		}
		else
		{
			// Single output (existing behavior)
			auto outputVar = Ast::AST::defineOutputVariate(type, 0);
			Ast::AST::assign(outputVar, fsOutput);
		}
	}

	inline std::vector<Ast::ParseOutput> RasterPipelineObject::parse(const VertexEntry& vertex, const FragmentEntry& fragment)
	{
		for (auto& stmt : Ast::Parser::getGlobalStatements())
		{
			if (auto* def = dynamic_cast<Ast::DefineUniversalTexture*>(stmt.get()))
			{
				if (def->texture)
					def->texture->renderTargetLocation = -1;
			}
		}

		Ast::Parser::beginShaderParse(Ast::ShaderStage::Vertex);
		if (auto value = (*vertex.record_)()) {
			auto output = Ast::AST::defineOutputVariate(value->type, 0);
			Ast::AST::assign(output, value);
		}
		Ast::Parser::beginShaderParse(Ast::ShaderStage::Fragment);
		if (auto value = (*fragment.record_)())
			handleFragmentOutput(value);
		return Ast::Parser::endPipelineParse(sampledTextures);
	}
}
