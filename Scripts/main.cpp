#include <filesystem>
#include <iostream>
#include <Helicon.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <set>
#include <Compiler/ShaderUtils.h>
#include <Compiler/ShaderTypeMapping.h>
using namespace EmbeddedShader;

void generateBinary(std::stringstream& out, std::string_view name, const std::vector<uint32_t>& shaderCode)
{
	out << "static inline std::vector<uint32_t> " << name <<" {";
	emitSpirVLiteral(out, shaderCode);
	out << "};\n";
}

void buildFunctionParameter(FunctionSignature& signature, std::stringstream& out)
{
	out << "(";
	if (!signature.parameters.empty())
	{
		auto& param0 = signature.parameters[0];
		out << emitCppParamType(param0.typeName) << " " << param0.name;
		for (size_t i = 1; i < signature.parameters.size(); ++i)
		{
			auto& param = signature.parameters[i];
			out << ", " << emitCppParamType(param.typeName) << " " << param.name;
		}
	}
	out << ")";
}

void buildFunctionSignature(FunctionSignature &signature, std::stringstream &out, std::string_view sourceSpv)
{
	out << "static inline ::EmbeddedShader::FunctionProxy<";
	if (signature.returnTypeName == "void")
	{
		out << "void";
		buildFunctionParameter(signature,out);
	}
	else
	{
		auto retCppType = typeNameToCpp(signature.returnTypeName);
		if (isOpaqueProxyType(retCppType))
		{
			// Opaque type: emit as full proxy type directly (e.g. Texture2DProxy<fvec4>)
			out << "::EmbeddedShader::" << retCppType.substr(1);
		}
		else
		{
			out << "::EmbeddedShader::VariateProxy<" << retCppType;
		}
		buildFunctionParameter(signature,out);
		if (!isOpaqueProxyType(retCppType))
			out << ">";
	}
	out << "> " << signature.name << "{";
	out << "\""<< signature.name << "\",\""<< typeNameToSlang(signature.returnTypeName) << "\",{";
	for (auto element: signature.parameters)
	{
	    out << "{\"" << typeNameToSlang(element.typeName) << "\",\"" << element.name << "\"},";
	}
	out<< "},&" << sourceSpv << "};";
}

void buildStruct(const StructInfo& structInfo, std::stringstream& out)
{
	out << "struct " << structInfo.name << "\n{\n";
	for (auto& member : structInfo.members)
	{
		out << "\t" << emitCppParamType(member.typeName) << " " << member.name << ";\n";
	}
	out << "};";
}

std::string sanitizeToIdentifier(const std::filesystem::path& p)
{
	auto s = p.filename().string();
	for (auto& c : s)
	{
		if (!std::isalnum(static_cast<unsigned char>(c)))
			c = '_';
	}
	if (!s.empty() && std::isdigit(static_cast<unsigned char>(s[0])))
		s = "_" + s;
	return s;
}

std::string extractMemberName(const std::string& qualifiedKey)
{
	auto pos = qualifiedKey.rfind('.');
	if (pos != std::string::npos)
		return qualifiedKey.substr(pos + 1);
	return qualifiedKey;
}

static bool isDirectResourceBindType(ShaderCodeModule::ShaderResources::BindType bindType)
{
	using BindType = ShaderCodeModule::ShaderResources::BindType;
	switch (bindType)
	{
		case BindType::sampledImages:
		case BindType::texture:
		case BindType::sampler:
		case BindType::rawBuffer:
		case BindType::storageTexture:
		case BindType::storageBuffer:
			return true;
		default:
			return false;
	}
}

