#pragma once

#include <string>
#include <string_view>

namespace EmbeddedShader
{
	// GLSL/HLSL 类型名 → Slang 类型名
	std::string typeNameToSlang(std::string_view typeName);

	// GLSL/HLSL 类型名 → C++ (ktm::) 类型名
	// 不透明纹理/图像类型返回以 '@' 开头的代理类型（如 "@Texture2DProxy<::ktm::fvec4>"）
	std::string typeNameToCpp(std::string_view typeName);

	// 检查 typeNameToCpp 返回值是否为不透明代理类型（以 '@' 开头）
	bool isOpaqueProxyType(const std::string& cppType);

	// 返回用于 C++ 参数/成员声明的完整类型（自动处理 VariateProxy 包装和 opaque 类型）
	std::string emitCppParamType(const std::string& typeName);
}
