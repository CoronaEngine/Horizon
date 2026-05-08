#pragma once

#include "ShaderCodeCompiler.h"
#include <filesystem>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace EmbeddedShader
{
	// ---- 路径清理 ----

	// 将路径字符串中的特殊符号替换为下划线，返回可用作 C++ 标识符的字符串
	// 对完整路径替换（用于生成 map key 和 static inline 变量名）
	std::string sanitizePath(const std::filesystem::path& p);

	// 仅取文件名部分并清理为合法标识符（用于生成 .hpp 外层 struct 名）
	std::string sanitizeFilename(const std::filesystem::path& p);

	// ---- SPIR-V 二进制序列化 ----

	// 将 SPIR-V 二进制输出为逗号分隔的 uint32_t 序列（不含外围大括号）
	void emitSpirVLiteral(std::ostream& out, const std::vector<uint32_t>& spirv);

	// ---- ShaderResources 序列化 ----

	// 将 ShaderResources 序列化为 C++ 聚合初始化表达式
	std::string serializeShaderResources(const ShaderCodeModule::ShaderResources& resources);

	// ---- Shader Stage 推断 ----

	// 从文件路径和源码内容推断 shader stage
	// 优先级：扩展名 > 文件名关键字 > 代码内容特征
	ShaderStage inferShaderStage(const std::filesystem::path& shaderPath, std::string_view code);
}
