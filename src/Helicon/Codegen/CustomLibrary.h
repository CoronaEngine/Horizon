#pragma once

#include <ktm/ktm.h>
#include <Codegen/VariateProxy.h>
#include <Codegen/AST/Node.hpp>

namespace EmbeddedShader::CustomLibrary
{
	template<typename Type>
	VariateProxy<base_type_t<Type>> abs(Type&& a)
	{
		return {Ast::AST::callFunc("abs", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(a)>(a))}),Ast::AccessPermissions::ReadOnly)};
	}

	template<typename Type>
	VariateProxy<base_type_t<Type>> pow(Type&& x, proxy_wrapper<Type> auto&& y)
	{
		return {Ast::AST::callFunc("pow", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x)),proxy_wrap(std::forward<decltype(y)>(y))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type>
	VariateProxy<base_type_t<Type>> clamp(Type&& x, proxy_wrapper<Type> auto&& min,proxy_wrapper<Type> auto&& max)
	{
		return {Ast::AST::callFunc("clamp", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x)), proxy_wrap(std::forward<decltype(min)>(min)),proxy_wrap(std::forward<decltype(max)>(max))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename TexelType>
	VariateProxy<TexelType> texture(Texture2DProxy<TexelType>& tex, const VariateProxy<ktm::fvec2>& uv)
	{
		return tex.sample(uv);
	}

	template<typename Type> requires ktm::is_vector_v<base_type_t<Type>>
	VariateProxy<base_type_t<Type>> normalize(Type&& x)
	{
		return VariateProxy<base_type_t<Type>>{Ast::AST::callFunc("normalize", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type>
	VariateProxy<base_type_t<Type>> max(Type&& a, proxy_wrapper<Type> auto&& b)
	{
		return {Ast::AST::callFunc("max", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(a)>(a)), proxy_wrap(std::forward<decltype(b)>(b))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type>
	VariateProxy<base_type_t<Type>> min(Type&& a, proxy_wrapper<Type> auto&& b)
	{
		return {Ast::AST::callFunc("min", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(a)>(a)), proxy_wrap(std::forward<decltype(b)>(b))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type> requires ktm::is_vector_v<base_type_t<Type>>
	VariateProxy<ktm::vec_traits_base_t<base_type_t<Type>>> dot(Type&& a, proxy_wrapper<Type> auto&& b)
	{
		return {Ast::AST::callFunc("dot", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(a)>(a)), proxy_wrap(std::forward<decltype(b)>(b))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type>
	VariateProxy<base_type_t<Type>> lerp(Type&& x, proxy_wrapper<Type> auto&& y,proxy_wrapper<Type> auto&& s)
	{
		return {Ast::AST::callFunc("lerp", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x)), proxy_wrap(std::forward<decltype(y)>(y)),proxy_wrap(std::forward<decltype(s)>(s))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type> requires ktm::is_vector_v<base_type_t<Type>>
	VariateProxy<base_type_t<Type>> lerp(Type&& x, proxy_wrapper<Type> auto&& y,proxy_wrapper<ktm::vec_traits_base_t<base_type_t<Type>>> auto&& s)
	{
		return VariateProxy<base_type_t<Type>>{Ast::AST::callFunc("lerp", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x)), proxy_wrap(std::forward<decltype(y)>(y)),proxy_wrap(std::forward<decltype(s)>(s))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type>
	VariateProxy<base_type_t<Type>> mix(Type&& x, proxy_wrapper<Type> auto&& y,proxy_wrapper<Type> auto&& s)
	{
		return lerp(x,y,s);
	}

	template<typename Type> requires ktm::is_vector_v<base_type_t<Type>>
	VariateProxy<base_type_t<Type>> mix(Type&& x, proxy_wrapper<Type> auto&& y,proxy_wrapper<ktm::vec_traits_base_t<base_type_t<Type>>> auto&& s)
	{
		return lerp(x,y,s);
	}

	template<typename Type> requires ktm::is_matrix_v<base_type_t<Type>>
	VariateProxy<ktm::mat<ktm::mat_traits_col_v<base_type_t<Type>>,ktm::mat_traits_row_v<base_type_t<Type>>,ktm::mat_traits_base_t<base_type_t<Type>>>> transpose(Type&& x)
	{
		return {Ast::AST::callFunc("transpose", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x))}),Ast::AccessPermissions::ReadOnly)};
	}

	// template<typename Type> requires ktm::is_matrix_v<Type>
	// VariateProxy<Type> inverse(const VariateProxy<Type>& x)
	// {
	// 	return {Ast::AST::callFunc("inverse", Ast::AST::createType<base_type_t<Type>>(),{x.node})};
	// }

	template<typename Type> requires std::is_arithmetic_v<base_type_t<Type>>
	VariateProxy<base_type_t<Type>> mul(Type&& x, proxy_wrapper<Type> auto&& y)
	{
		return {Ast::AST::callFunc("mul", Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x)), proxy_wrap(std::forward<decltype(y)>(y))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type1, typename Type2> requires (std::is_arithmetic_v<base_type_t<Type1>> && (ktm::is_vector_v<base_type_t<Type2>> || ktm::is_matrix_v<base_type_t<Type2>>))
	VariateProxy<base_type_t<Type2>> mul(Type1&& x, Type2&& y)
	{
		return {Ast::AST::callFunc("mul", Ast::AST::createType<base_type_t<Type2>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x)), proxy_wrap(std::forward<decltype(y)>(y))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type1, typename Type2> requires (ktm::is_vector_v<base_type_t<Type1>> && std::is_arithmetic_v<base_type_t<Type2>>)
	VariateProxy<base_type_t<Type1>> mul(Type1&& x, Type2&& y)
	{
		return {Ast::AST::callFunc("mul", Ast::AST::createType<base_type_t<Type1>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x)), proxy_wrap(std::forward<decltype(y)>(y))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type>
	VariateProxy<ktm::vec_traits_base_t<base_type_t<Type>>> mul(Type&& a, proxy_wrapper<Type> auto&& b)
	{
		return {Ast::AST::callFunc("mul", Ast::AST::createType<ktm::vec_traits_base_t<base_type_t<Type>>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(a)>(a)), proxy_wrap(std::forward<decltype(b)>(b))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type1, typename Type2> requires (ktm::is_vector_v<base_type_t<Type1>> && ktm::is_matrix_v<base_type_t<Type2>> && std::same_as<ktm::vec_traits_base_t<base_type_t<Type1>>,ktm::mat_traits_base_t<base_type_t<Type2>>> && ktm::mat_traits_row_v<base_type_t<Type2>> == ktm::vec_traits_len<base_type_t<Type1>>)
	VariateProxy<base_type_t<Type1>> mul(Type1&& a, Type2&& b)
	{
		return {Ast::AST::callFunc("mul", Ast::AST::createType<base_type_t<Type1>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(a)>(a)), proxy_wrap(std::forward<decltype(b)>(b))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type1, typename Type2> requires (ktm::is_matrix_v<base_type_t<Type1>> && std::is_arithmetic_v<base_type_t<Type2>>)
	VariateProxy<base_type_t<Type1>> mul(Type1&& x, Type2&& y)
	{
		return {Ast::AST::callFunc("mul", Ast::AST::createType<base_type_t<Type1>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x)), proxy_wrap(std::forward<decltype(y)>(y))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type1,typename Type2> requires (ktm::is_matrix_v<base_type_t<Type1>> && ktm::is_vector_v<base_type_t<Type2>> && std::same_as<ktm::mat_traits_base_t<base_type_t<Type1>>,ktm::vec_traits_base_t<base_type_t<Type2>>> && ktm::mat_traits_col_v<base_type_t<Type1>> == ktm::vec_traits_len<base_type_t<Type2>>)
	VariateProxy<ktm::vec<ktm::mat_traits_row_v<base_type_t<Type1>>,ktm::mat_traits_base_t<base_type_t<Type1>>>> mul(Type1&& a,Type2&& b)//const VariateProxy<ktm::mat<Rows,Columns,Type>>& a, const VariateProxy<ktm::vec<Columns,Type>>& b)
	{
		return {Ast::AST::callFunc("mul", Ast::AST::createType<ktm::vec<ktm::mat_traits_row_v<base_type_t<Type1>>,ktm::mat_traits_base_t<base_type_t<Type1>>>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(a)>(a)), proxy_wrap(std::forward<decltype(b)>(b))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type1,typename Type2> requires (ktm::is_matrix_v<base_type_t<Type1>> && ktm::is_matrix_v<base_type_t<Type2>> && std::same_as<ktm::mat_traits_base_t<base_type_t<Type1>>,ktm::mat_traits_base_t<base_type_t<Type2>>> && ktm::mat_traits_col_v<base_type_t<Type1>> == ktm::mat_traits_row_v<base_type_t<Type2>>)
	VariateProxy<ktm::mat<ktm::mat_traits_row_v<base_type_t<Type1>>,ktm::mat_traits_col_v<base_type_t<Type2>>,ktm::mat_traits_base_t<base_type_t<Type2>>>> mul(Type1&& a,Type2&& b)
	{
		return {Ast::AST::callFunc("mul",Ast::AST::createType<ktm::mat<ktm::mat_traits_row_v<base_type_t<Type1>>,ktm::mat_traits_col_v<base_type_t<Type2>>,ktm::mat_traits_base_t<base_type_t<Type2>>>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(a)>(a)),proxy_wrap(std::forward<decltype(b)>(b))},Ast::AccessPermissions::ReadOnly))};
	}

	template<typename Type>
	VariateProxy<base_type_t<Type>> sqrt(Type&& x)
	{
		return {Ast::AST::callFunc("sqrt",Ast::AST::createType<base_type_t<Type>>(),Ast::Node::accessAll({proxy_wrap(std::forward<decltype(x)>(x))},Ast::AccessPermissions::ReadOnly))};
	}

}

namespace EmbeddedShader
{
	using namespace CustomLibrary;
}
