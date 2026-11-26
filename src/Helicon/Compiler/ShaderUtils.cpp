#include "ShaderUtils.h"
#include <Compiler/ShaderCommon.h>
#include <spirv-tools/libspirv.hpp>
#include <spirv-tools/optimizer.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace EmbeddedShader
{
    bool rasterShaderCanMergeWithoutUses(const ShaderCodeModule& module)
    {
        const auto* code = std::get_if<std::vector<uint32_t>>(&module.shaderCode);
        if (!code || code->empty()) return false;

        // Pointer types and the values of those types share one SPIR-V ID namespace.
        enum Storage : uint32_t { Input = 1, Uniform = 2, Output = 3, Private = 6, Function = 7,
                                  PushConstant = 9, StorageBuffer = 12, PhysicalStorageBuffer = 5349 };
        std::unordered_map<uint32_t, Storage> pointerStorage;
        bool inFunction = false;
        spvtools::SpirvTools parser(SPV_ENV_VULKAN_1_4);
        return parser.Parse(*code, [](auto, const auto&) { return SPV_SUCCESS; },
            [&](const spv_parsed_instruction_t& inst) {
                const std::string_view op = spvOpcodeString(inst.opcode);
                if (op == "TypePointer")
                    pointerStorage.emplace(inst.result_id, static_cast<Storage>(inst.words[2]));
                else if (const auto type = pointerStorage.find(inst.type_id); type != pointerStorage.end())
                    pointerStorage.emplace(inst.result_id, type->second);

                if (op == "Function") { inFunction = true; return SPV_SUCCESS; }
                if (op == "FunctionEnd") { inFunction = false; return SPV_SUCCESS; }
                if (!inFunction) return SPV_SUCCESS;

                if (op == "Store" || op == "Load") {
                    const auto ptr = pointerStorage.find(inst.words[op == "Store" ? 1 : 3]);
                    if (ptr == pointerStorage.end()) return SPV_REQUESTED_TERMINATION;
                    const auto storage = ptr->second;
                    if (storage == Output || storage == Private || storage == Function) return SPV_SUCCESS;
                    if (op == "Load" && (storage == Input || storage == Uniform || storage == PushConstant ||
                                         storage == StorageBuffer || storage == PhysicalStorageBuffer)) return SPV_SUCCESS;
                    return SPV_REQUESTED_TERMINATION;
                }
                if (op == "ExtInst") {
                    // GLSL.std.450 includes two instructions with output pointer operands (Modf and Frexp).
                    if (inst.ext_inst_type == SPV_EXT_INST_TYPE_GLSL_STD_450 && inst.words[4] <= 81 &&
                        inst.words[4] != 35 && inst.words[4] != 51) return SPV_SUCCESS;
                    return SPV_REQUESTED_TERMINATION;
                }
                // Core SPIR-V opcode groups: composites; conversions, arithmetic and derivatives; control flow.
                if ((inst.opcode >= 77 && inst.opcode <= 84) || (inst.opcode >= 109 && inst.opcode <= 215) ||
                    (inst.opcode >= 245 && inst.opcode <= 255)) return SPV_SUCCESS;
                if (op == "Variable" || op == "FunctionParameter" || op == "FunctionCall" ||
                    op == "AccessChain" || op == "InBoundsAccessChain" || op == "PtrAccessChain" ||
                    op == "InBoundsPtrAccessChain" || op == "CopyLogical" || op == "Nop" ||
                    op == "Line" || op == "NoLine") return SPV_SUCCESS;
                // Image accesses may alias an attachment. Atomics, copies and unknown operations need a boundary.
                return SPV_REQUESTED_TERMINATION;
            });
    }

    void linkRasterDrawParameter(ShaderCodeModule& vertex, ShaderCodeModule& fragment)
    {
        using Resources = ShaderCodeModule::ShaderResources;
        constexpr uint32_t parameterLocation = 15;
        const auto isParameter = [](const Resources::ShaderBindInfo& binding) {
            return binding.location == parameterLocation && binding.typeName == "uint" && binding.variateName == "hzParameterWord";
        };
        const auto isOutput = [&](const auto& binding) {
            return binding.bindType == Resources::stageOutputs && isParameter(binding);
        };
        if (!std::ranges::any_of(vertex.shaderResources.bindInfoPool, isOutput)) return;

        // Slang layout includes declared inputs even when linking a $IF branch removed the read.
        auto& fs = std::get<std::vector<uint32_t>>(fragment.shaderCode);
        std::unordered_set<uint32_t> liveLocations, liveBuiltins;
        spvtools::Optimizer analyze(SPV_ENV_VULKAN_1_4);
        analyze.RegisterPass(spvtools::CreateAnalyzeLiveInputPass(&liveLocations, &liveBuiltins));
        analyze.RegisterPass(spvtools::CreateRemoveUnusedInterfaceVariablesPass());
        analyze.RegisterPass(spvtools::CreateDeadVariableEliminationPass());
        if (!analyze.Run(fs.data(), fs.size(), &fs))
            throw std::runtime_error("Could not analyze the fragment SPIR-V interface");
        if (liveLocations.contains(parameterLocation)) return;

        // The draw offset is the only output we own. Keep the other reflected varyings,
        // including their full location ranges (for example, a matrix before location 15).
        for (const auto& binding : vertex.shaderResources.bindInfoPool) {
            if (binding.bindType == Resources::stageOutputs && binding.location != parameterLocation)
                liveLocations.insert(binding.location);
        }
        auto& vs = std::get<std::vector<uint32_t>>(vertex.shaderCode);
        spvtools::Optimizer prune(SPV_ENV_VULKAN_1_4);
        prune.RegisterPass(spvtools::CreateEliminateDeadOutputStoresPass(&liveLocations, &liveBuiltins));
        prune.RegisterPass(spvtools::CreateRemoveUnusedInterfaceVariablesPass());
        prune.RegisterPass(spvtools::CreateDeadVariableEliminationPass());
        if (!prune.Run(vs.data(), vs.size(), &vs))
            throw std::runtime_error("Could not link the indirect raster vertex outputs");
        std::erase_if(vertex.shaderResources.bindInfoPool, isOutput);
        std::erase_if(fragment.shaderResources.bindInfoPool, [&](const auto& binding) {
            return binding.bindType == Resources::stageInputs && isParameter(binding);
        });
    }

	// ---- 路径清理 ----

	std::string sanitizePath(const std::filesystem::path& p)
	{
		auto s = p.string();
		// C++ identifiers must not depend on path punctuation or the process locale.
		for (auto& c : s)
		{
			const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
			const bool digit = c >= '0' && c <= '9';
			if (!letter && !digit && c != '_') c = '_';
		}
		if (s.empty()) return "_";
		if (s[0] >= '0' && s[0] <= '9') s.insert(s.begin(), '_');
		return s;
	}

	std::string sanitizeFilename(const std::filesystem::path& p)
	{
		return sanitizePath(p.filename());
	}

	// ---- SPIR-V 二进制序列化 ----

	void emitSpirVLiteral(std::ostream& out, const std::vector<uint32_t>& spirv)
	{
		for (uint32_t code : spirv)
		{
			out << code << ",";
		}
	}

    void emitSlangModuleLiteral(std::ostream& out, const std::vector<uint8_t>& slangModule)
	{
	    for (auto code : slangModule)
	    {
	        out << std::to_string(code) << ",";
	    }
	}

	// ---- C++ string literal serialization ----

	void emitCppStringLiteral(std::ostream& out, std::string_view text)
	{
		out << '"';
		for (unsigned char ch : text)
		{
			switch (ch)
			{
			case '\\':
				out << "\\\\";
				break;
			case '"':
				out << "\\\"";
				break;
			case '\n':
				out << "\\n\"\n\"";
				break;
			case '\r':
				out << "\\r";
				break;
			case '\t':
				out << "\\t";
				break;
			default:
				if (ch < 0x20)
				{
					out << '\\'
						<< static_cast<char>('0' + ((ch >> 6) & 0x7))
						<< static_cast<char>('0' + ((ch >> 3) & 0x7))
						<< static_cast<char>('0' + (ch & 0x7));
				}
				else
				{
					out << static_cast<char>(ch);
				}
				break;
			}
		}
		out << '"';
	}

	// ---- ShaderResources 序列化 ----

	std::string serializeShaderResources(const ShaderCodeModule::ShaderResources& shaderResources)
	{
		std::stringstream result;
		result << "::EmbeddedShader::ShaderCodeModule::ShaderResources{";
		result << shaderResources.pushConstantSize << ",";
		emitCppStringLiteral(result, shaderResources.pushConstantName);
		result << "," << shaderResources.uniformBufferSize << ",";
		emitCppStringLiteral(result, shaderResources.uniformBufferName);
		result << ",{";
		for (const auto& bindInfo : shaderResources.bindInfoPool)
		{
			result << "{";
			result << bindInfo.set << ",";
			result << bindInfo.binding << ",";
			result << bindInfo.location << ",";
			emitCppStringLiteral(result, bindInfo.semantic);
			result << ",";
			emitCppStringLiteral(result, bindInfo.variateName);
			result << ",";
			emitCppStringLiteral(result, bindInfo.typeName);
			result << "," << bindInfo.elementCount << ",";
			result << bindInfo.typeSize << ",";
			result << bindInfo.byteOffset << ",";
			result << "static_cast<::EmbeddedShader::ShaderCodeModule::ShaderResources::BindType>(" << bindInfo.bindType << ")";
			result << "},";
		}
		result << "}";
		// entryPointInfoPool：保留 compute 的 [numthreads]/local_size 反射，
		// 否则 hardcode-shader 构建加载回来 numthreads 丢失，dispatch_groups 会误算。
		result << ",{";
		for (const auto& entryPoint : shaderResources.entryPointInfoPool)
		{
			result << "{";
			emitCppStringLiteral(result, entryPoint.name);
			result << ",static_cast<::EmbeddedShader::ShaderStage>(" << static_cast<uint32_t>(entryPoint.stage) << "),";
			result << "::ktm::uvec3(" << entryPoint.numthreads.x << "," << entryPoint.numthreads.y << "," << entryPoint.numthreads.z << ")";
			result << "},";
		}
		result << "}";
		result << "}";
		return result.str();
	}

	// ---- Shader Stage 推断 ----

	ShaderStage inferShaderStage(const std::filesystem::path& shaderPath, std::string_view code)
	{
		auto stem = shaderPath.stem().string();
		auto extension = shaderPath.extension().string();

		// 优先级 1：扩展名
		if (extension == ".frag" || extension == ".fs")
			return ShaderStage::FragmentShader;
		if (extension == ".comp" || extension == ".cs")
			return ShaderStage::ComputeShader;
		if (extension == ".vert" || extension == ".vs")
			return ShaderStage::VertexShader;

		// 优先级 2：文件名关键字（stem 可能含双扩展名如 xxx.frag.glsl → stem = "xxx.frag"）
		if (stem.ends_with(".frag") || stem.find("frag") != std::string::npos)
			return ShaderStage::FragmentShader;
		if (stem.ends_with(".comp") || stem.find("compute") != std::string::npos)
			return ShaderStage::ComputeShader;
		if (stem.ends_with(".vert") || stem.find("vert") != std::string::npos)
			return ShaderStage::VertexShader;

		// 优先级 3：代码内容特征
		if (code.find("gl_FragCoord") != std::string_view::npos ||
		    code.find("gl_SampleID") != std::string_view::npos ||
		    (code.find("out vec4") != std::string_view::npos && code.find("gl_Position") == std::string_view::npos))
			return ShaderStage::FragmentShader;

		if (code.find("gl_GlobalInvocationID") != std::string_view::npos ||
		    code.find("gl_WorkGroupID") != std::string_view::npos ||
		    code.find("gl_LocalInvocationID") != std::string_view::npos)
			return ShaderStage::ComputeShader;

		// 默认
		return ShaderStage::VertexShader;
	}
}
