#include "ShaderTypeMapping.h"

namespace EmbeddedShader
{
	std::string typeNameToSlang(std::string_view n)
	{
		/* ---------- 向量 ---------- */
		if (n == "ivec2") return "int2";
		if (n == "ivec3") return "int3";
		if (n == "ivec4") return "int4";
		if (n == "uvec2") return "uint2";
		if (n == "uvec3") return "uint3";
		if (n == "uvec4") return "uint4";
		if (n == "vec2")  return "float2";
		if (n == "vec3")  return "float3";
		if (n == "vec4")  return "float4";
		if (n == "bvec2") return "bool2";
		if (n == "bvec3") return "bool3";
		if (n == "bvec4") return "bool4";
		if (n == "dvec2") return "double2";
		if (n == "dvec3") return "double3";
		if (n == "dvec4") return "double4";

		/* ---------- 矩阵 ---------- */
		if (n == "mat2")   return "float2x2";
		if (n == "mat3")   return "float3x3";
		if (n == "mat4")   return "float4x4";
		if (n == "mat2x2") return "float2x2";
		if (n == "mat2x3") return "float2x3";
		if (n == "mat2x4") return "float2x4";
		if (n == "mat3x2") return "float3x2";
		if (n == "mat3x3") return "float3x3";
		if (n == "mat3x4") return "float3x4";
		if (n == "mat4x2") return "float4x2";
		if (n == "mat4x3") return "float4x3";
		if (n == "mat4x4") return "float4x4";
		if (n == "dmat2")   return "double2x2";
		if (n == "dmat3")   return "double3x3";
		if (n == "dmat4")   return "double4x4";
		if (n == "dmat2x2") return "double2x2";
		if (n == "dmat2x3") return "double2x3";
		if (n == "dmat2x4") return "double2x4";
		if (n == "dmat3x2") return "double3x2";
		if (n == "dmat3x3") return "double3x3";
		if (n == "dmat3x4") return "double3x4";
		if (n == "dmat4x2") return "double4x2";
		if (n == "dmat4x3") return "double4x3";
		if (n == "dmat4x4") return "double4x4";

		/* ---------- 纹理（GLSL → Slang） ---------- */
		if (n == "sampler1D")         return "Texture1D";
		if (n == "sampler2D")         return "Texture2D";
		if (n == "sampler3D")         return "Texture3D";
		if (n == "samplerCube")       return "TextureCube";
		if (n == "sampler2DArray")    return "Texture2DArray";
		if (n == "samplerCubeArray")  return "TextureCubeArray";
		if (n == "sampler2DMS")       return "Texture2DMS";
		if (n == "sampler2DShadow")   return "Texture2DShadow";
		if (n == "samplerCubeShadow") return "TextureCubeShadow";
		// int texel sampler types
		if (n == "isampler1D")        return "Texture1D<int4>";
		if (n == "isampler2D")        return "Texture2D<int4>";
		if (n == "isampler3D")        return "Texture3D<int4>";
		if (n == "isamplerCube")      return "TextureCube<int4>";
		if (n == "isampler2DArray")   return "Texture2DArray<int4>";
		// uint texel sampler types
		if (n == "usampler1D")        return "Texture1D<uint4>";
		if (n == "usampler2D")        return "Texture2D<uint4>";
		if (n == "usampler3D")        return "Texture3D<uint4>";
		if (n == "usamplerCube")      return "TextureCube<uint4>";
		if (n == "usampler2DArray")   return "Texture2DArray<uint4>";

		/* ---------- 缓冲（GLSL 风格 → Slang） ---------- */
		if (n == "samplerBuffer")       return "Buffer";
		if (n == "imageBuffer")         return "RWBuffer";
		if (n == "sampler2DRect")       return "Texture2D";
		if (n == "image1D")             return "RWTexture1D";
		if (n == "image2D")             return "RWTexture2D";
		if (n == "image3D")             return "RWTexture3D";
		if (n == "imageCube")           return "RWTextureCube";
		if (n == "image2DArray")        return "RWTexture2DArray";
		if (n == "imageCubeArray")      return "RWTextureCubeArray";
		if (n == "uimage2D")            return "RWTexture2D<uint>";
		if (n == "uimage3D")            return "RWTexture3D<uint>";
		if (n == "uimageCube")          return "RWTextureCube<uint>";
		if (n == "uimage2DArray")       return "RWTexture2DArray<uint>";
		if (n == "iimage2D")            return "RWTexture2D<int>";
		if (n == "iimage3D")            return "RWTexture3D<int>";
		if (n == "iimageCube")          return "RWTextureCube<int>";
		if (n == "iimage2DArray")       return "RWTexture2DArray<int>";

		/* 未命中则原样返回 */
		return std::string(n);
	}

