#pragma once
#include <cstdint>
#include <iostream>
#include <ktm/type_mat.h>
#include <ktm/type_vec.h>

namespace EmbeddedShader::Ast
{
	enum class ShaderStage
	{
		Vertex,
		Fragment,
		Compute,
	};

	enum class AccessPermissions : uint8_t
	{
		None = 0,
		ReadOnly = 0b1,
		WriteOnly = 0b10,
		ReadAndWrite = ReadOnly | WriteOnly,
	};

	inline AccessPermissions operator|(AccessPermissions a, AccessPermissions b)
	{
		using UnderlyingType = std::underlying_type_t<AccessPermissions>;
		return static_cast<AccessPermissions>(static_cast<UnderlyingType>(a) |static_cast<UnderlyingType>(b));
	}

	inline AccessPermissions operator&(AccessPermissions a, AccessPermissions b)
	{
		using UnderlyingType = std::underlying_type_t<AccessPermissions>;
		return static_cast<AccessPermissions>(static_cast<UnderlyingType>(a) & static_cast<UnderlyingType>(b));
	}

	inline std::ostream& operator<<(std::ostream& os, AccessPermissions a)
	{
		switch (a)
		{
			case AccessPermissions::None:
				return os << "None (Not used)";
			case AccessPermissions::ReadOnly:
				return os << "ReadOnly";
				case AccessPermissions::WriteOnly:
				return os << "WriteOnly";
			case AccessPermissions::ReadAndWrite:
				return os << "ReadAndWrite";
				default:break;
		}

		return os << "Unknown AccessPermissions";
	}
}
