#include "AST.hpp"
#include <utility>
#include <Codegen/Generator/SlangGenerator.hpp>

#include "Parser.hpp"
#include "Compiler/ShaderCommon.h"

#include <optional>

std::shared_ptr<EmbeddedShader::Ast::LocalVariate> EmbeddedShader::Ast::AST::defineLocalVariate(std::shared_ptr<Type> type, std::shared_ptr<Value> initValue)
{
	if (initValue)
		initValue->access(AccessPermissions::ReadOnly);
	auto localVariate = std::make_shared<LocalVariate>();
	localVariate->name = Parser::getUniqueVariateName();
	localVariate->type = std::move(type);
	auto defineNode = std::make_shared<DefineLocalVariate>();
	defineNode->localVariate = localVariate;
	defineNode->value = std::move(initValue);
	addLocalStatement(defineNode);
	return localVariate;
}

std::shared_ptr<EmbeddedShader::Ast::Value> EmbeddedShader::Ast::AST::binaryOperator(std::shared_ptr<Value> value1, std::shared_ptr<Value> value2,
	std::string operatorType,std::shared_ptr<Type> type)
{
	value1->access(AccessPermissions::ReadOnly);
	value2->access(AccessPermissions::ReadOnly);
	auto binaryOp = std::make_shared<BinaryOperator>();
	binaryOp->value1 = std::move(value1);
	binaryOp->value2 = std::move(value2);
	binaryOp->operatorType = std::move(operatorType);
	binaryOp->type = type? std::move(type) : binaryOp->value1->type;
	return binaryOp; // 这里需要实现具体的操作符逻辑
}

std::shared_ptr<EmbeddedShader::Ast::Value> EmbeddedShader::Ast::AST::unaryOperator(std::shared_ptr<Value> value,
	std::string operatorType, bool isPrefix, AccessPermissions accessPermissions)
{
	value->access(accessPermissions);
	auto unaryOp = std::make_shared<UnaryOperator>();
	unaryOp->value = std::move(value);
	unaryOp->operatorType = std::move(operatorType);
	unaryOp->isPrefix = isPrefix;
	unaryOp->type = unaryOp->value->type;
	return unaryOp;
}

void EmbeddedShader::Ast::AST::assign(std::shared_ptr<Value> variate, std::shared_ptr<Value> value)
{
	variate->access(AccessPermissions::WriteOnly);
	value->access(AccessPermissions::ReadOnly);
	auto assignNode = std::make_shared<Assign>();
	assignNode->leftValue = std::move(variate);
	assignNode->rightValue = std::move(value);
	addLocalStatement(assignNode);
}

std::shared_ptr<EmbeddedShader::Ast::InputVariate> EmbeddedShader::Ast::AST::defineInputVariate(std::shared_ptr<Type> type, size_t location)
{
	auto inputVariate = std::make_shared<InputVariate>();
	inputVariate->type = std::move(type);
	inputVariate->name = Parser::getUniqueVariateName();
	inputVariate->location = location;
	auto defineNode = std::make_shared<DefineInputVariate>();
	defineNode->variate = inputVariate;
	addInputStatement(defineNode);
	return inputVariate;
}

std::shared_ptr<EmbeddedShader::Ast::MemberAccess> EmbeddedShader::Ast::AST::access(std::shared_ptr<Value> value,
                                                                                    std::string memberName, std::shared_ptr<Type> memberType) {
	auto memberAccess = std::make_shared<MemberAccess>();
	memberAccess->value = std::move(value);
	memberAccess->memberName = std::move(memberName);
    memberAccess->type = std::move(memberType);
	return memberAccess;
}

std::shared_ptr<EmbeddedShader::Ast::OutputVariate> EmbeddedShader::Ast::AST::defineOutputVariate(std::shared_ptr<Type> type, size_t location)
{
	auto outputVariate = std::make_shared<OutputVariate>();
	outputVariate->type = std::move(type);
	outputVariate->name = Parser::getUniqueVariateName();
	outputVariate->location = location;
	auto defineNode = std::make_shared<DefineOutputVariate>();
	defineNode->variate = outputVariate;
	addOutputStatement(defineNode);
	return outputVariate;
}

std::shared_ptr<EmbeddedShader::Ast::IfStatement> EmbeddedShader::Ast::AST::beginIf(std::shared_ptr<Value> condition, std::optional<std::function<bool()>> conditionDetector)
{
	auto ifStatement = std::make_shared<IfStatement>();
    if (conditionDetector.has_value())
    {
        ifStatement->index = Parser::getCurrentBranchIndex();
    }
    ifStatement->condition = std::move(condition);
	ifStatement->conditionDetector = std::move(conditionDetector);
	addLocalStatement(ifStatement);
	getLocalStatementStack().push(&ifStatement->statements);
    return ifStatement;
}

