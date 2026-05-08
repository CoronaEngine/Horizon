#pragma once
#include <Codegen/VariateProxy.h>

namespace EmbeddedShader::TypeAlias
{
	using Int8_t = VariateProxy<int8_t>;
	using Int16_t = VariateProxy<int16_t>;
	using Int = VariateProxy<int32_t>;
	using Int64_t = VariateProxy<int64_t>;
	using Uint8_t = VariateProxy<uint8_t>;
	using Uint16_t = VariateProxy<uint16_t>;
	using Uint = VariateProxy<uint32_t>;
	using Uint64_t = VariateProxy<uint64_t>;
	using Float = VariateProxy<float>;
	using Double = VariateProxy<double>;
	using Bool = VariateProxy<bool>;

	template <size_t N,typename Type>
	using Vector = VariateProxy<ktm::vec<N, Type>>;
	template <typename Type>
	using Vector2 = VariateProxy<ktm::vec<2,Type>>;
	template <typename Type>
	using Vector3 = VariateProxy<ktm::vec<3,Type>>;
	template <typename Type>
	using Vector4 = VariateProxy<ktm::vec<4,Type>>;

	using Int8_t2 = Vector2<uint8_t>;
	using Int8_t3 = Vector3<uint8_t>;
	using Int8_t4 = Vector4<uint8_t>;
	using Int16_t2 = Vector2<uint16_t>;
	using Int16_t3 = Vector3<uint16_t>;
	using Int16_t4 = Vector4<uint16_t>;
	using Int2 = Vector2<uint32_t>;
	using Int3 = Vector3<uint32_t>;
	using Int4 = Vector4<uint32_t>;
	using Int64_t2 = Vector2<uint64_t>;
	using Int64_t3 = Vector3<uint64_t>;
	using Int64_t4 = Vector4<uint64_t>;
	using Uint8_t2 = Vector2<uint8_t>;
	using Uint8_t3 = Vector3<uint8_t>;
	using Uint8_t4 = Vector4<uint8_t>;
	using Uint16_t2 = Vector2<uint16_t>;
	using Uint16_t3 = Vector3<uint16_t>;
	using Uint16_t4 = Vector4<uint16_t>;
	using Uint2 = Vector2<uint32_t>;
	using Uint3 = Vector3<uint32_t>;
	using Uint4 = Vector4<uint32_t>;
	using Uint64_t2 = Vector2<uint64_t>;
	using Uint64_t3 = Vector3<uint64_t>;
	using Uint64_t4 = Vector4<uint64_t>;
	using Uint64_t2 = Vector2<uint64_t>;
	using Uint64_t3 = Vector3<uint64_t>;
	using Uint64_t4 = Vector4<uint64_t>;
	using Float2 = Vector2<float>;
	using Float3 = Vector3<float>;
	using Float4 = Vector4<float>;
	using Double2 = Vector2<double>;
	using Double3 = Vector3<double>;
	using Double4 = Vector4<double>;
	using Bool2 = Vector2<bool>;
	using Bool3 = Vector3<bool>;
	using Bool4 = Vector4<bool>;

	template<size_t Row,size_t Col, typename Type>
	using Matrix = VariateProxy<ktm::mat<Row,Col,Type>>;

	using Float2x2 = Matrix<2, 2, float>;
	using Float2x3 = Matrix<2, 3, float>;
	using Float2x4 = Matrix<2, 4, float>;
	using Float3x2 = Matrix<3, 2, float>;
	using Float3x3 = Matrix<3, 3, float>;
	using Float3x4 = Matrix<3, 4, float>;
	using Float4x2 = Matrix<4, 2, float>;
	using Float4x3 = Matrix<4, 3, float>;
	using Float4x4 = Matrix<4, 4, float>;

	using Double2x2 = Matrix<2, 2, double>;
	using Double2x3 = Matrix<2, 3, double>;
	using Double2x4 = Matrix<2, 4, double>;
	using Double3x2 = Matrix<3, 2, double>;
	using Double3x3 = Matrix<3, 3, double>;
	using Double3x4 = Matrix<3, 4, double>;
	using Double4x2 = Matrix<4, 2, double>;
	using Double4x3 = Matrix<4, 3, double>;
	using Double4x4 = Matrix<4, 4, double>;

	using Int8_t2x2 = Matrix<2, 2, int8_t>;
	using Int8_t2x3 = Matrix<2, 3, int8_t>;
	using Int8_t2x4 = Matrix<2, 4, int8_t>;
	using Int8_t3x2 = Matrix<3, 2, int8_t>;
	using Int8_t3x3 = Matrix<3, 3, int8_t>;
	using Int8_t3x4 = Matrix<3, 4, int8_t>;
	using Int8_t4x2 = Matrix<4, 2, int8_t>;
	using Int8_t4x3 = Matrix<4, 3, int8_t>;
	using Int8_t4x4 = Matrix<4, 4, int8_t>;

	using Int16_t2x2 = Matrix<2, 2, int16_t>;
	using Int16_t2x3 = Matrix<2, 3, int16_t>;
	using Int16_t2x4 = Matrix<2, 4, int16_t>;
	using Int16_t3x2 = Matrix<3, 2, int16_t>;
	using Int16_t3x3 = Matrix<3, 3, int16_t>;
	using Int16_t3x4 = Matrix<3, 4, int16_t>;
	using Int16_t4x2 = Matrix<4, 2, int16_t>;
	using Int16_t4x3 = Matrix<4, 3, int16_t>;
	using Int16_t4x4 = Matrix<4, 4, int16_t>;

