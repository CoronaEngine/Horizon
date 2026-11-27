#pragma once
#include <Compiler/ShaderCommon.h>

#include <algorithm>
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>
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
        bool sampled = false;
        Ast::AccessPermissions access = Ast::AccessPermissions::None;
        uint8_t stages = 0;

        static void appendActive(std::vector<AutoBindEntry>& entries, const std::vector<Ast::BindingUse>& uses,
                                 ShaderCodeModule& module, const ShaderCodeCompiler::ConditionInfo& conditions,
                                 Ast::ShaderStage stage)
        {
            for (const auto& use : uses) {
                if (std::ranges::any_of(use.conditions, [&](const auto& branch) {
                    return conditions.at(branch.first) != branch.second;
                })) continue;
                auto* info = module.shaderResources.findShaderBindInfo(use.variable->name);
                if (!info) continue;
                AutoBindEntry entry{};
                entry.byteOffset = info->byteOffset;
                entry.typeSize = info->typeSize;
                entry.bindType = static_cast<int32_t>(info->bindType);
                entry.location = info->location;
                if (auto* value = dynamic_cast<Ast::UniformVariate*>(use.variable.get())) {
                    entry.dirtyVersion = &value->dirtyVersion;
                    entry.boundValueRef = value->boundValueRef;
                    entry.boundValueSize = value->boundValueSize;
                } else if (auto* buffer = dynamic_cast<Ast::UniversalArray*>(use.variable.get())) {
                    entry.dirtyVersion = &buffer->dirtyVersion;
                    entry.boundResourceRef = buffer->boundResourceRef;
                    entry.boundResourceIsBuffer = true;
                } else {
                    auto& image = static_cast<Ast::UniversalTexture&>(*use.variable);
                    entry.dirtyVersion = &image.dirtyVersion;
                    entry.boundResourceRef = image.boundResourceRef;
                    entry.sampled = use.sampled;
                }
                if (entry.boundResourceRef) {
                    entry.access = use.access;
                    entry.stages = uint8_t(1u << static_cast<unsigned>(stage));
                }
                auto matching = std::ranges::find_if(entries, [&](const AutoBindEntry& previous) {
                    return previous.dirtyVersion == entry.dirtyVersion && previous.bindType == entry.bindType &&
                           previous.byteOffset == entry.byteOffset;
                });
                if (matching == entries.end()) entries.push_back(entry);
                else {
                    matching->access = matching->access | entry.access;
                    matching->stages |= entry.stages;
                }
            }
        }
	};

	class ComputePipelineObject
	{
	public:
		static ComputePipelineObject compile(auto&& computeShaderCode, ktm::uvec3 numthreads = ktm::uvec3(1), CompilerOption compilerOption = {}, std::source_location sourceLocation = std::source_location::current());
	    [[nodiscard]] static std::string getCombinedKey(const ShaderCodeCompiler::ConditionInfo& conditionInfo);
	    ShaderCodeModule updateAutoBind(const ShaderCodeCompiler::ConditionInfo& compConditionInfo);
		std::unique_ptr<ShaderCodeCompiler> compute;
		ShaderCodeCompiler::ConditionInfo conditions;
		std::vector<AutoBindEntry> autoBindEntries;
	private:
		std::vector<Ast::BindingUse> bindings;
		std::vector<Ast::ParseOutput> parse(auto&& computeShaderCode);
	};

    inline std::string ComputePipelineObject::getCombinedKey(const ShaderCodeCompiler::ConditionInfo& conditionInfo)
    {
        return "compute" + ShaderCodeCompiler::getCombinedKey(conditionInfo);
    }

    inline ShaderCodeModule ComputePipelineObject::updateAutoBind(const ShaderCodeCompiler::ConditionInfo& compConditionInfo)
    {
        auto codeModule = compute->getShaderCode(ShaderLanguage::SpirV, compConditionInfo);
        autoBindEntries.clear();
        AutoBindEntry::appendActive(autoBindEntries, bindings, codeModule, compConditionInfo, Ast::ShaderStage::Compute);
        return codeModule;
    }

    ComputePipelineObject ComputePipelineObject::compile(auto&& computeShaderCode, ktm::uvec3 numthreads,CompilerOption compilerOption,std::source_location sourceLocation)
	{
		// DXBC shader model 5 has no descriptor heap handles.
		compilerOption.compileDXBC = false;
		Generator::SlangGenerator::numthreads = numthreads;
		ComputePipelineObject result;
		auto outputs = result.parse(std::forward<decltype(computeShaderCode)>(computeShaderCode));

	    compilerOption.slangModules.insert(compilerOption.slangModules.end(),outputs[0].sourceModule.begin(), outputs[0].sourceModule.end());

	    result.bindings = std::move(outputs[0].bindings);
	    compilerOption.branches.swap(outputs[0].branches);
	    compilerOption.typeHeader = outputs[0].typeHeader;
		result.compute = std::make_unique<ShaderCodeCompiler>(outputs[0].output, ShaderStage::ComputeShader, compilerOption, sourceLocation);

		result.conditions = result.compute->getCurrentConditionInfo();
		result.updateAutoBind(result.conditions);

		return result;
	}

	std::vector<Ast::ParseOutput> ComputePipelineObject::parse(auto&& computeShaderCode)
	{
		using Function = decltype(std::function{computeShaderCode});
		auto shader = std::make_shared<Function>(std::forward<decltype(computeShaderCode)>(computeShaderCode));
		Function csFunc = std::ref(*shader);

		Ast::Parser::beginShaderParse(Ast::ShaderStage::Compute);
		if (ParseHelper::hasReturnValue(csFunc))
			throw std::logic_error("Compute shader function doesn't have return value");
		auto csParams = ParseHelper::createParamTuple(csFunc);
		ParseHelper::callLambda(csFunc,std::move(csParams));
		auto outputs = Ast::Parser::endPipelineParse();
		// Host condition callbacks can reference captures inside the shader lambda.
		for (auto& branch : outputs.front().branches)
			branch.conditionDetector = [shader, detect = std::move(branch.conditionDetector)] { return detect(); };
		return outputs;
	}

    struct ComputeEntry
    {
        const ShaderCodeModule* const module{};
        std::function<ComputePipelineObject(ktm::uvec3, CompilerOption, const std::source_location&)> compile;

        template<class F> requires std::invocable<F> && std::copy_constructible<std::decay_t<F>>
        ComputeEntry(F&& shader)
            : compile([shader = std::forward<F>(shader)](ktm::uvec3 numthreads, CompilerOption options,
                                                         const std::source_location& location) mutable {
                return ComputePipelineObject::compile(std::move(shader), numthreads, std::move(options), location);
            }) {}

        template<class Tag> requires requires {
            requires Tag::shaderStage == ShaderStage::ComputeShader;
            { Tag::shaderModule } -> std::same_as<const ShaderCodeModule&>;
        }
        explicit ComputeEntry(const Tag&) : module(&Tag::shaderModule) {}
    };
}