void EmbeddedShader::Ast::AST::endIf()
{
	getLocalStatementStack().pop();
}

void EmbeddedShader::Ast::AST::beginElse(std::shared_ptr<IfStatement> followIf)
{
	auto elseStatement = std::make_shared<ElseStatement>();
    elseStatement->followIf = std::move(followIf);
	addLocalStatement(elseStatement);
	getLocalStatementStack().push(&elseStatement->statements);
}

void EmbeddedShader::Ast::AST::endElse()
{
	getLocalStatementStack().pop();
}

std::shared_ptr<EmbeddedShader::Ast::UniversalArray> EmbeddedShader::Ast::AST::defineUniversalArray(std::shared_ptr<Type> elementType)
{
	auto arrayType = std::make_shared<ArrayType>();
	arrayType->elementType = std::move(elementType);
	auto variate = std::make_shared<UniversalArray>();
	variate->type = std::move(arrayType);
	variate->name = Parser::getUniqueGlobalVariateName();
	auto defineNode = std::make_shared<DefineUniversalArray>();
	defineNode->array = variate;
	addGlobalStatement(defineNode);
	return variate;
}

std::shared_ptr<EmbeddedShader::Ast::UniversalTexture2D> EmbeddedShader::Ast::AST::defineUniversalTexture2D(std::shared_ptr<Type> texelType)
{
	auto textureType = std::make_shared<Texture2DType>();
	textureType->texelType = std::move(texelType);
    auto variate = std::make_shared<UniversalTexture2D>();
    variate->type = textureType;
    variate->name = Parser::getUniqueGlobalVariateName();
    auto defineNode = std::make_shared<DefineUniversalTexture2D>();
    defineNode->texture = variate;
    addGlobalStatement(defineNode);
    return variate;
}

std::shared_ptr<EmbeddedShader::Ast::UniformVariate> EmbeddedShader::Ast::AST::defineUniformVariate(
	std::shared_ptr<Type> type, bool pushConstant)
{
	auto variate = std::make_shared<UniformVariate>();
	variate->type = std::move(type);
	variate->name = Parser::getUniqueGlobalVariateName();
	variate->pushConstant = pushConstant;
	auto defineNode = std::make_shared<DefineUniformVariate>();
	defineNode->variate = variate;
	addGlobalStatement(defineNode);
	return variate;
}

std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Ast::AST::getPositionOutput()
{
	auto& posOutput = Parser::currentParser->positionOutput;
	if (posOutput)
		return posOutput;
	posOutput = Generator::SlangGenerator::getPositionOutput();
	return posOutput;
}

std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Ast::AST::getDispatchThreadIDInput()
{
	auto& idOutput = Parser::currentParser->dispatchThreadIDInput;
	if (idOutput)
		return idOutput;
	idOutput = Generator::SlangGenerator::getDispatchThreadIDInput();
	return idOutput;
}

std::shared_ptr<EmbeddedShader::Ast::ElementValue> EmbeddedShader::Ast::AST::at(
	std::shared_ptr<Value> array, uint32_t index)
{
	auto element = std::make_shared<ElementValue>();
	element->type = array->type;
	element->index = createValue(index);
	element->array = std::move(array);
	return element;
}

std::shared_ptr<EmbeddedShader::Ast::ElementValue> EmbeddedShader::Ast::AST::at(std::shared_ptr<Value> array,
	const std::shared_ptr<Value>& index)
{
	index->access(AccessPermissions::ReadOnly);
	auto element = std::make_shared<ElementValue>();
	element->type = array->type;
	element->index = index;
	element->array = std::move(array);
	return element;
}

void EmbeddedShader::Ast::AST::addLocalUniversalStatement(
	std::shared_ptr<Node> node)
{
	auto universalStatement = std::make_shared<UniversalStatement>();
	universalStatement->node = std::move(node);
	addLocalStatement(universalStatement);
}

std::shared_ptr<EmbeddedShader::Ast::CallFunc> EmbeddedShader::Ast::AST::callFunc(std::string funcName,
	std::shared_ptr<Type> returnType, std::vector<std::shared_ptr<Value>> args)
{
	auto func = std::make_shared<CallFunc>();
	func->type = std::move(returnType);
	func->args = std::move(args);
	func->funcName = std::move(funcName);
	return func;
}

