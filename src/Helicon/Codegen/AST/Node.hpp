#pragma once
#include <string>
#include <memory>

#include <Codegen/AST/Enum.hpp>
#include <Codegen/AST/Node.hpp>

namespace EmbeddedShader::Ast
{
	struct DefineLocalVariate;
	class Value;
	struct Node
	{
		friend class Parser;
		virtual ~Node() = default;
		virtual std::string parse();
		static const std::vector<std::shared_ptr<Value>>& accessAll(const std::vector<std::shared_ptr<Value>>& values,AccessPermissions permissions);
	};

	class Type : public Node
	{
	public:
		virtual void access(AccessPermissions permissions);
	};

	struct NameType : Type
	{
		std::string name;
		std::string parse() override;
	};

	class Value : public Node
	{
	public:
		std::shared_ptr<Type> type;
		virtual void access(AccessPermissions permissions);
		virtual std::string accessPath();
	};

	struct Variate : Value
	{
		std::string name;
		std::string parse() override;
		std::string accessPath() override;
	};

	struct LocalVariate : Variate
	{

	};

	struct BasicType : NameType
	{

	};

	struct VecType : NameType
	{
	};

	struct BasicValue : Value
	{
		std::string value;
		std::string parse() override;
	};

	struct VecValue : Value
	{
		std::string value;
		std::string parse() override;
	};

	struct Statement : Node
	{
		virtual void resetAccessPermissions() {}
	};

	struct DefineLocalVariate : Statement
	{
		std::shared_ptr<LocalVariate> localVariate;
		std::shared_ptr<Value> value;
		std::string parse() override;
	};

	struct BinaryOperator : Value
	{
		std::shared_ptr<Value> value1;
		std::shared_ptr<Value> value2;
		std::string operatorType;
		std::string parse() override;
	};

	struct Assign : Statement
    {
        std::shared_ptr<Value> leftValue;
        std::shared_ptr<Value> rightValue;
		std::string parse() override;
    };

	struct InputVariate : Variate
	{
		size_t location = 0;
		std::string parse() override;
	};

	struct DefineInputVariate : Statement
    {
		std::shared_ptr<InputVariate> variate;
		std::string parse() override;
    };

	struct MemberAccess : Value
	{
		std::shared_ptr<Value> value;
		std::string memberName;
		std::string parse() override;
		void access(AccessPermissions permissions) override;
		std::string accessPath() override;
	};

	struct OutputVariate : Variate
	{
		size_t location = 0;
		std::string parse() override;
	};

	struct DefineOutputVariate : Statement
	{
		std::shared_ptr<OutputVariate> variate;
		std::string parse() override;
	};

	struct IfStatement : Statement
    {
        std::shared_ptr<Value> condition;
        std::vector<std::shared_ptr<Statement>> statements;
        std::string parse() override;
    };

	struct ElifStatement : Statement
	{
		std::shared_ptr<Value> condition;
		std::vector<std::shared_ptr<Statement>> statements;
		std::string parse() override;
	};

	struct ElseStatement : Statement
	{
		std::vector<std::shared_ptr<Statement>> statements;
		std::string parse() override;
	};

	//StructuredBuffer or RWStructuredBuffer
	struct UniversalArray : Variate
	{
		//Variate::type 将被解释为元素类型

		AccessPermissions permissions = AccessPermissions::None;
		void access(AccessPermissions permissions) override;
		std::string parse() override;
	};

	struct ElementVariate : Variate
	{
		std::shared_ptr<Value> array;
		void access(AccessPermissions permissions) override;
	};

	struct DefineUniversalArray : Statement
	{
		std::shared_ptr<UniversalArray> array;
		std::string parse() override;
		void resetAccessPermissions() override;
	};

	struct UniformVariate : Variate
	{
		AccessPermissions permissions = AccessPermissions::None;
		bool pushConstant = false;
		std::string parse() override;
		void access(AccessPermissions permissions) override;
	};

	struct DefineUniformVariate : Statement
	{
		std::shared_ptr<UniformVariate> variate;
		std::string parse() override;
		void resetAccessPermissions() override;
	};

	struct AggregateType : NameType
	{
		AccessPermissions permissions = AccessPermissions::None;
		std::vector<std::shared_ptr<Variate>> members;
		static thread_local inline std::unordered_map<std::string,std::shared_ptr<AggregateType>> aggregateTypeMap;
		void access(AccessPermissions permissions) override;
	};

	struct DefineAggregateType : Statement
	{
		std::shared_ptr<AggregateType> aggregate;
		std::string parse() override;
		void resetAccessPermissions() override;
	};

	struct AggregateValue : Value
	{
		std::string value;
		std::string parse() override;
	};

    //RWTexture2D/Texture2D
    struct UniversalTexture2D : Variate
    {
        //Variate::type 将被解释为元素类型

        AccessPermissions permissions = AccessPermissions::None;
        void access(AccessPermissions permissions) override;
    	std::string parse() override;

        // Back-pointer to the C++ proxy's boundResource_ (void*).
        // Used by auto-bind to read the current resource at dispatch time.
        void** boundResourceRef = nullptr;

        // Render target location assigned by Texture2DProxy::operator().
        // -1 means this texture is NOT used as a render target output.
        int32_t renderTargetLocation = -1;
    };

    struct DefineUniversalTexture2D : Statement
    {
        std::shared_ptr<UniversalTexture2D> texture;
        std::string parse() override;
        void resetAccessPermissions() override;
    };

	struct MatType : NameType
	{

	};

	struct MatValue : Value
	{
		std::string value;
		std::string parse() override;
	};

	struct UnaryOperator : Value
	{
		std::shared_ptr<Value> value;
		std::string operatorType;
		bool isPrefix = true;
		std::string parse() override;
	};

	struct UniversalStatement : Statement
	{
		std::shared_ptr<Node> node;
		std::string parse() override;
	};

	struct ArrayType : Type
	{
		AccessPermissions permissions = AccessPermissions::None;
		std::shared_ptr<Type> elementType;
		std::string parse() override;
		void access(AccessPermissions permissions) override;
	};

	struct Texture2DType : Type
	{
		std::shared_ptr<Type> texelType;
		std::string name = "Texture2D";
		std::string parse() override;
	};

	struct CallFunc : Value
	{
		std::string funcName;
		std::vector<std::shared_ptr<Value>> args;
		std::string parse() override;
	};

	struct SamplerType : NameType
	{
		std::string parse() override;
	};

	struct FunctionDeclaration : Statement
	{
		std::string funcName;
		std::string returnType;
	        std::vector<std::pair<std::string,std::string>> args;
		std::string parse() override;
	};
}
