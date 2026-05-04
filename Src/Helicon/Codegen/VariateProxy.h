#pragma once

#include "boost/pfr.hpp"
#include <sstream>
#include <regex>
#include <type_traits>
#include <cassert>
#include <slang.h>
#include <unordered_map>
#include <string>
#include <tuple>
#include <ktm/ktm.h>
#include <ktm/type/vec.h>
#include <Codegen/AST/AST.hpp>
#include <Codegen/ParseHelper.h>
#include <Codegen/MathProxy.h>

// Forward declarations for Level 2 resource binding (defined in CabbageHardware.h)
struct HardwareImage;
struct HardwareBuffer;
struct HardwareImageCreateInfo;

namespace EmbeddedShader
{
	template<typename T>
	concept is_mathematical = requires(T a)
	{
		a = a + a;
		a = a - a;
	};

	template<typename T>
	std::shared_ptr<Ast::Value> proxy_wrap(T value)
	{
		return Ast::AST::createValue(value);
	}

	template<typename T>
	std::shared_ptr<Ast::Value> proxy_wrap(const VariateProxy<T>& proxy);

	template<typename T>
	std::shared_ptr<Ast::Value> proxy_wrap(const Texture2DProxy<T>& proxy);

	template<typename T>
	struct base_type
	{
		using type = T;
	};

	template<typename T>
	struct base_type<VariateProxy<T>>
	{
		using type = T;
	};

	template<typename T>
	using base_type_t = typename base_type<std::remove_cvref_t<T>>::type;

	template<typename T1,typename T2>
	concept proxy_wrapper = std::same_as<base_type_t<T1>,base_type_t<T2>>;

	template<typename Type>
	struct VariateProxy
	{
	private:
		template<typename T>
		struct VecArgProcessor
		{
			static T valueOrNode(T value)
			{
				return value;
			}
		};

		template<typename T>
		struct VecArgProcessor<VariateProxy<T>>
		{
			static std::shared_ptr<Ast::Value> valueOrNode(const VariateProxy<T>& proxy)
			{
				proxy.node->access(Ast::AccessPermissions::ReadOnly);
				return proxy.node;
			}
		};

		auto valueOrNode(auto&& arg)
		{
			return VecArgProcessor<std::remove_cvref_t<decltype(arg)>>::valueOrNode(std::forward<decltype(arg)>(arg));
		}
	public:
		template<typename T>
		friend struct ArrayProxy;
		template<typename T>
		friend struct Texture2DProxy;
	    template<typename OtherType>
	    friend struct VariateProxy;
		friend struct GPU_IF_BRANCH;
		friend struct GPU_ELSEIF_BRANCH;
		friend struct GPU_ELSE_BRANCH;

	    friend VariateProxy<ktm::fvec4> position();
		friend class RasterizedPipelineObject;
		friend class Generator::SlangGenerator;
		using value_type = Type;

		std::string getAstName() const
		{
			if (auto v = std::dynamic_pointer_cast<Ast::Variate>(node)) return v->name;
			return "";
		}
		VariateProxy()
		{
            value = std::make_unique<Type>();

            if (ParseHelper::notInitNode())
		        return;

			if (auto parent = ParseHelper::getVecParent())
			{
				auto name = ParseHelper::getVecComponentName();
				node = Ast::AST::access(parent,name,Ast::AST::createType<Type>());
				return;
			}

            if (auto parent = ParseHelper::getAggregateParent())
		    {
		        auto index = ParseHelper::getAggregateMemberIndex();
		        auto aggregateType = reinterpret_cast<Ast::AggregateType*>(parent->type.get());
		        auto member = aggregateType->members[index];
		        node = Ast::AST::access(parent,member->name, member->type);
		        return;
		    }

			//Uniform,Input,Local Variate
			if (ParseHelper::isInInputParameter())
			{
				node = Ast::AST::defineInputVariate<Type>(ParseHelper::getCurrentInputIndex());
				return;
			}

			if (ParseHelper::isInShaderCodeLambda())
			{
				node = Ast::AST::defineLocalVariate<Type>({});
				return;
			}
			node = Ast::AST::defineUniformVariate<Type>();
		}

