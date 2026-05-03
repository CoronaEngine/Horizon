#include <Codegen/AST/AST.hpp>
#include <Codegen/AST/Parser.hpp>
#include <Codegen/Generator/SlangGenerator.hpp>
std::string EmbeddedShader::Generator::SlangGenerator::getShaderOutput(const Ast::EmbeddedShaderStructure& structure)
{
	currentStage = structure.stage;
	std::string output;

	if (Ast::Parser::getBindless())
	{
		output += // Vulkan Bindless Process - sets 0-2 match CabbageHardware bindless descriptors, set 3 reserved for UBO
		R"([vk::binding(0, 0)]
__DynamicResource<__DynamicResourceKind.General> combinedTextureSamplerHandles[];

[vk::binding(0, 1)]
__DynamicResource<__DynamicResourceKind.General> bufferHandles[];

[vk::binding(0, 2)]
__DynamicResource<__DynamicResourceKind.General> textureHandles[];

export T getDescriptorFromHandle<T>(DescriptorHandle<T> handle) where T : IOpaqueDescriptor
{
	__target_switch
	{
		case spirv:
		case glsl:
		if (T.kind == DescriptorKind.CombinedTextureSampler)
			return combinedTextureSamplerHandles[((uint2)handle).x].asOpaqueDescriptor<T>();
		else if (T.kind == DescriptorKind.Buffer)
			return bufferHandles[((uint2)handle).x].asOpaqueDescriptor<T>();
		else if (T.kind == DescriptorKind.Texture)
			return textureHandles[((uint2)handle).x].asOpaqueDescriptor<T>();
		else
			return defaultGetDescriptorFromHandle(handle);
		default:
		return defaultGetDescriptorFromHandle(handle);
	}
}
)";
	}

	for (auto& statement: structure.shaderOnlyStatements)
	{
		output += statement->parse() + '\n';
	}

	std::string mainContent;
	for (auto& statement: structure.localStatements)
	{
		mainContent += "\t" + statement->parse() + '\n';
	}

	std::string stageType = "unknown";
	switch (structure.stage)
	{
		case Ast::ShaderStage::Vertex:
			stageType = "vertex";
			break;
		case Ast::ShaderStage::Fragment:
			stageType = "fragment";
			break;
		case Ast::ShaderStage::Compute:
			stageType = "compute";
			break;
	}

	std::string outputStructName = "void";
	std::string inputStructName;

	if (!structure.inputStatements.empty())
	{
		inputStructName = stageType + "_input";
		std::string inputStruct = "struct " + inputStructName +" {\n";
		for (auto& statement: structure.inputStatements)
		{
			inputStruct += "\t" + statement->parse() + '\n';
		}
		inputStruct += "}\n";
		output += inputStruct;
	}

	if (!structure.outputStatements.empty())
	{
		outputStructName = stageType + "_output";
		std::string outputStruct = "struct " + outputStructName +" {\n";
		for (auto& statement: structure.outputStatements)
		{
			outputStruct += "\t" + statement->parse() + '\n';
		}
		outputStruct += "}\n";
		output += outputStruct;
	}

	std::string entrypoint = "[shader(\"" + stageType + "\")]\n";
	if (structure.stage == Ast::ShaderStage::Compute)
	{
		entrypoint += "[numthreads(" + std::to_string(numthreads.x) + "," + std::to_string(numthreads.y) + "," + std::to_string(numthreads.z) + ")]\n";
		numthreads = ktm::uvec3(1);
	}
	entrypoint += outputStructName + " main(";
	if (!structure.inputStatements.empty())
		entrypoint += inputStructName + " input";
	entrypoint += ") {\n";
	if (!structure.outputStatements.empty())
		entrypoint += "\t" + outputStructName + " output;\n";
	entrypoint += mainContent;
	if (!structure.outputStatements.empty())
		entrypoint += "\t""return output;\n";
	entrypoint += "}\n";

	output += entrypoint;

	return output;
}