std::set<std::string> generateBindingKeys(std::stringstream& out, const std::vector<uint32_t>& spirv, ShaderLanguage lang)
{
	auto resources = ShaderLanguageConverter::spirvCrossReflectedBindInfo(spirv, lang);
	std::set<std::string> bindingBlockNames;

	// Push constant block -> struct with static inline BindingKey members
	if (!resources.pushConstantName.empty())
	{
		bindingBlockNames.insert(resources.pushConstantName);
		out << "struct " << resources.pushConstantName << "\n{\n";
		for (auto& info : resources.bindInfoPool)
		{
			if (info.bindType == ShaderCodeModule::ShaderResources::pushConstantMembers)
				out << "\tstatic inline ::EmbeddedShader::BindingKey " << info.variateName
				    << "{" << info.byteOffset << ", " << info.typeSize
				    << ", " << static_cast<int32_t>(info.bindType) << ", " << info.location << "};\n";
		}
		out << "};\n";
	}

	// UBO block -> struct with static inline BindingKey members
	if (!resources.uniformBufferName.empty())
	{
		bindingBlockNames.insert(resources.uniformBufferName);
		out << "struct " << resources.uniformBufferName << "\n{\n";
		for (auto& info : resources.bindInfoPool)
		{
			if (info.bindType == ShaderCodeModule::ShaderResources::uniformBufferMembers)
				out << "\tstatic inline ::EmbeddedShader::BindingKey " << info.variateName
				    << "{" << info.byteOffset << ", " << info.typeSize
				    << ", " << static_cast<int32_t>(info.bindType) << ", " << info.location << "};\n";
		}
		out << "};\n";
	}

	// Stage inputs
	for (auto& info : resources.bindInfoPool)
	{
		if (info.bindType == ShaderCodeModule::ShaderResources::stageInputs)
			out << "static inline ::EmbeddedShader::BindingKey " << info.variateName
			    << "{" << info.byteOffset << ", " << info.typeSize
			    << ", " << static_cast<int32_t>(info.bindType) << ", " << info.location << "};\n";
	}

	// Stage outputs
	for (auto& info : resources.bindInfoPool)
	{
		if (info.bindType == ShaderCodeModule::ShaderResources::stageOutputs)
			out << "static inline ::EmbeddedShader::BindingKey " << info.variateName
			    << "{" << info.byteOffset << ", " << info.typeSize
			    << ", " << static_cast<int32_t>(info.bindType) << ", " << info.location << "};\n";
	}

	// Descriptor resources (sampled/storage/buffer/sampler) for direct key binding.
	for (auto& info : resources.bindInfoPool)
	{
		if (isDirectResourceBindType(info.bindType))
			out << "static inline ::EmbeddedShader::BindingKey " << info.variateName
			    << "{" << info.byteOffset << ", " << info.typeSize
			    << ", " << static_cast<int32_t>(info.bindType) << ", " << info.binding << "};\n";
	}

	return bindingBlockNames;
}

// Helper: emit a block proxy struct (push constants or UBO) into the output stream.
// Returns the initializer string for the parent constructor, or empty if block is absent.
static std::string emitBlockProxy(std::stringstream& out,
                                   const std::string& blockName,
                                   ShaderCodeModule::ShaderResources::BindType bindType,
                                   const ShaderCodeModule::ShaderResources& resources)
{
	if (blockName.empty()) return {};

	out << "struct " << blockName << "_t {\n";
	std::vector<std::string> fieldInits;
	for (auto& info : resources.bindInfoPool)
	{
		if (info.bindType == bindType)
		{
			out << "\t::EmbeddedShader::BoundField<P> " << info.variateName << ";\n";
			std::stringstream ss;
			ss << info.variateName << "(p, " << info.byteOffset << ", " << info.typeSize
			   << ", " << static_cast<int32_t>(info.bindType) << ", " << info.location << ")";
			fieldInits.push_back(ss.str());
		}
	}
	out << "\t" << blockName << "_t(P* p)";
	if (!fieldInits.empty())
	{
		out << " : ";
		for (size_t i = 0; i < fieldInits.size(); ++i)
		{
			if (i > 0) out << ", ";
			out << fieldInits[i];
		}
	}
	out << " {}\n";
	out << "} " << blockName << ";\n";
	return blockName + "(p)";
}