	    VariateProxy() requires std::is_aggregate_v<Type>
		{
		    if (ParseHelper::notInitNode())
		    {
		        value = std::make_unique<Type>();
		        return;
		    }

            //Uniform,Input,Local Variate
		    ParseHelper::beginNotInitNode();
		    if (auto parent = ParseHelper::getAggregateParent())
		    {
		        auto index = ParseHelper::getAggregateMemberIndex();
		        auto aggregateType = reinterpret_cast<Ast::AggregateType*>(parent->type.get());
		        auto member = aggregateType->members[index];
		        node = Ast::AST::access(parent,member->name, member->type);
		    }
		    else if (ParseHelper::isInInputParameter())
		    {
		        // Vertex stage: expand aggregate into individual InputVariates per member,
		        // reconstruct via local variable + assignments (mirrors handleFragmentOutput in reverse).
		        // Fragment/other stage: keep as single aggregate InputVariate (inter-stage IO supports structs).
		        if (Ast::AST::getEmbeddedShaderStructure().stage == Ast::ShaderStage::Vertex)
		        {
		            auto aggregateType = Ast::AST::createType<Type>();
		            node = Ast::AST::defineLocalVariate(
		                std::static_pointer_cast<Ast::Type>(aggregateType), nullptr);
		            for (size_t loc = 0; loc < aggregateType->members.size(); ++loc)
		            {
		                auto& member = aggregateType->members[loc];
		                auto inputVar = Ast::AST::defineInputVariate(member->type, loc);
		                auto memberAccess = Ast::AST::access(node, member->name, member->type);
		                Ast::AST::assign(memberAccess, inputVar);
		            }
		        }
		        else
		        {
		            node = Ast::AST::defineInputVariate<Type>(ParseHelper::getCurrentInputIndex());
		        }
		    }
		    else if (ParseHelper::isInShaderCodeLambda())
		    {
		        node = Ast::AST::defineLocalVariate<Type>({});
		    }
		    else
		    {
		        node = Ast::AST::defineUniformVariate<Type>();
		    }
		    ParseHelper::endNotInitNode();

            ParseHelper::beginAggregateParent(node);
		    value = std::make_unique<Type>();
		    ParseHelper::endAggregateParent();
		}

		template<typename Arg0,typename Arg1,typename... Args>
		explicit VariateProxy(Arg0&& arg0,Arg1&& arg1,Args&&... args) requires ktm::is_vector_v<Type>
		{
			value = std::make_unique<Type>();
			if (ParseHelper::notInitNode())
				return;
			node = Ast::AST::createVecValue<Type>(valueOrNode(std::forward<Arg0>(arg0)),valueOrNode(std::forward<Arg1>(arg1)),valueOrNode(std::forward<Args>(args))...);
		}

		VariateProxy(const Type& value) requires (!std::is_aggregate_v<Type>)
		{
		    if (ParseHelper::notInitNode())
		    {
		        this->value = std::make_unique<Type>(value);
		        return;
		    }

			//Local Variate
			if (ParseHelper::isInShaderCodeLambda())
			{
				node = Ast::AST::defineLocalVariate(value);
				return;
			}

			node = Ast::AST::defineUniformVariate<Type>();
		}

		VariateProxy(const VariateProxy& value)
		{
			//Local Variate
		    if (ParseHelper::isInShaderCodeLambda())
		        node = Ast::AST::defineLocalVariate(value.node->type, value.node);
		}

		VariateProxy(VariateProxy&& value) = default;

		~VariateProxy()
		{
			if (isNeedUniversalStatementCheck && node && node.use_count() == 1)
				Ast::AST::addLocalUniversalStatement(node);
		}

		Type* operator->() requires (std::is_aggregate_v<Type> && !ktm::is_vector_v<Type>)
		{
			return value.get();
		}

		auto operator->() requires (ktm::is_vector_v<Type>)
		{
			ParseHelper::beginVecComponentInit(node);
			auto vecComponents = std::make_unique<typename VecProxyMap<Type>::proxy_type>();
			vecComponents->parent = node;
			ParseHelper::endVecComponentInit();
			return std::move(vecComponents);
		}

		VariateProxy& operator=(const VariateProxy& rhs)
		{
			if (this == &rhs)
				return *this;
			if (!std::dynamic_pointer_cast<Ast::Variate>(node))
			{
				node = Ast::AST::defineLocalVariate(node->type, rhs.node);
				return *this;
			}
			Ast::AST::assign(node,rhs.node);
		    return *this;
		}