	using Int2x2 = Matrix<2, 2, int32_t>;
	using Int2x3 = Matrix<2, 3, int32_t>;
	using Int2x4 = Matrix<2, 4, int32_t>;
	using Int3x2 = Matrix<3, 2, int32_t>;
	using Int3x3 = Matrix<3, 3, int32_t>;
	using Int3x4 = Matrix<3, 4, int32_t>;
	using Int4x2 = Matrix<4, 2, int32_t>;
	using Int4x3 = Matrix<4, 3, int32_t>;
	using Int4x4 = Matrix<4, 4, int32_t>;

	using Int64_t2x2 = Matrix<2, 2, int64_t>;
	using Int64_t2x3 = Matrix<2, 3, int64_t>;
	using Int64_t2x4 = Matrix<2, 4, int64_t>;
	using Int64_t3x2 = Matrix<3, 2, int64_t>;
	using Int64_t3x3 = Matrix<3, 3, int64_t>;
	using Int64_t3x4 = Matrix<3, 4, int64_t>;
	using Int64_t4x2 = Matrix<4, 2, int64_t>;
	using Int64_t4x3 = Matrix<4, 3, int64_t>;
	using Int64_t4x4 = Matrix<4, 4, int64_t>;

	using Bool2x2 = Matrix<2, 2, bool>;
	using Bool2x3 = Matrix<2, 3, bool>;
	using Bool2x4 = Matrix<2, 4, bool>;
	using Bool3x2 = Matrix<3, 2, bool>;
	using Bool3x3 = Matrix<3, 3, bool>;
	using Bool3x4 = Matrix<3, 4, bool>;
	using Bool4x2 = Matrix<4, 2, bool>;
	using Bool4x3 = Matrix<4, 3, bool>;
	using Bool4x4 = Matrix<4, 4, bool>;

	using Uint8_t2x2 = Matrix<2, 2, uint8_t>;
	using Uint8_t2x3 = Matrix<2, 3, uint8_t>;
	using Uint8_t2x4 = Matrix<2, 4, uint8_t>;
	using Uint8_t3x2 = Matrix<3, 2, uint8_t>;
	using Uint8_t3x3 = Matrix<3, 3, uint8_t>;
	using Uint8_t3x4 = Matrix<3, 4, uint8_t>;
	using Uint8_t4x2 = Matrix<4, 2, uint8_t>;
	using Uint8_t4x3 = Matrix<4, 3, uint8_t>;
	using Uint8_t4x4 = Matrix<4, 4, uint8_t>;

	using Uint16_t2x2 = Matrix<2, 2, uint16_t>;
	using Uint16_t2x3 = Matrix<2, 3, uint16_t>;
	using Uint16_t2x4 = Matrix<2, 4, uint16_t>;
	using Uint16_t3x2 = Matrix<3, 2, uint16_t>;
	using Uint16_t3x3 = Matrix<3, 3, uint16_t>;
	using Uint16_t3x4 = Matrix<3, 4, uint16_t>;
	using Uint16_t4x2 = Matrix<4, 2, uint16_t>;
	using Uint16_t4x3 = Matrix<4, 3, uint16_t>;
	using Uint16_t4x4 = Matrix<4, 4, uint16_t>;

	using Uint2x2 = Matrix<2, 2, uint32_t>;
	using Uint2x3 = Matrix<2, 3, uint32_t>;
	using Uint2x4 = Matrix<2, 4, uint32_t>;
	using Uint3x2 = Matrix<3, 2, uint32_t>;
	using Uint3x3 = Matrix<3, 3, uint32_t>;
	using Uint3x4 = Matrix<3, 4, uint32_t>;
	using Uint4x2 = Matrix<4, 2, uint32_t>;
	using Uint4x3 = Matrix<4, 3, uint32_t>;
	using Uint4x4 = Matrix<4, 4, uint32_t>;

	using Uint64_t2x2 = Matrix<2, 2, uint64_t>;
	using Uint64_t2x3 = Matrix<2, 3, uint64_t>;
	using Uint64_t2x4 = Matrix<2, 4, uint64_t>;
	using Uint64_t3x2 = Matrix<3, 2, uint64_t>;
	using Uint64_t3x3 = Matrix<3, 3, uint64_t>;
	using Uint64_t3x4 = Matrix<3, 4, uint64_t>;
	using Uint64_t4x2 = Matrix<4, 2, uint64_t>;
	using Uint64_t4x3 = Matrix<4, 3, uint64_t>;
	using Uint64_t4x4 = Matrix<4, 4, uint64_t>;

	template<typename TexelType>
	using Texture2D = Texture2DProxy<TexelType>;
	using Sampler = SamplerProxy;
	template<typename ElementType>
	using Array = ArrayProxy<ElementType>;
	template<typename AggregateStruct> requires std::is_aggregate_v<AggregateStruct>
	using Aggregate = VariateProxy<AggregateStruct>;
}

namespace EmbeddedShader
{
	using namespace TypeAlias;
}