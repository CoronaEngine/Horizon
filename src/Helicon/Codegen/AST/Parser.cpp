#include "Parser.hpp"
#include <utility>
#include <Codegen/Generator/SlangGenerator.hpp>

thread_local std::unique_ptr<EmbeddedShader::Ast::Parser> EmbeddedShader::Ast::Parser::currentParser = std::unique_ptr<Parser>(new Parser);

void EmbeddedShader::Ast::Parser::beginShaderParse(ShaderStage stage)
{
	endShaderParse();
	currentParser->structure.stage = stage;
	currentParser->localStatementStack.push(&currentParser->structure.localStatements);
	currentParser->isInShaderParse = true;
}

std::vector<EmbeddedShader::Ast::ParseOutput> EmbeddedShader::Ast::Parser::endPipelineParse()
{
    endShaderParse();

	auto globalOutput = Generator::SlangGenerator::getGlobalOutput(currentParser->structure);
	for (auto& output: currentParser->parseOutputs)
	{
	    if (!output.branches.empty())
	    {
	        output.typeHeader = globalOutput + output.typeHeader;
	        continue;
	    }
        output.output = globalOutput + output.output;
	}

	for (const auto& global: currentParser->structure.globalStatements)
	{
        global->resetAccessPermissions();
	}

    std::vector<ParseOutput> result;
	currentParser->parseOutputs.swap(result);
	return result;
}

void EmbeddedShader::Ast::Parser::setBindless(bool bindless)
{
	currentParser->bindless = bindless;
}

bool EmbeddedShader::Ast::Parser::getBindless()
{
	return currentParser->bindless;
}

void EmbeddedShader::Ast::Parser::endShaderParse()
{
    if (currentParser->isInShaderParse)
    {
        currentParser->parseOutputs.emplace_back(Generator::SlangGenerator::getShaderOutput(currentParser->structure), currentParser->structure.slangModuleSource,currentParser->structure.stage,currentParser->branchOutputs,currentParser->typeHeader);
        currentParser->reset();
        currentParser->localStatementStack.pop();
        currentParser->isInShaderParse = false;
    }
}

void EmbeddedShader::Ast::Parser::reset()
{
	structure.localStatements.clear();
	structure.inputStatements.clear();
	structure.outputStatements.clear();
	structure.shaderOnlyStatements.clear();
	currentVariateIndex = 0;
    currentBranchIndex = 0;
	nextRenderTargetLocation = 0;
	positionOutput.reset();
	dispatchThreadIDInput.reset();
    structure.slangModuleSource.clear();
    resetBranchOutputs();
    typeHeader.clear();
}

std::string EmbeddedShader::Ast::Parser::getUniqueVariateName()
{
	return "var_" + std::to_string(currentParser->currentVariateIndex++);
}

std::string EmbeddedShader::Ast::Parser::getUniqueAggregateTypeName()
{
	return "aggregate_type_" + std::to_string(currentParser->currentAggregateTypeIndex++);
}

std::string EmbeddedShader::Ast::Parser::getUniqueGlobalVariateName()
{
	return "global_var_" + std::to_string(currentParser->currentGlobalVariateIndex++);
}

const std::vector<std::shared_ptr<EmbeddedShader::Ast::Statement>>& EmbeddedShader::Ast::Parser::getGlobalStatements()
{
	return currentParser->structure.globalStatements;
}

size_t EmbeddedShader::Ast::Parser::getNextRenderTargetLocation()
{
	return currentParser->nextRenderTargetLocation++;
}

size_t EmbeddedShader::Ast::Parser::getCurrentBranchIndex()
{
    return currentParser->currentBranchIndex++;
}
bool EmbeddedShader::Ast::Parser::isCollectExternBranchVariate()
{
    return !currentParser->externBranchVar.empty();
}
void EmbeddedShader::Ast::Parser::beginCollectExternBranchVariate()
{
    currentParser->externBranchVar.push({});
}
void EmbeddedShader::Ast::Parser::endCollectExternBranchVariate(){
    currentParser->externBranchVar.pop();
}
void EmbeddedShader::Ast::Parser::pushBranchVariateReference(const Variate* var)
{
    if (Parser::isCollectExternBranchVariate())
    {
        currentParser->externBranchVar.top().variateRefs.insert(std::move(var));
    }
}
void EmbeddedShader::Ast::Parser::pushBranchLocalVariateDefinition(const DefineLocalVariate* definition)
{
    currentParser->externBranchVar.top().localDefinitions.push_back(std::move(definition));
}
EmbeddedShader::Ast::ExternBranchVariateCollection EmbeddedShader::Ast::Parser::getCurrentExternBranchVariateCollection(){
    std::set<const Variate*> nVars;
    auto collection = currentParser->externBranchVar.top();
    for (auto i = collection.variateRefs.begin(); i != collection.variateRefs.end();)
    {
        bool isLocal = false;
        for (auto definition : collection.localDefinitions)
        {
            if (definition->localVariate->name == (*i)->name)
            {
                //local var
                isLocal = true;
                break;
            }
        }
        if (isLocal)
        {
            i = collection.variateRefs.erase(i);
            continue;
        }
        ++i;
    }

    for (const auto & variate_ref : collection.variateRefs)
    {
         if (auto nVar = variate_ref->getRootVariate(); nVar)
         {
             nVars.insert(nVar);
         }
    }
    collection.variateRefs.swap(nVars);
    return collection;
}
void EmbeddedShader::Ast::Parser::resetBranchOutputs()
{
    currentParser->branchOutputs.clear();
}
std::vector<EmbeddedShader::Ast::BranchOutput>& EmbeddedShader::Ast::Parser::getBranchOutputs()
{
    return currentParser->branchOutputs;
}

std::string& EmbeddedShader::Ast::Parser::getTypeHeader()
{
    return currentParser->typeHeader;
}

std::stack<std::vector<size_t>>& EmbeddedShader::Ast::Parser::getBranchReferences()
{
    if (currentParser->branchReferences.empty())
    {
        currentParser->branchReferences.push({});
    }
    return currentParser->branchReferences;
}