		VariateProxy& operator=(const Type& rhs)
		{
			Ast::AST::assign(node,Ast::AST::createValue(rhs));
			return *this;
		}

		VariateProxy& operator=(Type&& rhs)
		{
			Ast::AST::assign(node,Ast::AST::createValue(std::forward<Type>(rhs)));
			return *this;
		}

		const VariateProxy operator++()
		{
			VariateProxy proxy(Ast::AST::unaryOperator(node,"++",true,Ast::AccessPermissions::ReadAndWrite));
			proxy.isNeedUniversalStatementCheck = true;
			return proxy;
		}

		const VariateProxy operator--()
		{
			VariateProxy proxy(Ast::AST::unaryOperator(node,"--",true,Ast::AccessPermissions::ReadAndWrite));
			proxy.isNeedUniversalStatementCheck = true;
			return proxy;
		}

		const VariateProxy operator++(int)
		{
			VariateProxy proxy(Ast::AST::unaryOperator(node,"++",false,Ast::AccessPermissions::ReadAndWrite));
			proxy.isNeedUniversalStatementCheck = true;
			return proxy;
		}

		const VariateProxy operator--(int)
		{
			VariateProxy proxy(Ast::AST::unaryOperator(node,"--",false,Ast::AccessPermissions::ReadAndWrite));
			proxy.isNeedUniversalStatementCheck = true;
			return proxy;
		}

