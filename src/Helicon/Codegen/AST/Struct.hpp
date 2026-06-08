#pragma once
#include <Codegen/AST/Enum.hpp>
#include <memory>
#include <unordered_set>
#include <vector>

namespace EmbeddedShader
{
struct SlangModule;
}

namespace EmbeddedShader::Ast
{
	struct Statement;

	struct EmbeddedShaderStructure
	{
		ShaderStage stage;
		std::vector<std::shared_ptr<Statement>> globalStatements;
		std::vector<std::shared_ptr<Statement>> inputStatements;
		std::vector<std::shared_ptr<Statement>> outputStatements;
		std::vector<std::shared_ptr<Statement>> localStatements;
		std::vector<std::shared_ptr<Statement>> shaderOnlyStatements;
	    std::unordered_set<SlangModule*> slangModuleSource;
	};
}
