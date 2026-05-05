#pragma once

#include <Codegen/AST/Node.hpp>
#include <utility>
#include <boost/pfr/core.hpp>
#include <boost/pfr/core_name.hpp>
#include <Codegen/AST/Parser.hpp>
#include <Codegen/Generator/SlangGenerator.hpp>
#include <Codegen/ParseHelper.h>

namespace EmbeddedShader {
	struct SamplerProxy;
}

namespace EmbeddedShader::Generator
{
	class SlangGenerator;
}

namespace EmbeddedShader::Ast
{
	//AST不会写过多约束，此部分交给前端进行处理

	class AST
	{
		friend class Generator::SlangGenerator;
		AST() = default;
	public:

		template<typename T> requires std::is_arithmetic_v<T>
		static std::shared_ptr<BasicType> createType();
		template<typename T> requires ktm::is_vector_v<T>
		static std::shared_ptr<VecType> createType();
		template<typename T> requires ktm::is_matrix_v<T>
		static std::shared_ptr<MatType> createType();
		template<typename T> requires std::is_aggregate_v<T>
		static std::shared_ptr<AggregateType> createType();

		template<typename VariateType> requires std::is_arithmetic_v<VariateType>
		static std::shared_ptr<BasicValue> createValue(VariateType value);
		template<typename VariateType> requires ktm::is_vector_v<VariateType>
		static std::shared_ptr<VecValue> createValue(const VariateType& value);
		template<typename VariateType> requires ktm::is_matrix_v<VariateType>
		static std::shared_ptr<MatValue> createValue(const VariateType& value);
		template<typename VariateType> requires std::is_aggregate_v<VariateType>
		static std::shared_ptr<AggregateValue> createValue(const VariateType& value);

		template<typename ValueType,typename... Args> requires ktm::is_vector_v<ValueType>
		static std::shared_ptr<VecValue> createVecValue(Args&&... args);

		template<typename VariateType>
		static std::shared_ptr<LocalVariate> defineLocalVariate(VariateType&& value);
		static std::shared_ptr<LocalVariate> defineLocalVariate(std::shared_ptr<Type> type, std::shared_ptr<Value> initValue);

		static std::shared_ptr<Value> binaryOperator(std::shared_ptr<Value> value1, std::shared_ptr<Value> value2, std::string operatorType, std::shared_ptr<Type> type =
				                                             nullptr);
		static std::shared_ptr<Value> binaryOperator(auto&& value1, auto&& value2, std::string operatorType);
		static std::shared_ptr<Value> unaryOperator(std::shared_ptr<Value> value, std::string operatorType, bool isPrefix = true, AccessPermissions accessPermissions =
				                                            AccessPermissions::ReadOnly);

		static void assign(std::shared_ptr<Value> variate, std::shared_ptr<Value> value);
		static void assign(auto&& variate, auto&& value);

		static std::shared_ptr<InputVariate> defineInputVariate(std::shared_ptr<Type> type, size_t location);
		template<typename VariateType>
		static std::shared_ptr<InputVariate> defineInputVariate(size_t location);

		static std::shared_ptr<MemberAccess> access(std::shared_ptr<Value> value, std::string memberName, std::shared_ptr<Type> memberType);

		static std::shared_ptr<OutputVariate> defineOutputVariate(std::shared_ptr<Type> type, size_t location);
		template<typename VariateType>
		static std::shared_ptr<OutputVariate> defineOutputVariate(size_t location);

		static void beginIf(std::shared_ptr<Value> condition);
		static void endIf();
		static void beginElif(std::shared_ptr<Value> condition);
		static void endElif();
		static void beginElse();
		static void endElse();

		static std::shared_ptr<UniversalArray> defineUniversalArray(std::shared_ptr<Type> elementType);
		template<typename ElementType>
		static std::shared_ptr<UniversalArray> defineUniversalArray();

	    static std::shared_ptr<UniversalTexture2D> defineUniversalTexture2D(std::shared_ptr<Type> texelType);
	    template<typename ElementType>
        static std::shared_ptr<UniversalTexture2D> defineUniversalTexture2D();

		static std::shared_ptr<UniformVariate> defineUniformVariate(std::shared_ptr<Type> type, bool pushConstant = false);
		template<typename VariateType>
		static std::shared_ptr<UniformVariate> defineUniformVariate(bool pushConstant = false);

		template<typename T> requires std::is_aggregate_v<T>
		static std::shared_ptr<AggregateType> createAggregateType(const T& value);

		static std::shared_ptr<Variate> getPositionOutput();
		static std::shared_ptr<Variate> getDispatchThreadIDInput();