		VariateProxy operator+(const VariateProxy& rhs) const
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"+"));
		}

		VariateProxy operator-(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"-"));
		}

		VariateProxy operator*(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"*"));
		}

		VariateProxy operator/(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"/"));
		}

		VariateProxy operator%(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"%"));
		}

		VariateProxy operator!()
		{
			return VariateProxy(Ast::AST::unaryOperator(node,"!"));
		}

		VariateProxy operator||(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"||"));
		}

		VariateProxy operator&&(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"&&"));
		}

		VariateProxy operator~()
		{
			return VariateProxy(Ast::AST::unaryOperator(node,"~"));
		}

		VariateProxy operator&(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"&"));
		}

		VariateProxy operator|(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"|"));
		}

		VariateProxy operator^(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"^"));
		}

		VariateProxy operator<<(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,"<<"));
		}

		VariateProxy operator>>(const VariateProxy& rhs)
		{
			return VariateProxy(Ast::AST::binaryOperator(node,rhs.node,">>"));
		}

		VariateProxy& operator+=(const VariateProxy& rhs)
		{
			return *this = *this + rhs;
		}

		VariateProxy& operator-=(const VariateProxy& rhs)
		{
			return *this = *this - rhs;
		}

		VariateProxy& operator*=(const VariateProxy& rhs)
		{
			return *this = *this * rhs;
		}

		VariateProxy& operator/=(const VariateProxy& rhs)
		{
			return *this = *this / rhs;
		}

		VariateProxy& operator%=(const VariateProxy& rhs)
		{
			return *this = *this % rhs;
		}

		VariateProxy& operator&=(const VariateProxy& rhs)
		{
			return *this = *this & rhs;
		}

		VariateProxy& operator|=(const VariateProxy& rhs)
		{
			return *this = *this | rhs;
		}

		VariateProxy& operator^=(const VariateProxy& rhs)
		{
			return *this = *this ^ rhs;
		}

		VariateProxy& operator>>=(const VariateProxy& rhs)
		{
			return *this = *this >> rhs;
		}

		VariateProxy& operator<<=(const VariateProxy& rhs)
		{
			return *this = *this << rhs;
		}
		
		VariateProxy<bool> operator>(const VariateProxy& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,rhs.node,">",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator>(const Type& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,Ast::AST::createValue(rhs),">",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator>=(const VariateProxy& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,rhs.node,">=",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator>=(const Type& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,Ast::AST::createValue(rhs),">=",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator<(const VariateProxy& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,rhs.node,"<",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator<(const Type& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,Ast::AST::createValue(rhs),"<",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator<=(const VariateProxy& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,rhs.node,"<=",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator<=(const Type& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,Ast::AST::createValue(rhs),"<=",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator!=(const VariateProxy& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,rhs.node,"!=",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator!=(const Type& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,Ast::AST::createValue(rhs),"!=",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator==(const VariateProxy& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,rhs.node,"==",Ast::AST::createType<bool>()));
		}

		VariateProxy<bool> operator==(const Type& rhs)
		{
			return VariateProxy<bool>(Ast::AST::binaryOperator(node,Ast::AST::createValue(rhs),"==",Ast::AST::createType<bool>()));
		}

		VariateProxy operator+() { return VariateProxy(Ast::AST::unaryOperator(node,"+")); }
		VariateProxy operator-() { return VariateProxy(Ast::AST::unaryOperator(node,"-")); }
		//VariateProxy& operator!() { return *(new VariateProxy()); }
		//VariateProxy& operator~() { return *(new VariateProxy()); }

		//VariateProxy& operator+(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator-(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator*(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator/(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator%(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator||(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator&&(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator&(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator|(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator^(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator<<(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator>>(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator+=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator-=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator*=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator/=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator%=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator&=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator|=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator^=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator>>=(const VariateProxy& rhs) { return *(new VariateProxy()); }
		//VariateProxy& operator<<=(const VariateProxy& rhs) { return *(new VariateProxy()); }

		//VariateProxy<bool>& operator>(const VariateProxy& rhs) { return *(new VariateProxy<bool>(true)); }
		//VariateProxy<bool>& operator>=(const VariateProxy& rhs) { return *(new VariateProxy<bool>(true)); }
		//VariateProxy<bool>& operator<(const VariateProxy& rhs) { return *(new VariateProxy<bool>(true)); }
		//VariateProxy<bool>& operator<=(const VariateProxy& rhs) { return *(new VariateProxy<bool>(true)); }
		//VariateProxy<bool>& operator!=(const VariateProxy& rhs) { return *(new VariateProxy<bool>(true)); }
		//VariateProxy<bool>& operator==(const VariateProxy& rhs) { return *(new VariateProxy<bool>(true)); }

		VariateProxy(std::shared_ptr<Ast::Value> node) : node(std::move(node))//,value(std::make_unique<Type>())
	    {
	    }

	    std::unique_ptr<Type> value{};
		std::shared_ptr<Ast::Value> node;
		bool isNeedUniversalStatementCheck = false;
	};

	template<typename Type>
	VariateProxy<Type> operator+(Type a, const VariateProxy<Type>& b)
	{
		return VariateProxy<Type>(a) + b;
	}

	template<typename Type>
	VariateProxy<Type> operator-(Type a, const VariateProxy<Type>& b)
	{
		return VariateProxy<Type>(a) - b;
	}

	template<typename Type>
	VariateProxy<Type> operator*(Type a, const VariateProxy<Type>& b)
	{
		return VariateProxy<Type>(a) * b;
	}

	template<size_t N,typename Type> requires std::is_arithmetic_v<Type>
	VariateProxy<ktm::vec<N,Type>> operator+(const VariateProxy<Type>& a,const VariateProxy<ktm::vec<N,Type>>& b)
	{
		return VariateProxy<ktm::vec<N,Type>>(Ast::AST::binaryOperator(a.node,b.node,"+"));
	}

	template<size_t N,typename Type> requires std::is_arithmetic_v<Type>
	VariateProxy<ktm::vec<N,Type>> operator+(const VariateProxy<ktm::vec<N,Type>>& a,const VariateProxy<Type>& b)
	{
		return b + a;
	}

	template<size_t N,typename Type> requires std::is_arithmetic_v<Type>
	VariateProxy<ktm::vec<N,Type>> operator-(const VariateProxy<Type>& a,const VariateProxy<ktm::vec<N,Type>>& b)
	{
		return VariateProxy<ktm::vec<N,Type>>(Ast::AST::binaryOperator(a.node,b.node,"-"));
	}

	template<size_t N,typename Type> requires std::is_arithmetic_v<Type>
	VariateProxy<ktm::vec<N,Type>> operator-(const VariateProxy<ktm::vec<N,Type>>& a,const VariateProxy<Type>& b)
	{
		return b - a;
	}

	template<size_t N,typename Type> requires std::is_arithmetic_v<Type>
	VariateProxy<ktm::vec<N,Type>> operator*(const VariateProxy<Type>& a,const VariateProxy<ktm::vec<N,Type>>& b)
	{
		return VariateProxy<ktm::vec<N,Type>>(Ast::AST::binaryOperator(a.node,b.node,"*"));
	}

	template<size_t N,typename Type> requires std::is_arithmetic_v<Type>
	VariateProxy<ktm::vec<N,Type>> operator*(const VariateProxy<ktm::vec<N,Type>>& a,const VariateProxy<Type>& b)
	{
		return VariateProxy<ktm::vec<N,Type>>(Ast::AST::binaryOperator(a.node,b.node,"*"));
	}

	template<size_t N,typename Type> requires std::is_arithmetic_v<Type>
	VariateProxy<ktm::vec<N,Type>> operator*=(const VariateProxy<ktm::vec<N,Type>>& a,const VariateProxy<Type>& b)
	{
		return VariateProxy<ktm::vec<N,Type>>(Ast::AST::binaryOperator(a.node,b.node,"*="));
	}

	template<size_t N,typename Type> requires std::is_arithmetic_v<Type>
	VariateProxy<ktm::vec<N,Type>> operator/(const VariateProxy<ktm::vec<N,Type>>& a,const VariateProxy<Type>& b)
	{
		return VariateProxy<ktm::vec<N,Type>>(Ast::AST::binaryOperator(a.node,b.node,"/"));
	}

	template<typename Type>
	struct Texture2DProxyTraits
	{
		static constexpr bool value = false;
	};

	template<typename Type>
	struct Texture2DProxyTraits<Texture2DProxy<Type>>
	{
		static constexpr bool value = true;
	};

	template<typename Type>
	struct ArrayProxy
	{
		using value_type = Type;
		ArrayProxy()
		{
			if (ParseHelper::notInitNode())
				return;

			if (auto parent = ParseHelper::getAggregateParent())
			{
				auto index = ParseHelper::getAggregateMemberIndex();
				auto aggregateType = reinterpret_cast<Ast::AggregateType*>(parent->type.get());
				auto member = aggregateType->members[index];
				node = Ast::AST::access(parent,member->name, member->type);
				return;
			}

			if constexpr (Texture2DProxyTraits<Type>::value)
			{
				auto textureType = std::make_shared<Ast::Texture2DType>();
				textureType->texelType = Ast::AST::createType<Type::value_type>();
				node = Ast::AST::defineUniversalArray(std::move(textureType));
			}
			else node = Ast::AST::defineUniversalArray<Type>();
		}

		template<std::integral IndexType>
		VariateProxy<Type> operator[](IndexType index)
		{
			if constexpr (std::is_aggregate_v<Type>)
			{
				VariateProxy<Type> proxy{Ast::AST::at(node, index)};
				ParseHelper::beginAggregateParent(proxy.node);
				proxy->value = std::make_unique<Type>();
				ParseHelper::endAggregateParent(proxy.node);
				return proxy;
			}
			else return {Ast::AST::at(node, index)};
		}

		template<std::integral IndexType>
		VariateProxy<Type> operator[](const VariateProxy<IndexType>& index)
		{
			if constexpr (std::is_aggregate_v<Type>)
			{
				VariateProxy<Type> proxy{Ast::AST::at(node, index.node)};
				ParseHelper::beginAggregateParent(proxy.node);
				proxy->value = std::make_unique<Type>();
				ParseHelper::endAggregateParent(proxy.node);
				return proxy;
			}
			return {Ast::AST::at(node, index.node)};
		}

		template<std::integral IndexType>
		Type operator[](IndexType index) requires Texture2DProxyTraits<Type>::value
		{
			return {Ast::AST::at(node, index)};
		}

		template<std::integral IndexType>
		Type operator[](const VariateProxy<IndexType>& index) requires Texture2DProxyTraits<Type>::value
		{
			return {Ast::AST::at(node, index.node)};
		}

		ArrayProxy(std::shared_ptr<Ast::Value> node) : node(std::move(node)) {}

		std::string getAstName() const
		{
			if (auto v = std::dynamic_pointer_cast<Ast::Variate>(node)) return v->name;
			return "";
		}
	private:
		std::shared_ptr<Ast::Value> node;
	};

	struct SamplerProxy
	{
		SamplerProxy()
		{
			if (auto parent = ParseHelper::getAggregateParent())
			{
				auto index = ParseHelper::getAggregateMemberIndex();
				auto aggregateType = reinterpret_cast<Ast::AggregateType*>(parent->type.get());
				auto member = aggregateType->members[index];
				node = Ast::AST::access(parent,member->name, member->type);
				return;
			}
		}

		void init(std::string name)
		{
			if (node)
				return;
			auto type = std::make_shared<Ast::SamplerType>();
			type->name = std::move(name);
			node = Ast::AST::defineUniformVariate(std::move(type));
		}
		std::string getAstName() const
		{
			if (auto v = std::dynamic_pointer_cast<Ast::Variate>(node)) return v->name;
			return "";
		}
		std::shared_ptr<Ast::Value> node;
	};

	template<typename Type>
	struct Texture2DProxy
	{
		using value_type = Type;
		Texture2DProxy()
		{
			if (ParseHelper::notInitNode())
				return;

			if (auto parent = ParseHelper::getAggregateParent())
			{
				auto index = ParseHelper::getAggregateMemberIndex();
				auto aggregateType = reinterpret_cast<Ast::AggregateType*>(parent->type.get());
				auto member = aggregateType->members[index];
				node = Ast::AST::access(parent,member->name, member->type);
				return;
			}

			node = Ast::AST::defineUniversalTexture2D<Type>();
			// Set back-pointer so auto-bind can read current resource at dispatch time
			if (auto tex = std::dynamic_pointer_cast<Ast::UniversalTexture2D>(node)) {
				tex->boundResourceRef = &boundResource_;
			}
		}

		// Owning constructor: creates proxy + GPU resource in one step.
		// Definition is in CabbageHardware.h (after HardwareImage is complete).
		void createResource(const ::HardwareImageCreateInfo& createInfo);

		// Bind existing HardwareImage at construction: Texture2D<fvec4> img = existingImage;
		Texture2DProxy(::HardwareImage& img) : Texture2DProxy()
		{
			boundResource_ = &img;
		}

		// Own a new HardwareImage at construction: Texture2D<fvec4> img = HardwareImage(createInfo);
		Texture2DProxy(::HardwareImage&& img) : Texture2DProxy()
		{
			ownedResource_ = std::make_unique<::HardwareImage>(std::move(img));
			boundResource_ = ownedResource_.get();
		}

		// Texture2DProxy(SamplerProxy&& sampler)
		// {
		// 	isHybrid = true;
		// 	if (ParseHelper::notInitNode())
		// 		return;
		//
		// 	if (auto parent = ParseHelper::getAggregateParent())
		// 	{
		// 		auto index = ParseHelper::getAggregateMemberIndex();
		// 		auto aggregateType = reinterpret_cast<Ast::AggregateType*>(parent->type.get());
		// 		auto member = aggregateType->members[index];
		// 		std::reinterpret_pointer_cast<Ast::Texture2DType>(member->type)->name = "Sampler2D";
		// 		node = Ast::AST::access(parent,member->name, member->type);
		// 		return;
		// 	}
		//
		// 	node = Ast::AST::defineUniversalTexture2D<Type>();
		// 	std::reinterpret_pointer_cast<Ast::Texture2DType>(node->type)->name = "Sampler2D";
		// }

		template<std::integral IndexType>
		VariateProxy<Type> operator[](ktm::vec<2,IndexType> index)
		{
			if constexpr (std::is_aggregate_v<Type>)
			{
				VariateProxy<Type> proxy{Ast::AST::at(node, index)};
				ParseHelper::beginAggregateParent(proxy.node);
				proxy->value = std::make_unique<Type>();
				ParseHelper::endAggregateParent(proxy.node);
				return proxy;
			}
			else return {Ast::AST::at(node, index)};
		}

		template<std::integral IndexType>
		VariateProxy<Type> operator[](VariateProxy<ktm::vec<2,IndexType>> index)
		{
			if constexpr (std::is_aggregate_v<Type>)
			{
				VariateProxy<Type> proxy{Ast::AST::at(node, index.node)};
				ParseHelper::beginAggregateParent(proxy.node);
				proxy->value = std::make_unique<Type>();
				ParseHelper::endAggregateParent(proxy.node);
				return proxy;
			}
			return {Ast::AST::at(node, index.node)};
		}

		VariateProxy<Type> sample(SamplerProxy& sampler,const VariateProxy<ktm::fvec2>& location)
		{
			//Cannot sample texture with sampler in hybrid mode, use sample(location) instead.
			assert(!isHybrid);
			sampler.init("SamplerState");
			sampler.node->access(Ast::AccessPermissions::ReadOnly);
			node->access(Ast::AccessPermissions::ReadOnly);
			auto func = Ast::AST::callFunc("Sample",Ast::AST::createType<Type>(),{sampler.node,location.node});
			return {Ast::AST::access(node,func->parse(), func->type)};
		}

		VariateProxy<Type> sample(const VariateProxy<ktm::fvec2>& location)
		{
			node->access(Ast::AccessPermissions::ReadOnly);
			location.node->access(Ast::AccessPermissions::ReadOnly);
			if (auto textureType = std::dynamic_pointer_cast<Ast::Texture2DType>(node->type))
			{
				textureType->name = "Sampler2D";
			}
			auto func = Ast::AST::callFunc("Sample",Ast::AST::createType<Type>(),{location.node});
			return {Ast::AST::access(node,func->parse(), func->type)};
		}

		Texture2DProxy(std::shared_ptr<Ast::Value> node) : node(std::move(node)) {}

		std::string getAstName() const
		{
			if (auto v = std::dynamic_pointer_cast<Ast::Variate>(node)) return v->name;
			return "";
		}

		// --- Level 2/3: Resource binding ---
		Texture2DProxy& operator=(::HardwareImage& img) { boundResource_ = &img; return *this; }
		::HardwareImage* resource() const { return static_cast<::HardwareImage*>(boundResource_); }

		// --- Render target output via operator<< ---
		// Usage in FS lambda: outputImage << Float4(r, g, b, a);
		// Emits DefineOutputVariate + assign at incremental SV_TARGET location.
		void operator<<(const VariateProxy<Type>& value)
		{
			auto tex = std::dynamic_pointer_cast<Ast::UniversalTexture2D>(node);
			size_t location = Ast::Parser::getNextRenderTargetLocation();
			auto outputVar = Ast::AST::defineOutputVariate<Type>(location);
			Ast::AST::assign(outputVar, value.node);
			if (tex) tex->renderTargetLocation = static_cast<int32_t>(location);
		}

		// Access the owned HardwareImage (only valid if constructed with HardwareImageCreateInfo)
		::HardwareImage& image() const { return *static_cast<::HardwareImage*>(ownedResource_.get()); }

		std::shared_ptr<Ast::Value> node;
		bool isHybrid = false;
		void* boundResource_ = nullptr;
		std::unique_ptr<::HardwareImage> ownedResource_;
	};

	template<typename T>
	std::shared_ptr<Ast::Value> proxy_wrap(const VariateProxy<T>& proxy)
	{
		return proxy.node;
	}

	template<typename T>
	std::shared_ptr<Ast::Value> proxy_wrap(const Texture2DProxy<T>& proxy)
	{
		return proxy.node;
	}

	template<typename T>
	class FunctionProxy
	{
	public:
		FunctionProxy() = delete;
	};

	template<typename Ret,typename... Args>
	class FunctionProxy<Ret(Args...)>
	{
	public:
		FunctionProxy(std::string funcName, std::string returnType, std::vector<std::pair<std::string,std::string>> args,std::vector<uint32_t>* sourceSpv)
		: node(Ast::AST::functionDeclaration(std::move(funcName),std::move(returnType),std::move(args))),sourceSpv(std::move(sourceSpv)) {}

		Ret operator()(Args... args) requires (ParseHelper::IsVariateProxy<Ret>::value ||
				ParseHelper::IsArrayProxy<Ret>::value ||
				ParseHelper::IsTexture2DProxy<Ret>::value ||
				(!std::same_as<void,Ret>))
		{
			if (!isBuildDeclaration)
			{
			    if (sourceSpv) Ast::AST::getEmbeddedShaderStructure().spvSource.insert(sourceSpv);
				Ast::AST::addGlobalStatement(node);
				isBuildDeclaration = true;
			}
			Ret ret{Ast::AST::callFunc(node->funcName,Ast::AST::createType<typename Ret::value_type>(),{proxy_wrap(args)...})};
			return ret;
		}

		void operator()(Args... args) requires std::same_as<void,Ret>
		{
			if (!isBuildDeclaration)
			{
			    if (sourceSpv) Ast::AST::getEmbeddedShaderStructure().spvSource.insert(sourceSpv);
				Ast::AST::addGlobalStatement(node);
				isBuildDeclaration = true;
			}
			Ast::AST::addLocalUniversalStatement(Ast::AST::callFunc(node->funcName,nullptr,{proxy_wrap(args)...}));
		}
	private:
		std::shared_ptr<Ast::FunctionDeclaration> node;
		bool isBuildDeclaration = false;
	    std::vector<uint32_t>* sourceSpv = nullptr;
	};

	//暂时先这样特化VariateProxy，后续优化

	template<typename Ret,typename... Args>
	class FunctionProxy<VariateProxy<Ret(Args...)>>
	{
	public:
	    FunctionProxy(std::string funcName, std::string returnType, std::vector<std::pair<std::string,std::string>> args,std::vector<uint32_t>* sourceSpv)
        : node(Ast::AST::functionDeclaration(std::move(funcName),std::move(returnType),std::move(args))),sourceSpv(std::move(sourceSpv)) {}

		VariateProxy<Ret> operator()(Args... args) requires (ParseHelper::IsVariateProxy<Ret>::value ||
				ParseHelper::IsArrayProxy<Ret>::value ||
				ParseHelper::IsTexture2DProxy<Ret>::value ||
				(!std::same_as<void,Ret>))
		{
	        if (sourceSpv) Ast::AST::getEmbeddedShaderStructure().spvSource.insert(sourceSpv);
			if (!isBuildDeclaration)
			{
				Ast::AST::addGlobalStatement(node);//需要处理当返回值被忽略时，函数调用未被生成的问题
				isBuildDeclaration = true;
			}
			VariateProxy<Ret> ret{Ast::AST::callFunc(node->funcName,Ast::AST::createType<Ret>(),{proxy_wrap(args)...})};
			return ret;
		}
	private:
		std::shared_ptr<Ast::FunctionDeclaration> node;
		bool isBuildDeclaration = false;
	    std::vector<uint32_t>* sourceSpv = nullptr;
	};

	struct BindingKey
	{
		// Direct-access metadata (populated by generated .hpp at compile time)
		uint64_t byteOffset = ~0ULL;
		uint32_t typeSize = 0;
		int32_t  bindType = -1;   // mirrors ShaderResources::BindType; -1 = no metadata
		uint32_t location = 0;

		bool hasMetadata() const { return bindType >= 0; }

		// --- Level 2: Resource binding ---
		BindingKey& operator=(::HardwareImage& img) { boundImage_ = &img; boundBuffer_ = nullptr; return *this; }
		BindingKey& operator=(::HardwareBuffer& buf) { boundBuffer_ = &buf; boundImage_ = nullptr; return *this; }
		::HardwareImage* boundImage() const { return boundImage_; }
		::HardwareBuffer* boundBuffer() const { return boundBuffer_; }

		::HardwareImage* boundImage_ = nullptr;
		::HardwareBuffer* boundBuffer_ = nullptr;
	};

	// Pipeline-bound field proxy for direct member access syntax:
	//   rasterizer.GlobalUniformParam.globalTime = currentTime;
	// PipelineType is RasterizerPipeline or ComputePipeline.
	// operator= implementation is deferred to CabbageHardware.h (needs ResourceProxy).
	template<typename PipelineType>
	struct BoundField
	{
		PipelineType* pipeline_;
		uint64_t byteOffset;
		uint32_t typeSize;
		int32_t  bindType;
		uint32_t location;

		BoundField(PipelineType* p, uint64_t off, uint32_t sz, int32_t bt, uint32_t loc)
			: pipeline_(p), byteOffset(off), typeSize(sz), bindType(bt), location(loc) {}

		template<typename T>
		BoundField& operator=(const T& value);
	};
}
