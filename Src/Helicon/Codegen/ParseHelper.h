#pragma once
#include <functional>
#include <stack>
#include <Codegen/AST/Node.hpp>

namespace EmbeddedShader
{
	template<typename Type>
	struct VariateProxy;

	template<typename Type>
	struct ArrayProxy;

	template<typename Type>
	struct Texture2DProxy;

	class ParseHelper final
	{
	public:
		template<typename ReturnType> requires std::is_same_v<ReturnType, void>
		static void callLambda(std::function<ReturnType()> f)
		{
			instance.bIsInShaderCodeLambda = true;
			f();
			instance.bIsInShaderCodeLambda = false;
		}

		template<typename ReturnType> requires (!std::is_same_v<ReturnType, void>)
		static auto callLambda(std::function<ReturnType()> f)
		{
			instance.bIsInShaderCodeLambda = true;
			auto result = f();
			instance.bIsInShaderCodeLambda = false;
			return result;
		}

		template<typename ReturnType,typename ParamType> requires std::is_same_v<ReturnType, void>
		static auto callLambda(std::function<ReturnType(ParamType)> f, ParamType param)
		{
			instance.bIsInShaderCodeLambda = true;
			f(std::move(param));
			instance.bIsInShaderCodeLambda = false;
		}

		template<typename ReturnType,typename ParamType> requires (!std::is_same_v<ReturnType, void>)
		static auto callLambda(std::function<ReturnType(ParamType)> f, ParamType param)
		{
			instance.bIsInShaderCodeLambda = true;
			auto result = f(std::move(param));
			instance.bIsInShaderCodeLambda = false;
			return result;
		}

		template<typename ReturnType, typename... ParamTypes> requires std::is_same_v<ReturnType, void>
		static auto callLambda(std::function<ReturnType(ParamTypes...)> f,std::tuple<ParamTypes...> params)
		{
			instance.bIsInShaderCodeLambda = true;
			std::apply(f, std::move(params));
			instance.bIsInShaderCodeLambda = false;
		}

		template<typename ReturnType, typename... ParamTypes> requires (!std::is_same_v<ReturnType, void>)
		static auto callLambda(std::function<ReturnType(ParamTypes...)> f,std::tuple<ParamTypes...> params)
		{
			instance.bIsInShaderCodeLambda = true;
			auto result = std::apply(f, std::move(params));
			instance.bIsInShaderCodeLambda = false;
			return result;
		}

		template<typename ReturnType,typename... ParamTypes>
		static std::tuple<ParamTypes...> createParamTuple(const std::function<ReturnType(ParamTypes...)>& f)
		{
			instance.bIsInInputParameter = true;
			instance.currentInputIndex = sizeof...(ParamTypes);
			auto tuple = std::tuple<ParamTypes...>();
			instance.bIsInInputParameter = false;
			return tuple;
		}

		template<typename ReturnType,typename ParamType>
		static ParamType createParam(const std::function<ReturnType(ParamType)>& f)
		{
			instance.bIsInInputParameter = true;
			instance.currentInputIndex = 1;
			ParamType param;
			instance.bIsInInputParameter = false;
			return param;
		}

		template<typename ReturnType,typename... ParamTypes>
		static constexpr bool hasReturnValue(const std::function<ReturnType(ParamTypes...)>& f)
		{
			return !std::is_same_v<ReturnType, void>;
		}

		template<typename ReturnType1,typename ReturnType2,typename... ParamTypes1,typename... ParamTypes2>
		static constexpr bool isMatchInputAndOutput(const std::function<ReturnType1(ParamTypes1...)>& fOutput,const std::function<ReturnType2(ParamTypes2...)>& fInput)
		{
			//1.没有输入输出
			//2. 输入和输出类型相同
			//note:这里的if constexpr不能省略，因为std::is_same_v<ReturnType1,ParamTypes2...>这条代码必须要求sizeof...(ParamTypes2) == 1
			if constexpr (std::is_same_v<ReturnType1, void> && sizeof...(ParamTypes2) == 0)
				return true;
			else if constexpr (!std::is_same_v<ReturnType1, void> && sizeof...(ParamTypes2) == 1)
			{
				if constexpr (std::is_same_v<ReturnType1,ParamTypes2...>)
					return true;
			}
			else
				return false;
		}

