#include "Node.hpp"

#include "AST.hpp"

#include <filesystem>
#include <Codegen/Generator/SlangGenerator.hpp>

#include "Parser.hpp"

std::string EmbeddedShader::Ast::Node::generate()
{
    collectVariateReference();
    return parse();
}

std::string EmbeddedShader::Ast::Node::parse()
{
	return "";
}

const std::vector<std::shared_ptr<EmbeddedShader::Ast::Value>>& EmbeddedShader::Ast::Node::accessAll(const std::vector<std::shared_ptr<Value>>& values, AccessPermissions permissions)
{
	for (const auto& value: values)
	{
		value->access(permissions);
	}
	return values;
}

void EmbeddedShader::Ast::Type::access(AccessPermissions permissions)
{

}

std::string EmbeddedShader::Ast::NameType::parse()
{
	return name;
}

std::string EmbeddedShader::Ast::PushConstantType::parse()
{
    return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::StageType::parse()
{
    return Generator::SlangGenerator::getParseOutput(this);
}
void EmbeddedShader::Ast::Value::access(AccessPermissions permissions)
{
	type->access(permissions);
}

std::string EmbeddedShader::Ast::Value::accessPath()
{
	return "";
}

std::string EmbeddedShader::Ast::Variate::parse()
{

	return name;
}
void EmbeddedShader::Ast::Variate::collectVariateReference()
{
    Value::collectVariateReference();
    Parser::pushBranchVariateReference(this);
}

std::string EmbeddedShader::Ast::Variate::accessPath()
{
	return name;
}
const EmbeddedShader::Ast::Variate *EmbeddedShader::Ast::Variate::getRootVariate()const
{
    return this;
}

void EmbeddedShader::Ast::LocalVariate::access(AccessPermissions permissions)
{
    Value::access(permissions);
    this->permissions = this->permissions | permissions;
}

EmbeddedShader::Ast::AccessPermissions EmbeddedShader::Ast::LocalVariate::getAccessPermissions() const
{
    return permissions;
}

std::string EmbeddedShader::Ast::BasicValue::parse()
{
	return value;
}

std::string EmbeddedShader::Ast::VecValue::parse()
{
    return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::DefineLocalVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::BinaryOperator::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::Assign::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::InputVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}
const EmbeddedShader::Ast::Variate* EmbeddedShader::Ast::InputVariate::getRootVariate() const
{
    return AST::getStageInput().get();
}

std::string EmbeddedShader::Ast::DefineInputVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::MemberAccess::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::MemberAccess::access(AccessPermissions permissions)
{
	Value::access(permissions);
	value->access(permissions);
}

std::string EmbeddedShader::Ast::MemberAccess::accessPath()
{
	return value->accessPath() + "." + memberName;
}
const EmbeddedShader::Ast::Variate *EmbeddedShader::Ast::MemberAccess::getRootVariate()const{
    return value->getRootVariate();
}

std::string EmbeddedShader::Ast::OutputVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}
const EmbeddedShader::Ast::Variate* EmbeddedShader::Ast::OutputVariate::getRootVariate() const
{
    return AST::getStageOutput().get();
}

std::string EmbeddedShader::Ast::DefineOutputVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::IfStatement::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::ElseStatement::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::StringStatement::parse()
{
    return content + ";";
}

EmbeddedShader::Ast::AccessPermissions EmbeddedShader::Ast::UniversalArray::getAccessPermissions() const
{
    return permissions;
}

void EmbeddedShader::Ast::UniversalArray::access(AccessPermissions permissions)
{
	Value::access(permissions);
	this->permissions = this->permissions | permissions;
}

std::string EmbeddedShader::Ast::UniversalArray::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}
const EmbeddedShader::Ast::Variate* EmbeddedShader::Ast::UniversalArray::getRootVariate() const
{
    if (Parser::getBindless())
        return AST::getGlobalPushConstant().get();
    return AST::getGlobalParameterBlock().get();
}

void EmbeddedShader::Ast::ElementValue::access(AccessPermissions permissions)
{
	Value::access(permissions);
	array->access(permissions);
}
const EmbeddedShader::Ast::Variate *EmbeddedShader::Ast::ElementValue::getRootVariate()const{
    return array->getRootVariate();
}
std::string EmbeddedShader::Ast::ElementValue::parse(){
    return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::DefineUniversalArray::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::DefineUniversalArray::resetAccessPermissions()
{
	array->permissions = AccessPermissions::None;
}

EmbeddedShader::Ast::AccessPermissions EmbeddedShader::Ast::UniformVariate::getAccessPermissions() const
{
    return permissions;
}

std::string EmbeddedShader::Ast::UniformVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::UniformVariate::access(AccessPermissions permissions)
{
	Value::access(permissions);
	this->permissions = this->permissions | permissions;
}
const EmbeddedShader::Ast::Variate* EmbeddedShader::Ast::UniformVariate::getRootVariate() const
{
    if (pushConstant)
        return AST::getGlobalPushConstant().get();
    // bindless 模式下 SamplerType 在 push constant 中
    if (Parser::getBindless() && std::dynamic_pointer_cast<Ast::SamplerType>(type))
        return AST::getGlobalPushConstant().get();
    return AST::getGlobalUBO().get();
}

std::string EmbeddedShader::Ast::DefineUniformVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::DefineUniformVariate::resetAccessPermissions()
{
	variate->permissions = AccessPermissions::None;
}

std::string EmbeddedShader::Ast::DefineAggregateType::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::DefineAggregateType::resetAccessPermissions()
{
	aggregate->permissions = AccessPermissions::None;
}

std::string EmbeddedShader::Ast::AggregateValue::parse()
{
	return value;
}

EmbeddedShader::Ast::AccessPermissions EmbeddedShader::Ast::UniversalTexture::getAccessPermissions() const
{
    return permissions;
}

void EmbeddedShader::Ast::UniversalTexture::access(AccessPermissions permissions)
{
	Value::access(permissions);
    this->permissions = this->permissions | permissions;
}

std::string EmbeddedShader::Ast::UniversalTexture::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

const EmbeddedShader::Ast::Variate* EmbeddedShader::Ast::UniversalTexture::getRootVariate() const
{
    if (Parser::getBindless())
        return AST::getGlobalPushConstant().get();
    return AST::getGlobalParameterBlock().get();
}
std::string EmbeddedShader::Ast::DefineUniversalTexture::parse()
{
    return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::DefineUniversalTexture::resetAccessPermissions()
{
    texture->permissions = AccessPermissions::None;
}

std::string EmbeddedShader::Ast::MatValue::parse()
{
	return value;
}

std::string EmbeddedShader::Ast::UnaryOperator::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::UniversalStatement::parse()
{
	return node->parse() + ";";
}

std::string EmbeddedShader::Ast::ArrayType::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::ArrayType::access(AccessPermissions permissions)
{
	Type::access(permissions);
	this->permissions = this->permissions | permissions;
}

std::string EmbeddedShader::Ast::TextureType::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::CallFunc::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::SamplerType::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::FunctionDeclaration::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::AggregateType::access(AccessPermissions permissions)
{
	NameType::access(permissions);
	this->permissions = this->permissions | permissions;
}