void EmbeddedShader::Ast::AST::callFunc(std::string funcName, std::vector<std::shared_ptr<Value>> args)
{
	addLocalUniversalStatement(callFunc(std::move(funcName),nullptr,std::move(args)));
}

std::shared_ptr<EmbeddedShader::Ast::FunctionDeclaration> EmbeddedShader::Ast::AST::functionDeclaration(
	std::string funcName, std::string returnType,
	std::vector<std::pair<std::string,std::string>> args)
{
	auto funcDecl = std::make_shared<FunctionDeclaration>();
	funcDecl->funcName = std::move(funcName);
	funcDecl->returnType = std::move(returnType);
	funcDecl->args = std::move(args);
	return funcDecl;
}

void EmbeddedShader::Ast::AST::addLocalStatement(std::shared_ptr<Statement> statement)
{
	getLocalStatementStack().top()->push_back(std::move(statement));
}

void EmbeddedShader::Ast::AST::addInputStatement(std::shared_ptr<Statement> inputStatement)
{
	Parser::currentParser->structure.inputStatements.push_back(std::move(inputStatement));
}

void EmbeddedShader::Ast::AST::addOutputStatement(std::shared_ptr<Statement> outputStatement)
{
	Parser::currentParser->structure.outputStatements.push_back(std::move(outputStatement));
}

void EmbeddedShader::Ast::AST::addGlobalStatement(std::shared_ptr<Statement> globalStatement)
{
	Parser::currentParser->structure.globalStatements.push_back(std::move(globalStatement));
}

void EmbeddedShader::Ast::AST::addShaderOnlyStatement(std::shared_ptr<Statement> shaderOnlyStatement)
{
	Parser::currentParser->structure.shaderOnlyStatements.push_back(std::move(shaderOnlyStatement));
}

std::stack<std::vector<std::shared_ptr<EmbeddedShader::Ast::Statement>>*>& EmbeddedShader::Ast::AST::getLocalStatementStack()
{
	return Parser::currentParser->localStatementStack;
}

EmbeddedShader::Ast::EmbeddedShaderStructure& EmbeddedShader::Ast::AST::getEmbeddedShaderStructure()
{
	return Parser::currentParser->structure;
}
std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Ast::AST::getGlobalUBO()
{
	auto& ubo = Parser::currentParser->globalUBO;
	if (!ubo)
	{
        auto type = std::make_shared<NameType>();
		ubo = std::make_shared<Variate>();
		ubo->type = type;
		type->name = "ConstantBuffer<global_ubo_struct>";
		ubo->name = "global_ubo";
	}
	return ubo;
}
std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Ast::AST::getGlobalParameterBlock()
{
    auto& pb = Parser::currentParser->globalParameterBlock;
    if (!pb)
    {
        auto type = std::make_shared<NameType>();
        pb = std::make_shared<Variate>();
        pb->type = type;
        type->name = "ParameterBlock<parameter_block_struct>";
        pb->name = "global_parameter_block";
    }
    return pb;
}std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Ast::AST::getGlobalPushConstant(){
    auto& pc = Parser::currentParser->globalPushConstant;
    if (!pc)
    {
        auto type = std::make_shared<NameType>();
        pc = std::make_shared<Variate>();
        pc->type = type;
        // push constant 必须是裸 struct 类型:[[vk::push_constant]] T name;
        // 曾误用 ParameterBlock<...> 包裹 —— Slang 会优先按 ParameterBlock 分配 descriptor set
        // (在 bindless 下落到 set 3),把 push constant 编译成 set-3 UBO,与 runtime 的
        // VkPushConstantRange/push_constant_data_ 期望不符,触发 VUID-...layout-07988。
        // 直接用 struct 名,使 [[vk::push_constant]] 真正生效。对比 getGlobalUBO 用 ConstantBuffer<...>。
        type->name = "global_push_constant_struct";
        pc->name = "global_push_constant";
    }
    return pc;
}
std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Ast::AST::getStageInput()
{
    auto& input = Parser::currentParser->stageInput;
    if (!input)
    {
        auto type = std::make_shared<StageType>();
        input = std::make_shared<Variate>();
        input->type = type;
        type->suffix = "_input";
        input->name = "input";
    }
    return input;
}
std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Ast::AST::getStageOutput()
{
    auto& input = Parser::currentParser->stageOutput;
    if (!input)
    {
        auto type = std::make_shared<StageType>();
        input = std::make_shared<Variate>();
        input->type = type;
        type->suffix = "_output";
        input->name = "output";
    }
    return input;
}