std::string EmbeddedShader::Generator::SlangGenerator::getGlobalOutput(const Ast::EmbeddedShaderStructure& structure)
{
	std::string output;
	for (auto& statement: structure.globalStatements)
	{
		auto statementContent = statement->parse();
		if (!statementContent.empty())
			output += std::move(statementContent) + '\n';
	}

	// ① UBO — 始终生成为 ConstantBuffer，bindless 模式下显式绑定到 set=3
	if (!uboMembers.empty())
	{
		std::string uboStructName = "global_ubo_struct";
		output += "struct " + uboStructName + " {\n" + uboMembers + "}\n";
		if (bindless())
			output += "[[vk::binding(0, 3)]] ConstantBuffer<" + uboStructName + "> global_ubo;\n";
		else
			output += "ConstantBuffer<" + uboStructName + "> global_ubo;\n";
		uboMembers.clear();
	}

	// ② Push Constant — 仅包含用户显式 pushConstant 成员 + bindless handle 成员
	if (!pushConstantMembers.empty() || !bindlessHandleMembers.empty())
	{
		std::string pushConstantStructName = "global_push_constant_struct";
		auto pushConstantStruct = "struct " + pushConstantStructName + " {\n"
			+ pushConstantMembers + bindlessHandleMembers + "}\n";
		auto pushConstant = "[[vk::push_constant]] ConstantBuffer<" + pushConstantStructName + "> global_push_constant;\n";
		output += pushConstantStruct + pushConstant;
		pushConstantMembers.clear();
		bindlessHandleMembers.clear();
	}

	// ③ ParameterBlock — non-bindless 模式下的资源容器
	if (!parameterBlockMembers.empty())
	{
		std::string parameterBlockStructName = "parameter_block_struct";
		auto parameterBlockStruct = "struct " + parameterBlockStructName + " {\n" + parameterBlockMembers + "}\n";
		auto parameterBlock = "ParameterBlock<" + parameterBlockStructName + "> global_parameter_block;\n";
		output += parameterBlockStruct + parameterBlock;
		parameterBlockMembers.clear();
	}

	return output;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::DefineLocalVariate* node)
{
	return node->localVariate->type->parse() + " " + node->localVariate->name +
		(node->value ? " = " + node->value->parse() : "") + ";";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::DefineInputVariate* node)
{
	//这段内容应放在Generator生成的input struct中
	return node->variate->type->parse() + " " + node->variate->name + " : LOCATION" + std::to_string(node->variate->location) + ";";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::Assign* node)
{
	return node->leftValue->parse() + " = " + node->rightValue->parse() + ";";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::BinaryOperator* node)
{
	return "(" + node->value1->parse() + " " + node->operatorType + " " + node->value2->parse() + ")";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::MemberAccess* node)
{
	return node->value->parse() + "." + node->memberName;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::DefineOutputVariate* node)
{
	if (currentStage != Ast::ShaderStage::Fragment)
		return node->variate->type->parse() + " " + node->variate->name + " : LOCATION" + std::to_string(node->variate->location) + ";";
	return node->variate->type->parse() + " " + node->variate->name + " : SV_TARGET" + std::to_string(node->variate->location) + ";";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::IfStatement* node)
{
	auto result = "if (" + node->condition->parse() + ") {\n";
	nestHierarchy++;
	for (auto& statement: node->statements)
	{
		result += getCodeIndentation() + statement->parse() + "\n";
	}
	nestHierarchy--;
	result += getCodeIndentation() + "}";
	return result;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::ElifStatement* node)
{
	auto result = "else if (" + node->condition->parse() + ") {\n";
	nestHierarchy++;
	for (auto& statement: node->statements)
	{
		result += getCodeIndentation() + statement->parse() + "\n";
	}
	nestHierarchy--;
	result += getCodeIndentation() + "}";
	return result;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::ElseStatement* node)
{
	std::string result = "else {\n";
	nestHierarchy++;
	for (auto& statement: node->statements)
	{
		result += getCodeIndentation() + statement->parse() + "\n";
	}
	nestHierarchy--;
	result += getCodeIndentation() + "}";
	return result;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::InputVariate* node)
{
	return "input." + node->name;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::OutputVariate* node)
{
	return "output." + node->name;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::DefineUniversalArray* node)
{
	if (node->array->permissions == Ast::AccessPermissions::None)
		return "";

	auto result = node->array->type->parse() + " " + node->array->name + ";";
	if (node->array->permissions != Ast::AccessPermissions::ReadOnly)
		result = "RW" + result;

	if (bindless())
		bindlessHandleMembers += "\t" + result + "\n";
	else
		parameterBlockMembers += "\t" + result + "\n";
	return "";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::DefineUniformVariate* node)
{
	if (node->variate->permissions == Ast::AccessPermissions::None)
		return "";
	if (node->variate->pushConstant)
	{
		pushConstantMembers += "\t" + node->variate->type->parse() + " " + node->variate->name + ";\n";
		return "";
	}
	// bindless 模式下，资源类型 (Sampler) 走 push constant handle
	if (bindless() && std::dynamic_pointer_cast<Ast::SamplerType>(node->variate->type))
	{
		bindlessHandleMembers += "\t" + node->variate->type->parse() + " " + node->variate->name + ";\n";
		return "";
	}
	uboMembers += "\t" + node->variate->type->parse() + " " + node->variate->name + ";\n";
	return "";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::UniformVariate* node)
{
	if (node->pushConstant)
		return "global_push_constant." + node->name;
	// bindless 模式下 SamplerType 在 push constant 中
	if (bindless() && std::dynamic_pointer_cast<Ast::SamplerType>(node->type))
		return "global_push_constant." + node->name;
	return "global_ubo." + node->name;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::UniversalTexture2D* node)
{
	if (bindless())
		return "global_push_constant." + node->name;
	return "global_parameter_block." + node->name;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::UniversalArray* node)
{
	if (bindless())
		return "global_push_constant." + node->name;
	return "global_parameter_block." + node->name;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::DefineAggregateType* node)
{
	if (node->aggregate->permissions == Ast::AccessPermissions::None)
		return "";
	auto result = "struct " + node->aggregate->name + " {\n";
	if ((node->aggregate->permissions & Ast::AccessPermissions::WriteOnly) == Ast::AccessPermissions::None)
	{
		for (const auto& member: node->aggregate->members)
		{
			result += "\t" + member->type->parse() + " " + member->name + ";\n";
		}
	}
	else
	{
		for (const auto& member: node->aggregate->members)
		{
			if (std::dynamic_pointer_cast<Ast::ArrayType>(member->type) || std::dynamic_pointer_cast<Ast::Texture2DType>(member->type))
			{
				result += "\t" "RW" + member->type->parse() + " " + member->name + ";\n";
				continue;
			}
			result += "\t" + member->type->parse() + " " + member->name + ";\n";
		}
	}
	result += "}";
	return result;
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::DefineUniversalTexture2D *node)
{
    if (node->texture->permissions == Ast::AccessPermissions::None)
        return "";

	auto result = node->texture->type->parse() + " " + node->texture->name + ";";

    if (node->texture->permissions != Ast::AccessPermissions::ReadOnly)
    {
        result = "RW" + result;
    }

	if (bindless())
		bindlessHandleMembers += "\t" + result + "\n";
	else
		parameterBlockMembers += "\t" + result + "\n";
	return "";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::UnaryOperator* node)
{
	if (node->isPrefix)
		return "(" + node->operatorType + node->value->parse() + ")";

	return "(" + node->value->parse() + node->operatorType + ")";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::ArrayType* node)
{
	if ((node->permissions & Ast::AccessPermissions::WriteOnly) != Ast::AccessPermissions::None && std::dynamic_pointer_cast<Ast::Texture2DType>(node->elementType))
		return "StructuredBuffer<RW" + node->elementType->parse() + ">" + (bindless() ?  ".Handle" : "");
	return "StructuredBuffer<" + node->elementType->parse() + ">" + (bindless() ?  ".Handle" : "");
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::Texture2DType* node)
{
	return node->name + "<" + node->texelType->parse() + ">" + (bindless() ?  ".Handle" : "");
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::CallFunc* node)
{
	auto result = node->funcName + "(";
	if (node->args.empty())
		return result + ")";

	result += node->args[0]->parse();

	for (size_t i = 1; i < node->args.size(); ++i)
	{
		result += "," + node->args[i]->parse();
	}

	return result + ")";
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::SamplerType* node)
{
	return node->name + (bindless() ?  ".Handle" : "");
}