		static std::shared_ptr<ElementVariate> at(std::shared_ptr<Value> array, uint32_t index);
		static std::shared_ptr<ElementVariate> at(std::shared_ptr<Value> array, const std::shared_ptr<Value>& index);
	    template<typename IndexType> requires std::is_integral_v<IndexType>
	    static std::shared_ptr<ElementVariate> at(std::shared_ptr<Value> array, ktm::vec<2,IndexType> index);
		static void addLocalUniversalStatement(std::shared_ptr<Node> node);
		static std::shared_ptr<CallFunc> callFunc(std::string funcName,std::shared_ptr<Type> returnType,std::vector<std::shared_ptr<Value>> args);
		static void callFunc(std::string funcName,std::vector<std::shared_ptr<Value>> args);
		static std::shared_ptr<FunctionDeclaration> functionDeclaration(std::string funcName, std::string returnType,
		                                                                std::vector<std::pair<std::string,std::string>> args);
		static void addLocalStatement(std::shared_ptr<Statement> statement);
		static void addInputStatement(std::shared_ptr<Statement> inputStatement);
		static void addOutputStatement(std::shared_ptr<Statement> outputStatement);
		static void addGlobalStatement(std::shared_ptr<Statement> globalStatement);
		static void addShaderOnlyStatement(std::shared_ptr<Statement> shaderOnlyStatement);
		static std::stack<std::vector<std::shared_ptr<Statement>>*>& getLocalStatementStack();
		static EmbeddedShaderStructure& getEmbeddedShaderStructure();
	private:
		template<typename Type>
		struct ValueConverter
		{
			static std::shared_ptr<Value> convert(Type&& value)
			{
				return createValue(std::forward<Type>(value));
			}
		};

		template<typename Type> requires std::is_arithmetic_v<Type>
		struct ValueConverter<Type>
		{
			static std::shared_ptr<Value> convert(Type value)
			{
				return createValue(std::forward<Type>(value));
			}
		};

		template<typename Type>
		struct ValueConverter<std::shared_ptr<Type>>
		{
			static std::shared_ptr<Value> convert(const std::shared_ptr<Type>& value)
			{
				return value;
			}
		};

		static std::shared_ptr<Value> valueConverter(auto&& value)
		{
			return ValueConverter<std::remove_cvref_t<decltype(value)>>::convert(std::forward<decltype(value)>(value));
		}

		template<typename T> requires ktm::is_vector_v<T>
		static std::shared_ptr<VecType> createVecType()
		{
			auto type = std::make_shared<VecType>();
			type->name = Generator::SlangGenerator::getVariateTypeName<T>();
			return type;
		}
	};

	template<typename T> requires std::is_arithmetic_v<T>
	std::shared_ptr<BasicType> AST::createType()
	{
		auto type = std::make_shared<BasicType>();
		type->name = Generator::SlangGenerator::getVariateTypeName<T>();
		return type;
	}

	template<typename T> requires ktm::is_vector_v<T>
	std::shared_ptr<VecType> AST::createType()
	{
		return createVecType<T>();
	}

	template<typename T> requires ktm::is_matrix_v<T>
	std::shared_ptr<MatType> AST::createType()
	{
		auto type = std::make_shared<MatType>();
		type->name = Generator::SlangGenerator::getVariateTypeName<T>();
		return type;
	}

	template<typename T> requires std::is_aggregate_v<T>
	std::shared_ptr<AggregateType> AST::createType()
	{
		return createAggregateType<T>({});
	}

	template<typename VariateType> requires std::is_arithmetic_v<VariateType>
	std::shared_ptr<BasicValue> AST::createValue(VariateType value)
	{
		auto basicValue = std::make_shared<BasicValue>();
		basicValue->value = Generator::SlangGenerator::getValueOutput(std::forward<VariateType>(value));
		basicValue->type = createType<std::remove_cvref_t<VariateType>>();
		return basicValue;
	}

	template<typename VariateType> requires ktm::is_vector_v<VariateType>
	std::shared_ptr<VecValue> AST::createValue(const VariateType& value)
	{
		auto type = createVecType<VariateType>();
		auto vecValue = std::make_shared<VecValue>();
		vecValue->type = type;

		vecValue->value = Generator::SlangGenerator::getValueOutput<VariateType>(value);
		return vecValue;
	}

	template<typename VariateType> requires ktm::is_matrix_v<VariateType>
	std::shared_ptr<MatValue> AST::createValue(const VariateType& value)
	{
		auto type = createType<VariateType>();
		auto vecValue = std::make_shared<MatValue>();
		vecValue->type = type;

		vecValue->value = Generator::SlangGenerator::getValueOutput<VariateType>(value);
		return vecValue;
	}

	template<typename VariateType> requires std::is_aggregate_v<VariateType>
	std::shared_ptr<AggregateValue> AST::createValue(const VariateType& value)
	{
		auto aggregateValue = std::make_shared<AggregateValue>();
		aggregateValue->type = createType<std::remove_cvref_t<VariateType>>();
		aggregateValue->value = Generator::SlangGenerator::getValueOutput<std::remove_cvref_t<VariateType>>(value);
		return aggregateValue;
	}

