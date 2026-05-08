#include "Node.hpp"

#include <filesystem>
#include <Codegen/Generator/SlangGenerator.hpp>

#include "Parser.hpp"

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

std::string EmbeddedShader::Ast::Variate::accessPath()
{
	return name;
}

std::string EmbeddedShader::Ast::BasicValue::parse()
{
	return value;
}

std::string EmbeddedShader::Ast::VecValue::parse()
{
	return value;
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

std::string EmbeddedShader::Ast::OutputVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::DefineOutputVariate::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::IfStatement::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::ElifStatement::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::ElseStatement::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
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

void EmbeddedShader::Ast::ElementVariate::access(AccessPermissions permissions)
{
	Value::access(permissions);
	array->access(permissions);
}

std::string EmbeddedShader::Ast::DefineUniversalArray::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::DefineUniversalArray::resetAccessPermissions()
{
	array->permissions = AccessPermissions::None;
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

void EmbeddedShader::Ast::UniversalTexture2D::access(AccessPermissions permissions)
{
	Value::access(permissions);
    this->permissions = this->permissions | permissions;
}

std::string EmbeddedShader::Ast::UniversalTexture2D::parse()
{
	return Generator::SlangGenerator::getParseOutput(this);
}

std::string EmbeddedShader::Ast::DefineUniversalTexture2D::parse()
{
    return Generator::SlangGenerator::getParseOutput(this);
}

void EmbeddedShader::Ast::DefineUniversalTexture2D::resetAccessPermissions()
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

std::string EmbeddedShader::Ast::Texture2DType::parse()
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