std::string EmbeddedShader::Generator::SlangGenerator::getParseOutput(const Ast::FunctionDeclaration* node)
{
	std::string result = "extern " + node->returnType + " " + node->funcName + "(";
	if (!node->args.empty())
	{
		result += node->args[0].first + " " + node->args[0].second;
		for (size_t i = 1; i < node->args.size(); ++i)
		{
			result += ", " + node->args[i].first + " " + node->args[i].second;
		}
	}
    result += ");";
	// result += ") {\n\t" "__intrinsic_asm \"";
 //    result += node->funcName;
 //    result += "\";\n}";
	return result;
}

std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Generator::SlangGenerator::getPositionOutput()
{
	auto positionOutput = std::make_shared<Ast::OutputVariate>();
	positionOutput->type = Ast::AST::createVecType<ktm::fvec4>();
	positionOutput->name = "position_output";
	positionOutput->location = 0;
	auto defineNode = std::make_shared<DefineSystemSemanticVariate>();
	defineNode->variate = positionOutput;
	defineNode->semanticName = "SV_POSITION";
	Ast::AST::addOutputStatement(defineNode);
	return positionOutput;
}

std::shared_ptr<EmbeddedShader::Ast::Variate> EmbeddedShader::Generator::SlangGenerator::getDispatchThreadIDInput()
{
	auto id = std::make_shared<Ast::InputVariate>();
	id->type = Ast::AST::createVecType<ktm::uvec3>();
	id->name = "dispatch_thread_id_input";
	id->location = 0;
	auto defineNode = std::make_shared<DefineSystemSemanticVariate>();
	defineNode->variate = id;
	defineNode->semanticName = "SV_DispatchThreadID";
	Ast::AST::addInputStatement(defineNode);
	return id;
}

bool EmbeddedShader::Generator::SlangGenerator::bindless()
{
	return Ast::Parser::getBindless();
}

std::string EmbeddedShader::Generator::SlangGenerator::getCodeIndentation()
{
	//ide可能会误报warning
	return std::string(nestHierarchy, '\t');
}

std::string EmbeddedShader::Generator::DefineSystemSemanticVariate::parse()
{
	return variate->type->parse() + " " + variate->name + " : " + semanticName + ";";
}