	template<typename ValueType, typename ... Args> requires ktm::is_vector_v<ValueType>
	std::shared_ptr<VecValue> AST::createVecValue(Args&&... args)
	{
		auto type = createVecType<ValueType>();
		auto vecValue = std::make_shared<VecValue>();
		vecValue->type = type;

		bool first = true;
		//ide可能会误报警告
		vecValue->value = Generator::SlangGenerator::getVariateTypeName<ValueType>() + "(" + ((first? (first = false,valueConverter(std::forward<Args>(args))->parse()) :
				valueConverter(std::forward<Args>(args))->parse() + ",") + ...) + ")";
		return vecValue;
	}

	template<typename VariateType>
	std::shared_ptr<LocalVariate> AST::defineLocalVariate(VariateType&& value)
	{
		return defineLocalVariate(createType<std::remove_cvref_t<VariateType>>(),createValue(std::forward<VariateType>(value)));
	}

	std::shared_ptr<Value> AST::binaryOperator(auto&& value1, auto&& value2, std::string operatorType)
	{
		return binaryOperator(valueConverter(std::forward<decltype(value1)>(value1)),valueConverter(std::forward<decltype(value2)>(value2)),std::move(operatorType));
	}

	void AST::assign(auto&& variate, auto&& value)
	{
		assign(valueConverter(std::forward<decltype(variate)>(variate)),valueConverter(std::forward<decltype(value)>(value)));
	}

	template<typename VariateType>
	std::shared_ptr<InputVariate> AST::defineInputVariate(size_t location)
	{
		return defineInputVariate(createType<std::remove_cvref_t<VariateType>>(),location);
	}

	template<typename VariateType>
	std::shared_ptr<OutputVariate> AST::defineOutputVariate(size_t location)
	{
		return defineOutputVariate(createType<std::remove_cvref_t<VariateType>>(), location);
	}

	template<typename ElementType>
	std::shared_ptr<UniversalArray> AST::defineUniversalArray()
	{
		return defineUniversalArray(createType<std::remove_cvref_t<ElementType>>());
	}

    template <typename ElementType>
    std::shared_ptr<UniversalTexture2D> AST::defineUniversalTexture2D()
	{
	    return defineUniversalTexture2D(createType<std::remove_cvref_t<ElementType>>());
	}

    template<typename VariateType>
	std::shared_ptr<UniformVariate> AST::defineUniformVariate(bool pushConstant)
	{
		return defineUniformVariate(createType<VariateType>(),pushConstant);
	}

	template<typename T> requires std::is_aggregate_v<T>
	std::shared_ptr<AggregateType> AST::createAggregateType(const T& value)
	{
		auto& map = AggregateType::aggregateTypeMap;
		auto it = map.find(typeid(T).name());
		if (it != map.end())
		{
			auto aggregateType = it->second;
			return aggregateType;
		}

		auto aggregateType = std::make_shared<AggregateType>();
		aggregateType->name = Parser::getUniqueAggregateTypeName();
		auto reflect = [&](std::string_view name, auto&& structMember, std::size_t i)
		{
			using MemberType = std::remove_cvref_t<decltype(structMember)>;
			auto member = std::make_shared<Variate>();
			member->name = name;

			if constexpr (ParseHelper::isVariateProxy<MemberType>())
				member->type = createType<std::remove_cvref_t<typename MemberType::value_type>>();
			else if constexpr (ParseHelper::isArrayProxy<MemberType>())
			{
				auto arrayType = std::make_shared<ArrayType>();
				arrayType->elementType = createType<std::remove_cvref_t<typename MemberType::value_type>>();
				member->type = std::move(arrayType);
			}
			else if constexpr (ParseHelper::isTexture2DProxy<MemberType>())
			{
				auto texture2DType = std::make_shared<Texture2DType>();
				texture2DType->texelType = createType<std::remove_cvref_t<typename MemberType::value_type>>();
				member->type = std::move(texture2DType);
			}
			else if constexpr (std::is_same_v<MemberType, SamplerProxy>)
			{
				auto samplerType = std::make_shared<SamplerType>();
				samplerType->name = "SamplerState";
				member->type = std::move(samplerType);
			}

			aggregateType->members.push_back(std::move(member));
		};
		boost::pfr::for_each_field_with_name(value,reflect);

		auto defineNode = std::make_shared<DefineAggregateType>();
		defineNode->aggregate = aggregateType;
		addGlobalStatement(defineNode);

		map.insert({typeid(T).name(), aggregateType});
		return aggregateType;
	}

	template<typename IndexType> requires std::is_integral_v<IndexType>
	std::shared_ptr<ElementVariate> AST::at(std::shared_ptr<Value> array, ktm::vec<2, IndexType> index)
	{
		auto variate = std::make_shared<ElementVariate>();
		variate->type = array->type;
		variate->name = array->parse() + "[" + Generator::SlangGenerator::getValueOutput(index) + "]";
		variate->array = std::move(array);
		return variate;
	}
}