		template<typename T,typename... ParamTypes>
		static constexpr bool isReturnVariateProxy(const std::function<T(ParamTypes...)>& f)
		{
		    return IsReturnVariateProxy<std::remove_cvref_t<decltype(f)>>::value;
		}

		template<typename T>
		static constexpr bool isVariateProxy()
		{
			return IsVariateProxy<std::remove_cvref_t<T>>::value;
		}

		template<typename T>
		static constexpr bool isArrayProxy()
		{
			return IsArrayProxy<std::remove_cvref_t<T>>::value;
		}

		template<typename T>
		static constexpr bool isTexture2DProxy()
		{
			return IsTexture2DProxy<std::remove_cvref_t<T>>::value;
		}

		static bool isInInputParameter()
		{
			return instance.bIsInInputParameter;
		}

		static bool isInShaderCodeLambda()
		{
			return instance.bIsInShaderCodeLambda;
		}

		static size_t getCurrentInputIndex()
		{
			return --instance.currentInputIndex;
		}

	    static void beginAggregateParent(std::shared_ptr<Ast::Value> parent)
		{
		    instance.aggregateParentStack.emplace(std::move(parent),0);
        }

	    static void endAggregateParent()
		{
		    instance.aggregateParentStack.pop();
		}

	    static std::shared_ptr<Ast::Value> getAggregateParent()
		{
		    if (instance.aggregateParentStack.empty())
		        return nullptr;
		    return instance.aggregateParentStack.top().first;
		}

	    static size_t getAggregateMemberIndex()
		{
		    return instance.aggregateParentStack.top().second++;
		}

	    static void beginNotInitNode()
		{
		    instance.notInitNodeStack.push(true);
		}
	    static void endNotInitNode()
		{
		    instance.notInitNodeStack.pop();
		}

	    static bool notInitNode()
		{
		    if (instance.notInitNodeStack.empty())
		        return false;
		    return instance.notInitNodeStack.top();
		}

		static void beginVecComponentInit(std::shared_ptr<Ast::Value> vec)
		{
			instance.vec = std::move(vec);
		}

		static void endVecComponentInit()
		{
			instance.vec = nullptr;
			instance.currentVecComponentIndex = 0;
		}

		static std::shared_ptr<Ast::Value> getVecParent()
		{
			return instance.vec;
		}

		static std::string getVecComponentName()
		{
			auto index = instance.currentVecComponentIndex++;
			switch (index)
			{
				case 0:
					return "x";
				case 1:
					return "y";
				case 2:
					return "z";
				case 3:
					return "w";
				default:break;
			}
			return "unknown";
		}
    public:
        // 类型特征模板 - 需要在 FunctionProxy 的 requires 子句中使用
        template<typename T>
		struct IsVariateProxy
		{
			static constexpr bool value = false;
		};

		template<typename T>
		struct IsVariateProxy<VariateProxy<T>>
		{
			static constexpr bool value = true;
		};

		template<typename T>
		struct IsArrayProxy
		{
			static constexpr bool value = false;
		};

		template<typename T>
		struct IsArrayProxy<ArrayProxy<T>>
		{
			static constexpr bool value = true;
		};

		template<typename T>
		struct IsTexture2DProxy
		{
			static constexpr bool value = false;
		};

		template<typename T>
		struct IsTexture2DProxy<Texture2DProxy<T>>
		{
			static constexpr bool value = true;
		};

    private:
        bool bIsInInputParameter = false;
        bool bIsInShaderCodeLambda = false;
		std::shared_ptr<Ast::Value> vec;
		size_t currentVecComponentIndex = 0;
		size_t currentInputIndex = 0;
		std::stack<std::pair<std::shared_ptr<Ast::Value>,size_t>> aggregateParentStack;
        std::stack<bool> notInitNodeStack;
        static thread_local ParseHelper instance;

	    template<typename T>
        struct IsReturnVariateProxy
	    {
	        static constexpr bool value = false;
	    };

	    template<typename T, typename... ParamTypes>
        struct IsReturnVariateProxy<std::function<VariateProxy<T>(ParamTypes...)>>
	    {
	        static constexpr bool value = true;
	    };
	};

	inline thread_local ParseHelper ParseHelper::instance;
}