static void emitCtorBody(std::stringstream& out, const std::string& className,
                          const std::vector<std::string>& initList)
{
	if (initList.empty())
	{
		out << className << "(P* p) { (void)p; }\n";
	}
	else
	{
		out << className << "(P* p) : ";
		for (size_t i = 0; i < initList.size(); ++i)
		{
			if (i > 0) out << ", ";
			out << initList[i];
		}
		out << " {}\n";
	}
}

void generateBindings(std::stringstream& out, const std::vector<uint32_t>& spirv, ShaderLanguage lang)
{
	auto resources = ShaderLanguageConverter::spirvCrossReflectedBindInfo(spirv, lang);

	// Size metadata for cross-stage validation in TypedRasterizerPipeline
	out << "static constexpr size_t pushConstantBlockSize = " << resources.pushConstantSize << ";\n";
	out << "static constexpr size_t uniformBufferBlockSize = " << resources.uniformBufferSize << ";\n";

	// --- ResourceBindings<P>: push constants + UBO (shared between VS and FS) ---
	out << "template<typename P>\nstruct ResourceBindings {\n";
	{
		std::vector<std::string> initList;
		auto s = emitBlockProxy(out, resources.pushConstantName,
		                         ShaderCodeModule::ShaderResources::pushConstantMembers, resources);
		if (!s.empty()) initList.push_back(std::move(s));

		s = emitBlockProxy(out, resources.uniformBufferName,
		                    ShaderCodeModule::ShaderResources::uniformBufferMembers, resources);
		if (!s.empty()) initList.push_back(std::move(s));

		for (auto& info : resources.bindInfoPool)
		{
			if (!isDirectResourceBindType(info.bindType))
				continue;

			out << "::EmbeddedShader::BoundField<P> " << info.variateName << ";\n";
			std::stringstream ss;
			ss << info.variateName << "(p, " << info.byteOffset << ", " << info.typeSize
			   << ", " << static_cast<int32_t>(info.bindType) << ", " << info.binding << ")";
			initList.push_back(ss.str());
		}

		emitCtorBody(out, "ResourceBindings", initList);
	}
	out << "};\n";

	// --- OutputBindings<P>: stage outputs (render targets) ---
	out << "template<typename P>\nstruct OutputBindings {\n";
	{
		std::vector<std::string> initList;
		for (auto& info : resources.bindInfoPool)
		{
			if (info.bindType == ShaderCodeModule::ShaderResources::stageOutputs)
			{
				out << "::EmbeddedShader::BoundField<P> " << info.variateName << ";\n";
				std::stringstream ss;
				ss << info.variateName << "(p, " << info.byteOffset << ", " << info.typeSize
				   << ", " << static_cast<int32_t>(info.bindType) << ", " << info.location << ")";
				initList.push_back(ss.str());
			}
		}
		emitCtorBody(out, "OutputBindings", initList);
	}
	out << "};\n";

	// --- Bindings<P>: combined (for single-stage pipelines like compute) ---
	out << "template<typename P>\nstruct Bindings : ResourceBindings<P>, OutputBindings<P> {\n";
	out << "Bindings(P* p) : ResourceBindings<P>(p), OutputBindings<P>(p) {}\n";
	out << "};\n";
}

