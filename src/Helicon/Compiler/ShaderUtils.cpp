#include "ShaderUtils.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace EmbeddedShader
{
	// ---- 路径清理 ----

	std::string sanitizePath(const std::filesystem::path& p)
	{
		auto s = p.string();
		std::ranges::replace(s, '\\', '_');
		std::ranges::replace(s, '/', '_');
		std::ranges::replace(s, '.', '_');
		std::ranges::replace(s, ':', '_');
		return s;
	}

	std::string sanitizeFilename(const std::filesystem::path& p)
	{
		auto s = p.filename().string();
		for (auto& c : s)
		{
			if (!std::isalnum(static_cast<unsigned char>(c)))
				c = '_';
		}
		if (!s.empty() && std::isdigit(static_cast<unsigned char>(s[0])))
			s = "_" + s;
		return s;
	}

	// ---- SPIR-V 二进制序列化 ----

	void emitSpirVLiteral(std::ostream& out, const std::vector<uint32_t>& spirv)
	{
		for (uint32_t code : spirv)
		{
			out << code << ",";
		}
	}

	// ---- ShaderResources 序列化 ----

	std::string serializeShaderResources(const ShaderCodeModule::ShaderResources& shaderResources)
	{
		std::stringstream result;
		result << "ShaderCodeModule::ShaderResources{";
		result << shaderResources.pushConstantSize << ",";
		result << "\"" << shaderResources.pushConstantName << "\",";
		result << shaderResources.uniformBufferSize << ",";
		result << "\"" << shaderResources.uniformBufferName << "\",";
		result << "{";
		for (const auto& bindInfo : shaderResources.bindInfoPool)
		{
			result << "{";
			result << bindInfo.set << ",";
			result << bindInfo.binding << ",";
			result << bindInfo.location << ",";
			result << "\"" << bindInfo.semantic << "\",";
			result << "\"" << bindInfo.variateName << "\",";
			result << "\"" << bindInfo.typeName << "\",";
			result << bindInfo.elementCount << ",";
			result << bindInfo.typeSize << ",";
			result << bindInfo.byteOffset << ",";
			result << "static_cast<EmbeddedShader::ShaderCodeModule::ShaderResources::BindType>(" << bindInfo.bindType << ")";
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
