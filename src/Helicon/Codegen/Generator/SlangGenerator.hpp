#pragma once
#include <Codegen/ParseHelper.h>
#include <Codegen/AST/Node.hpp>
#include <Codegen/AST/Struct.hpp>
#include <Codegen/ParseHelper.h>
#include <boost/pfr/core_name.hpp>

namespace EmbeddedShader
{
	class ComputePipelineObject;
}

namespace EmbeddedShader::Generator
{
	class SlangGenerator final
	{
		friend class ComputePipelineObject;
		template<typename T>
		static constexpr std::string_view variateBasicTypeNameMap = "unknown";
	public:
#define DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(type, name) \
template<> constexpr std::string_view variateBasicTypeNameMap<type> = #name
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(int8_t, int8_t);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(int16_t, int16_t);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(int, int);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(int64_t, int64_t);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(uint8_t, uint8_t);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(uint16_t, uint16_t);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(uint32_t, uint);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(uint64_t, uint64_t);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(float, float);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(double, double);
	    DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(bool, bool);
	    //DEFINE_VARIATE_BASIC_TYPE_NAME_MAP(std::float16_t, half);
#undef DEFINE_VARIATE_BASIC_TYPE_NAME_MAP

		static std::string getShaderOutput(const Ast::EmbeddedShaderStructure& structure);
		static std::string getGlobalOutput(const Ast::EmbeddedShaderStructure& structure);

		template<typename T>
		static std::string getVariateTypeName() {return "unknown";}
		template<typename T> requires std::is_arithmetic_v<T>
		static std::string getVariateTypeName() {return std::string(variateBasicTypeNameMap<T>);}
		template<typename T> requires ktm::is_vector_v<T>
		static std::string getVariateTypeName();
		template<typename T> requires ktm::is_matrix_v<T>
		static std::string getVariateTypeName();

		template<typename T> requires std::is_arithmetic_v<T>
		static std::string getValueOutput(T value) {return std::to_string(value);}
		static std::string getValueOutput(bool value) {return value? "true" : "false";}

		template<typename T> requires ktm::is_vector_v<T>
		static std::string getValueOutput(const T& value)
		{
			auto array = value.to_array();
			std::string output = getVariateTypeName<T>() + "(";
			output += getValueOutput(array[0]);
			for (size_t i = 1; i < array.size(); ++i)
			{
				output += ", " + getValueOutput(array[i]);
			}
			output += ")";
			return output;
		}

		template<typename T> requires ktm::is_matrix_v<T>
		static std::string getValueOutput(const T& value)
		{
			auto array = value.to_array();
			std::string output = getVariateTypeName<T>() + "(";

			output += getValueOutput(array[0]);

			for (size_t i = 1; i < array.size(); ++i)
			{
				output += "," + getValueOutput(array[i]);
			}
			output += ")";
			return output;
		}

		template<typename T> requires std::is_aggregate_v<T>
		static std::string getValueOutput(const T& value);

		static std::string getParseOutput(const Ast::DefineLocalVariate* node);
		static std::string getParseOutput(const Ast::DefineInputVariate* node);
		static std::string getParseOutput(const Ast::Assign* node);
		static std::string getParseOutput(const Ast::BinaryOperator* node);
		static std::string getParseOutput(const Ast::MemberAccess* node);
		static std::string getParseOutput(const Ast::DefineOutputVariate* node);
		static std::string getParseOutput(const Ast::IfStatement* node);
		static std::string getParseOutput(const Ast::ElifStatement* node);
		static std::string getParseOutput(const Ast::ElseStatement* node);
		static std::string getParseOutput(const Ast::InputVariate* node);
		static std::string getParseOutput(const Ast::OutputVariate* node);
		static std::string getParseOutput(const Ast::DefineUniversalArray* node);
		static std::string getParseOutput(const Ast::DefineUniformVariate* node);
		static std::string getParseOutput(const Ast::UniformVariate* node);
		static std::string getParseOutput(const Ast::UniversalTexture2D* node);
		static std::string getParseOutput(const Ast::UniversalArray* node);
		static std::string getParseOutput(const Ast::DefineAggregateType* node);
		static std::string getParseOutput(const Ast::DefineUniversalTexture2D* node);
		static std::string getParseOutput(const Ast::UnaryOperator* node);
		static std::string getParseOutput(const Ast::ArrayType* node);
		static std::string getParseOutput(const Ast::Texture2DType* node);
		static std::string getParseOutput(const Ast::CallFunc* node);
		static std::string getParseOutput(const Ast::SamplerType* node);
		static std::string getParseOutput(const Ast::FunctionDeclaration* node);

		static std::shared_ptr<Ast::Variate> getPositionOutput();
		static std::shared_ptr<Ast::Variate> getDispatchThreadIDInput();
	private:
		static bool bindless();
		static thread_local inline std::string uboMembers;
		static thread_local inline std::string parameterBlockMembers;
		static thread_local inline std::string pushConstantMembers;
		static thread_local inline std::string bindlessHandleMembers;
		static thread_local inline ktm::uvec3 numthreads = ktm::uvec3(1);
		static thread_local inline Ast::ShaderStage currentStage;

		static thread_local inline size_t nestHierarchy = 1;
		static std::string getCodeIndentation();

		//貌似是冗余代码，暂时不清理，后续再查一下有没有依赖
		template<typename T>
		struct vec_traits
		{

		};

		template<size_t N,typename T>
		struct vec_traits<ktm::vec<N,T>>
		{
			static constexpr size_t element_count = N;
			using scalar_type = T;
		};

		template<typename T>
		struct mat_traits
		{

		};

		template<size_t Row,size_t Col,typename T>
		struct mat_traits<ktm::mat<Row,Col,T>>
		{
			static constexpr size_t row = Row;
			static constexpr size_t column = Col;
			using scalar_type = T;
		};
	};

	template<typename T> requires ktm::is_vector_v<T>
	std::string SlangGenerator::getVariateTypeName()
	{
		return getVariateTypeName<typename vec_traits<T>::scalar_type>() + std::to_string(vec_traits<T>::element_count);
	}

	template<typename T> requires ktm::is_matrix_v<T>
	std::string SlangGenerator::getVariateTypeName()
	{
		return getVariateTypeName<typename mat_traits<T>::scalar_type>() +
			std::to_string(mat_traits<T>::row) + "x" + std::to_string(mat_traits<T>::column);
	}

	template<typename T> requires std::is_aggregate_v<T>
	std::string SlangGenerator::getValueOutput(const T& value)
	{
	    // std::string output = "{";
	    // auto reflect = [&](std::string_view name, auto&& structMember, std::size_t i)
	    // {
	    //     using MemberType = std::remove_cvref_t<decltype(structMember)>;
	    //     if (i == 0)
	    //         output += getValueOutput<std::remove_cvref_t<typename MemberType::value_type>>(*structMember.value);
	    //     else output += "," + getValueOutput<std::remove_cvref_t<typename MemberType::value_type>>(*structMember.value);
	    // };
	    // boost::pfr::for_each_field_with_name(value, reflect);
	    // output += "}";
        return "{}";
	}

	struct DefineSystemSemanticVariate : Ast::Statement
	{
		std::shared_ptr<Ast::Variate> variate;
		std::string semanticName;
		std::string parse() override;
	};
}