int main(int argc, char** argv)
{
	std::cout << "Helicon Shader Compile Scripts\n";
	if (argc < 2)
	{
		std::cout << "NOTE:No input\n";
		return 1;
	}
	std::filesystem::path path = "";
	std::filesystem::path outPath = "";
	std::string ext  = ".h";
	ShaderLanguage inputLanguage = ShaderLanguage::GLSL;
	ShaderStage inputStage = ShaderStage::VertexShader;
	bool stageExplicit = false;

	//main 参数解析
	for (int i = 1; i < argc;)
	{
		if (std::string arg = argv[i]; arg == "-s")
		{
			++i;
			path = argv[i];
		}
		else if (arg == "-o")
		{
			++i;
			outPath = argv[i];
		}
		else if (arg == "-l")
		{
			++i;
			arg = argv[i];
			if (arg == "glsl")
				inputLanguage = ShaderLanguage::GLSL;
			else if (arg == "hlsl")
				inputLanguage = ShaderLanguage::HLSL;
			else
			{
				std::cout << "ERROR:Unrecognized Shader Language.\n";
				return 1;
			}
		}
		else if (arg == "-t")
		{
			++i;
			arg = argv[i];
			if (arg == "vert" || arg == "vertex")
			{
				inputStage = ShaderStage::VertexShader;
				stageExplicit = true;
			}
			else if (arg == "frag" || arg == "fragment")
			{
				inputStage = ShaderStage::FragmentShader;
				stageExplicit = true;
			}
			else if (arg == "comp" || arg == "compute")
			{
				inputStage = ShaderStage::ComputeShader;
				stageExplicit = true;
			}
			else
			{
				std::cout << "ERROR:Unrecognized Shader Stage.\n";
				return 1;
			}
		}
		else if (arg == "-output-file-extension")
		{
			++i;
			ext = argv[i];
		}
		else
		{
			std::cout << "ERROR:Unrecognized Parameter.\n";
			return 1;
		}
		++i;
	}

	if (path.empty())
	{
		std::cout << "ERROR:Cannot enter an empty source file path.\n";
		return 1;
	}

	std::fstream file(path,std::ios::in);
	if (!file.is_open())
	{
		std::cout << "ERROR:Cannot open the source file.\n";
		return 1;
	}

	if (outPath.empty())
	{
		std::cout << "NOTE:No output. Use the source file directory as the default output directory.\n";
		outPath = path.parent_path();
	}

	if (ext.empty())
	{
		std::cout << "NOTE:Invalid file extension. Use the default file extension (.h).\n";
		ext = ".h";
	}
	ext = ext[0] == '.' ? ext : "." + ext;

	std::string code = (std::stringstream{} << file.rdbuf()).str();
	file.close();

	// 如果未显式指定 stage，则根据文件名后缀或内容推断
	if (!stageExplicit)
	{
		auto stem = path.stem().string();
		auto extension = path.extension().string();
		// 检查常见命名: xxx.vert, xxx.frag, xxx.comp, xxx.vert.glsl 等
		if (extension == ".frag" || extension == ".fs" || stem.ends_with(".frag") ||
		    stem.find("frag") != std::string::npos)
			inputStage = ShaderStage::FragmentShader;
		else if (extension == ".comp" || extension == ".cs" || stem.ends_with(".comp") ||
		         stem.find("compute") != std::string::npos)
			inputStage = ShaderStage::ComputeShader;
		else if (extension == ".vert" || extension == ".vs" || stem.ends_with(".vert"))
			inputStage = ShaderStage::VertexShader;
		else
		{
			// 扫描代码中的 gl_FragCoord / gl_Position / gl_GlobalInvocationID 等内置变量
			if (code.find("gl_FragCoord") != std::string::npos ||
			    code.find("gl_SampleID") != std::string::npos ||
			    (code.find("out vec4") != std::string::npos && code.find("gl_Position") == std::string::npos))
				inputStage = ShaderStage::FragmentShader;
			else if (code.find("gl_GlobalInvocationID") != std::string::npos ||
			         code.find("gl_WorkGroupID") != std::string::npos ||
			         code.find("gl_LocalInvocationID") != std::string::npos)
				inputStage = ShaderStage::ComputeShader;
			// else default remains VertexShader
		}
		std::cout << "INFO:Auto-detected shader stage: " << static_cast<int>(inputStage) << "\n";
	}

	auto spirv = ShaderLanguageConverter::glslangSpirvCompiler(code,inputLanguage,inputStage,{ path.parent_path() }, false);
	if (spirv.empty())
	{
		std::cout << "ERROR:Cannot compile SPIR-V.\n";
		return 1;
	}
	std::cout << "SUCCESS:SPIR-V compiled.\n";

	auto irReflections = ShaderLanguageConverter::spirvCrossGetIRReflection(spirv);
	std::cout << "SUCCESS:Obtain reflection information from SPIR-V IR through SPIRV-CROSS.\n";

	// for (auto& irReflection: irReflections)
	// {
	// 	if (irReflection.type == IRReflection::Type::FunctionSignature)
	// 	{
	// 		auto& signature = std::get<FunctionSignature>(irReflection.info);
	// 		std::cout << "Function:" << signature.name.substr(0, signature.name.find('(')) << "\n";
	// 		std::cout << "Return Type:" << signature.returnTypeName << "\n";
	// 		std::cout << "Parameter List:\n";
	// 		for (auto& parameter: signature.parameters)
	// 		{
	// 			std::cout << "\t""Name:" << parameter.name << "\n";
	// 			std::cout << "\t""Type:" << parameter.typeName << "\n\n";
	// 		}
	// 	}
	//
	// 	if (irReflection.type == IRReflection::Type::Struct)
	// 	{
	// 		auto& structInfo = std::get<StructInfo>(irReflection.info);
	// 		std::cout << "Struct: " << structInfo.name << "\n";
	// 		std::cout << "Members:\n";
	// 		for (auto& member: structInfo.members)
	// 		{
	// 			std::cout << "\t""Name:" << member.name << "\n";
	// 			std::cout << "\t""Type:" << member.typeName << "\n\n";
	// 		}
	// 	}
	// }

	std::cout << "INFO:Generate the final C++ shader...\n";
	std::stringstream out;
	out << "#pragma once\n#include <Codegen/VariateProxy.h>\n";

	auto fileName = path.string();
	std::ranges::replace(fileName, '\\', '_');
	std::ranges::replace(fileName, '/', '_');
	std::ranges::replace(fileName, '.', '_');
	std::ranges::replace(fileName, ':', '_');

	// Wrap everything in a per-shader struct to avoid redefinition across .hpp files
	auto nsName = sanitizeToIdentifier(path);
	out << "struct " << nsName << " {\n";

	generateBinary(out, fileName, spirv);

	// 稳定别名：用户可通过 vert_glsl::spirv 引用预编译 SPIR-V 二进制
	out << "static inline auto& spirv = " << fileName << ";\n";

	// Generate BindingKey struct declarations first, collect binding block names
	std::cout << "INFO:Generate binding keys for struct '" << nsName << "'...\n";
	auto bindingBlockNames = generateBindingKeys(out, spirv, inputLanguage);

	// Generate IR reflection (skip structs that overlap with BindingKey structs)
	for (auto& irReflection : irReflections)
	{
		if (irReflection.type == IRReflection::Type::FunctionSignature)
		{
			auto& signature = std::get<FunctionSignature>(irReflection.info);
			if (signature.isEntryPoint) continue;
			buildFunctionSignature(signature, out, fileName);
			out << "\n";
		}

		if (irReflection.type == IRReflection::Type::Struct)
		{
			auto& structInfo = std::get<StructInfo>(irReflection.info);
			// Skip structs already generated as BindingKey structs
			if (bindingBlockNames.contains(structInfo.name)) continue;
			buildStruct(structInfo,out);
			out << "\n";
		}
	}

	// Generate Bindings<P> template for direct member access (TypedRasterizerPipeline / TypedComputePipeline)
	std::cout << "INFO:Generate Bindings<P> for struct '" << nsName << "'...\n";
	generateBindings(out, spirv, inputLanguage);

	out << "}; // struct " << nsName << "\n";

	//std::cout << out.str();

	auto stem = path.stem();
	auto outFilePath = outPath / (stem.string() + ext);
	file.open(outFilePath,std::ios::out);
	file << out.str();
	file.close();

	std::cout << "SUCCESS:The C++ shader has been output to the following directory:\n\t" << outFilePath.string();

	return 0;
}