	std::string typeNameToCpp(std::string_view typeName)
	{
		// GLSL 向量类型
		if (typeName == "vec2") return "::ktm::fvec2";
		if (typeName == "vec3") return "::ktm::fvec3";
		if (typeName == "vec4") return "::ktm::fvec4";

		if (typeName == "dvec2") return "::ktm::dvec2";
		if (typeName == "dvec3") return "::ktm::dvec3";
		if (typeName == "dvec4") return "::ktm::dvec4";

		if (typeName == "bvec2") return "::ktm::bvec2";
		if (typeName == "bvec3") return "::ktm::bvec3";
		if (typeName == "bvec4") return "::ktm::bvec4";

		if (typeName == "uvec2") return "::ktm::uvec2";
		if (typeName == "uvec3") return "::ktm::uvec3";
		if (typeName == "uvec4") return "::ktm::uvec4";

		if (typeName == "ivec2") return "::ktm::svec2";
		if (typeName == "ivec3") return "::ktm::svec3";
		if (typeName == "ivec4") return "::ktm::svec4";

		// GLSL 矩阵类型
		if (typeName == "mat2") return "::ktm::fmat2x2";
		if (typeName == "mat3") return "::ktm::fmat3x3";
		if (typeName == "mat4") return "::ktm::fmat4x4";

		if (typeName == "mat2x2") return "::ktm::fmat2x2";
		if (typeName == "mat2x3") return "::ktm::fmat2x3";
		if (typeName == "mat2x4") return "::ktm::fmat2x4";
		if (typeName == "mat3x2") return "::ktm::fmat3x2";
		if (typeName == "mat3x3") return "::ktm::fmat3x3";
		if (typeName == "mat3x4") return "::ktm::fmat3x4";
		if (typeName == "mat4x2") return "::ktm::fmat4x2";
		if (typeName == "mat4x3") return "::ktm::fmat4x3";
		if (typeName == "mat4x4") return "::ktm::fmat4x4";

		if (typeName == "dmat2") return "::ktm::dmat2x2";
		if (typeName == "dmat3") return "::ktm::dmat3x3";
		if (typeName == "dmat4") return "::ktm::dmat4x4";

		if (typeName == "dmat2x2") return "::ktm::dmat2x2";
		if (typeName == "dmat2x3") return "::ktm::dmat2x3";
		if (typeName == "dmat2x4") return "::ktm::dmat2x4";
		if (typeName == "dmat3x2") return "::ktm::dmat3x2";
		if (typeName == "dmat3x3") return "::ktm::dmat3x3";
		if (typeName == "dmat3x4") return "::ktm::dmat3x4";
		if (typeName == "dmat4x2") return "::ktm::dmat4x2";
		if (typeName == "dmat4x3") return "::ktm::dmat4x3";
		if (typeName == "dmat4x4") return "::ktm::dmat4x4";

		// HLSL 向量类型
		if (typeName == "float2") return "::ktm::fvec2";
		if (typeName == "float3") return "::ktm::fvec3";
		if (typeName == "float4") return "::ktm::fvec4";

		if (typeName == "int2") return "::ktm::svec2";
		if (typeName == "int3") return "::ktm::svec3";
		if (typeName == "int4") return "::ktm::svec4";

		if (typeName == "uint2") return "::ktm::uvec2";
		if (typeName == "uint3") return "::ktm::uvec3";
		if (typeName == "uint4") return "::ktm::uvec4";

		if (typeName == "double2") return "::ktm::dvec2";
		if (typeName == "double3") return "::ktm::dvec3";
		if (typeName == "double4") return "::ktm::dvec4";

		if (typeName == "half2") return "::ktm::fvec2";
		if (typeName == "half3") return "::ktm::fvec3";
		if (typeName == "half4") return "::ktm::fvec4";

		// HLSL 矩阵类型
		if (typeName == "float2x2") return "::ktm::fmat2x2";
		if (typeName == "float2x3") return "::ktm::fmat2x3";
		if (typeName == "float2x4") return "::ktm::fmat2x4";
		if (typeName == "float3x2") return "::ktm::fmat3x2";
		if (typeName == "float3x3") return "::ktm::fmat3x3";
		if (typeName == "float3x4") return "::ktm::fmat3x4";
		if (typeName == "float4x2") return "::ktm::fmat4x2";
		if (typeName == "float4x3") return "::ktm::fmat4x3";
		if (typeName == "float4x4") return "::ktm::fmat4x4";

		if (typeName == "half2x2") return "::ktm::fmat2x2";
		if (typeName == "half2x3") return "::ktm::fmat2x3";
		if (typeName == "half2x4") return "::ktm::fmat2x4";
		if (typeName == "half3x2") return "::ktm::fmat3x2";
		if (typeName == "half3x3") return "::ktm::fmat3x3";
		if (typeName == "half3x4") return "::ktm::fmat3x4";
		if (typeName == "half4x2") return "::ktm::fmat4x2";
		if (typeName == "half4x3") return "::ktm::fmat4x3";
		if (typeName == "half4x4") return "::ktm::fmat4x4";

		if (typeName == "double2x2") return "::ktm::dmat2x2";
		if (typeName == "double2x3") return "::ktm::dmat2x3";
		if (typeName == "double2x4") return "::ktm::dmat2x4";
		if (typeName == "double3x2") return "::ktm::dmat3x2";
		if (typeName == "double3x3") return "::ktm::dmat3x3";
		if (typeName == "double3x4") return "::ktm::dmat3x4";
		if (typeName == "double4x2") return "::ktm::dmat4x2";
		if (typeName == "double4x3") return "::ktm::dmat4x3";
		if (typeName == "double4x4") return "::ktm::dmat4x4";

		// 特定类型名称
		if (typeName == "uint") return "unsigned int";
		if (typeName == "half") return "float";
		if (typeName == "dword") return "unsigned int";

		// Opaque sampler/image types -> Texture2DProxy (EDSL proxy for GLSL sampler/image)
		// float texel types
		if (typeName == "sampler1D")        return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "sampler2D")        return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "sampler3D")        return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "samplerCube")      return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "sampler2DArray")   return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "samplerCubeArray") return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "sampler2DMS")      return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "sampler2DShadow")  return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "samplerCubeShadow") return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "sampler2DRect")    return "@Texture2DProxy<::ktm::fvec4>";
		// int texel types
		if (typeName == "isampler1D")       return "@Texture2DProxy<::ktm::svec4>";
		if (typeName == "isampler2D")       return "@Texture2DProxy<::ktm::svec4>";
		if (typeName == "isampler3D")       return "@Texture2DProxy<::ktm::svec4>";
		if (typeName == "isamplerCube")     return "@Texture2DProxy<::ktm::svec4>";
		if (typeName == "isampler2DArray")  return "@Texture2DProxy<::ktm::svec4>";
		// uint texel types
		if (typeName == "usampler1D")       return "@Texture2DProxy<::ktm::uvec4>";
		if (typeName == "usampler2D")       return "@Texture2DProxy<::ktm::uvec4>";
		if (typeName == "usampler3D")       return "@Texture2DProxy<::ktm::uvec4>";
		if (typeName == "usamplerCube")     return "@Texture2DProxy<::ktm::uvec4>";
		if (typeName == "usampler2DArray")  return "@Texture2DProxy<::ktm::uvec4>";
		// image types (storage images)
		if (typeName == "image1D")          return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "image2D")          return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "image3D")          return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "imageCube")        return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "image2DArray")     return "@Texture2DProxy<::ktm::fvec4>";
		if (typeName == "iimage2D")         return "@Texture2DProxy<::ktm::svec4>";
		if (typeName == "iimage3D")         return "@Texture2DProxy<::ktm::svec4>";
		if (typeName == "iimageCube")       return "@Texture2DProxy<::ktm::svec4>";
		if (typeName == "iimage2DArray")    return "@Texture2DProxy<::ktm::svec4>";
		if (typeName == "uimage2D")         return "@Texture2DProxy<::ktm::uvec4>";
		if (typeName == "uimage3D")         return "@Texture2DProxy<::ktm::uvec4>";
		if (typeName == "uimageCube")       return "@Texture2DProxy<::ktm::uvec4>";
		if (typeName == "uimage2DArray")    return "@Texture2DProxy<::ktm::uvec4>";

		return std::string(typeName);
	}

	bool isOpaqueProxyType(const std::string& cppType)
	{
		return !cppType.empty() && cppType[0] == '@';
	}

	std::string emitCppParamType(const std::string& typeName)
	{
		auto cppType = typeNameToCpp(typeName);
		if (isOpaqueProxyType(cppType))
			return "::EmbeddedShader::" + cppType.substr(1);
		return "::EmbeddedShader::VariateProxy<" + cppType + ">";
	}
}
