#include <filesystem>
#include <iostream>
#include <Helicon.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <regex>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <Compiler/ShaderUtils.h>
#include <Compiler/ShaderTypeMapping.h>
#include "Compiler/ShaderCodeCompiler.h"
using namespace EmbeddedShader;

void generateBinary(std::stringstream& out, std::string_view name, const std::vector<uint32_t>& shaderCode)
{
	out << "static inline std::vector<uint32_t> " << name <<" {";
	emitSpirVLiteral(out, shaderCode);
	out << "};\n";
}

void generateBinary(std::stringstream& out, std::string_view name, const SlangModule& slangModule)
{
    out << "static inline ::EmbeddedShader::SlangModule " << name << " {{";
    emitCppStringLiteral(out, slangModule.name);
    out << "}, {";
    emitCppStringLiteral(out, slangModule.path);
    out << "}, {";
    emitSlangModuleLiteral(out, slangModule.binData);
    out << "}};\n";
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
	return sanitizeFilename(p);
}

bool hasShaderEntryPoint(const std::string& code)
{
    static const std::regex mainPattern(R"(\bmain\s*\()");
    return std::regex_search(code, mainPattern) || code.find("[shader") != std::string::npos;
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

static bool isPushConstantBlockRecord(const ShaderCodeModule::ShaderResources::ShaderBindInfo& info,
                                      const ShaderCodeModule::ShaderResources& resources)
{
	return info.bindType == ShaderCodeModule::ShaderResources::pushConstantMembers &&
	       info.variateName == resources.pushConstantName &&
	       info.byteOffset == 0 &&
	       info.typeSize == resources.pushConstantSize;
}

std::set<std::string> generateBindingKeys(std::stringstream& out, const ShaderCodeModule::ShaderResources& resources)
{
	std::set<std::string> bindingBlockNames;

	// Push constant block -> struct with static inline BindingKey members
	if (!resources.pushConstantName.empty())
	{
		bindingBlockNames.insert(resources.pushConstantName);
		out << "struct " << resources.pushConstantName << "\n{\n";
		for (auto& info : resources.bindInfoPool)
		{
			if (info.bindType == ShaderCodeModule::ShaderResources::pushConstantMembers &&
			    !isPushConstantBlockRecord(info, resources))
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
				    << ", " << static_cast<int32_t>(info.bindType) << ", " << info.location
				    << ", " << info.set << ", " << info.binding << "};\n";
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
			    << ", " << static_cast<int32_t>(info.bindType) << ", " << info.binding
			    << ", " << info.set << ", " << info.binding << "};\n";
	}

	return bindingBlockNames;
}

using ShaderResources = ShaderCodeModule::ShaderResources;

// A stage binding group receives p in its constructor. A raster specialization
// binds each direct member to its own pipeline, including after a move.
static void emitBoundField(std::ostream& out, const ShaderResources::ShaderBindInfo& field,
                           std::string_view stateType, std::vector<std::string>* ctorInits,
                           std::string_view indent = "    ")
{
    std::ostringstream args;
    const bool descriptor = isDirectResourceBindType(field.bindType);
    args << ", " << field.byteOffset << ", " << field.typeSize << ", " << static_cast<int32_t>(field.bindType)
         << ", " << (descriptor ? field.binding : field.location);
    if (field.bindType != ShaderResources::stageOutputs)
        args << ", " << field.set << ", " << field.binding;

    out << indent << "::EmbeddedShader::BoundField<" << stateType << "> " << field.variateName;
    if (ctorInits)
        ctorInits->push_back(field.variateName + "(p" + args.str() + ")");
    else
        out << "{&pipeline_" << args.str() << "}";
    out << ";\n";
}

static void emitBindingConstructor(std::ostream& out, const std::string& name, std::string_view stateType,
                                   const std::vector<std::string>& inits, std::string_view indent = "    ")
{
    out << indent << name << '(' << stateType << (inits.empty() ? "*)" : "* p)");
    for (size_t i = 0; i < inits.size(); ++i)
        out << (i == 0 ? " : " : ", ") << inits[i];
    out << " {}\n";
}

static void emitResourceFields(std::ostream& out, const ShaderResources& resources,
                               std::string_view stateType, std::vector<std::string>* ctorInits)
{
    auto emitBlock = [&](const std::string& name, ShaderResources::BindType kind) {
        if (name.empty()) return;
        out << "    struct " << name << "_t {\n";
        std::vector<std::string> blockInits;
        for (const auto& field : resources.bindInfoPool)
            if (field.bindType == kind && !isPushConstantBlockRecord(field, resources))
                emitBoundField(out, field, stateType, &blockInits, "        ");
        emitBindingConstructor(out, name + "_t", stateType, blockInits, "        ");
        out << "    } " << name;
        if (ctorInits)
            ctorInits->push_back(name + "(p)");
        else
            out << "{&pipeline_}";
        out << ";\n";
    };
    emitBlock(resources.pushConstantName, ShaderResources::pushConstantMembers);
    emitBlock(resources.uniformBufferName, ShaderResources::uniformBufferMembers);
    for (const auto& field : resources.bindInfoPool)
        if (isDirectResourceBindType(field.bindType))
            emitBoundField(out, field, stateType, ctorInits);
}

static void emitOutputFields(std::ostream& out, const ShaderResources& resources,
                             std::string_view stateType, std::vector<std::string>* ctorInits)
{
    for (const auto& field : resources.bindInfoPool)
        if (field.bindType == ShaderResources::stageOutputs)
            emitBoundField(out, field, stateType, ctorInits);
}

static void generateComputeBindings(std::ostream& out, const ShaderResources& resources)
{
    out << "template<typename P>\nstruct ResourceBindings {\n";
    std::vector<std::string> inits;
    emitResourceFields(out, resources, "P", &inits);
    emitBindingConstructor(out, "ResourceBindings", "P", inits);
    out << "};\ntemplate<typename P>\nstruct OutputBindings {\n";
    inits.clear();
    emitOutputFields(out, resources, "P", &inits);
    emitBindingConstructor(out, "OutputBindings", "P", inits);
    out << "};\ntemplate<typename P>\nstruct Bindings : ResourceBindings<P>, OutputBindings<P> {\n"
        << "Bindings(P* p) : ResourceBindings<P>(p), OutputBindings<P>(p) {}\n};\n";
}

std::string procType(slang::TypeReflection* reflection);

std::string procVecType(slang::TypeReflection* reflection)
{
    return procType(reflection->getElementType()) + std::to_string(reflection->getElementCount());
}

std::string procMatType(slang::TypeReflection* reflection)
{
    return procType(reflection->getElementType()) + std::to_string(reflection->getColumnCount()) + "x" + std::to_string(reflection->getRowCount());
}

std::string procScalarType(slang::TypeReflection* reflection)
{
    switch (reflection->getScalarType())
    {
    case slang::TypeReflection::None:
        break;
    case slang::TypeReflection::Void:
        return "void";
    case slang::TypeReflection::Bool:
        return "bool";
    case slang::TypeReflection::Int32:
        return "int";
    case slang::TypeReflection::UInt32:
        return "uint";
    case slang::TypeReflection::Int64:
        return "int64_t";
    case slang::TypeReflection::UInt64:
        return "uint64_t";
    case slang::TypeReflection::Float16:
        return "half";
    case slang::TypeReflection::Float32:
        return "float";
    case slang::TypeReflection::Float64:
        return "double";
    case slang::TypeReflection::Int8:
        return "int8_t";
    case slang::TypeReflection::UInt8:
        return "uint8_t";
    case slang::TypeReflection::Int16:
        return "int16_t";
    case slang::TypeReflection::UInt16:
        return "uint16_t";
    case slang::TypeReflection::IntPtr:
        return "intptr_t";
    case slang::TypeReflection::UIntPtr:
        return "uintptr_t";
    case slang::TypeReflection::BFloat16:
        return "half";
    case slang::TypeReflection::FloatE4M3:
        return "float";
    case slang::TypeReflection::FloatE5M2:
        return "float";
    }
    return "unknown";
}

std::string procType(slang::TypeReflection* reflection)
{
    switch (reflection->getKind())
    {
    case slang::TypeReflection::Kind::None:
        break;
    case slang::TypeReflection::Kind::Struct:
        return reflection->getName();
    case slang::TypeReflection::Kind::Array:
        return "Array<" + procType(reflection->getElementType()) + ">";
    case slang::TypeReflection::Kind::Matrix:
        return procMatType(reflection);
    case slang::TypeReflection::Kind::Vector:
        return procVecType(reflection);
    case slang::TypeReflection::Kind::Scalar:
        return procScalarType(reflection);
    case slang::TypeReflection::Kind::ConstantBuffer:
        break;
    case slang::TypeReflection::Kind::Resource:
        break;
    case slang::TypeReflection::Kind::SamplerState:
        break;
    case slang::TypeReflection::Kind::TextureBuffer:
        break;
    case slang::TypeReflection::Kind::ShaderStorageBuffer:
        break;
    case slang::TypeReflection::Kind::ParameterBlock:
        break;
    case slang::TypeReflection::Kind::GenericTypeParameter:
        break;
    case slang::TypeReflection::Kind::Interface:
        break;
    case slang::TypeReflection::Kind::OutputStream:
        break;
    case slang::TypeReflection::Kind::Specialized:
        break;
    case slang::TypeReflection::Kind::Feedback:
        break;
    case slang::TypeReflection::Kind::Pointer:
        break;
    case slang::TypeReflection::Kind::DynamicResource:
        break;
    case slang::TypeReflection::Kind::MeshOutput:
        break;
    case slang::TypeReflection::Kind::Enum:
        break;
    }
    if (auto name = reflection->getName(); name)
    {
        return name;
    }
    return "";
}

std::string procVarDefinition(slang::VariableReflection* reflection)
{
    return EmbeddedShader::typeNameToCpp(procType(reflection->getType())) + " " + reflection->getName();
}

void procParam(std::stringstream& ss, slang::FunctionReflection* reflection)
{
    ss << "(";
    if (reflection->getParameterCount() != 0)
    {
        auto param0 = reflection->getParameterByIndex(0);
        ss << emitCppParamType(procType(param0->getType())) << " " << param0->getName();
        for (size_t i = 1; i < reflection->getParameterCount(); ++i)
        {
            auto param = reflection->getParameterByIndex(i);
            ss << ", " << emitCppParamType(procType(param->getType())) << " " << param->getName();
        }
    }
    ss << ")";
}

// Device pointers are valid in shaders, but have no C++ EDSL proxy type.
static bool containsDevicePointer(slang::TypeReflection* type)
{
    using Kind = slang::TypeReflection::Kind;
    if (type->getKind() == Kind::Pointer) return true;
    if (type->getKind() == Kind::Array) return containsDevicePointer(type->getElementType());
    if (type->getKind() == Kind::Struct) {
        for (unsigned i = 0; i < type->getFieldCount(); ++i)
            if (containsDevicePointer(type->getFieldByIndex(i)->getType())) return true;
    }
    return false;
}

void procFunc(std::stringstream& ss, slang::FunctionReflection* reflection,std::string sourceModule)
{
    if (containsDevicePointer(reflection->getReturnType())) return;
    for (unsigned i = 0; i < reflection->getParameterCount(); ++i)
        if (containsDevicePointer(reflection->getParameterByIndex(i)->getType())) return;
    ss << "static inline ::EmbeddedShader::FunctionProxy<";
    auto retType = procType(reflection->getReturnType());
    if (retType == "void")
    {
        ss << "void";
        procParam(ss,reflection);
    }
    else
    {
        auto retCppType = typeNameToCpp(retType);
        if (isOpaqueProxyType(retCppType))
        {
            // Opaque type: emit as full proxy type directly (e.g. Texture2DProxy<fvec4>)
            ss << "::EmbeddedShader::" << retCppType.substr(1);
        }
        else
        {
            ss << "::EmbeddedShader::VariateProxy<" << retCppType;
        }
        procParam(ss,reflection);
        if (!isOpaqueProxyType(retCppType))
            ss << ">";
    }

    ss << "> " << reflection->getName() << "{";
    ss << "\""<< reflection->getName() << "\",\""<< typeNameToSlang(retType) << "\",{";
    for (int i = 0; i < reflection->getParameterCount(); ++i)
    {
        auto child = reflection->getParameterByIndex(i);
        ss << "{\"" << typeNameToSlang(procType(child->getType())) << "\",\"" << child->getName() << "\"},";
    }
    ss << "},&" << sourceModule << "};";
}

void procStruct(std::stringstream& ss, slang::DeclReflection* reflection)
{
    if (containsDevicePointer(reflection->getType())) return;
    ss << "struct " << reflection->getName() << "\n{\n";
    for (auto member : reflection->getChildren())
    {
        if (member->getKind() == slang::DeclReflection::Kind::Variable)
        {
            auto var = member->asVariable();
            ss << "\t" << emitCppParamType(procType(var->getType())) << " " << var->getName() << ";\n";
        }
    }
    ss << "};";
}

void procStructInstance(std::stringstream& ss, slang::DeclReflection* reflection)
{
    auto var = reflection->asVariable();
    auto type = var->getType();
    auto kind = type->getKind();

    if (kind != slang::TypeReflection::Kind::ConstantBuffer &&
        kind != slang::TypeReflection::Kind::ParameterBlock)
    {
        return;
    }
    auto elementType = type->getElementType();
    if (containsDevicePointer(elementType)) return;
    ss << "struct " << reflection->getName() << "\n{\n";
    for (int i = 0; i < elementType->getFieldCount(); ++i)
    {
        auto member = elementType->getFieldByIndex(i);
        ss << "\t" << emitCppParamType(procType(member->getType())) << " " << member->getName() << ";\n";
    }
    ss << "};\n";
}

static ShaderResources reflectBindings(const SlangModule& module, ShaderStage stage)
{
    SlangModuleReflectShaderResourceArgs args;
    args.module = &module;
    args.stage = stage;
    auto resources = ShaderLanguageConverter::slangModuleReflectShaderResource(args);
    // The raster recorder owns the push constant root.
    if (stage != ShaderStage::ComputeShader)
        resources.pushConstantName.clear();
    for (auto& field : resources.bindInfoPool)
    {
        field.variateName = extractMemberName(field.variateName);
        if (field.variateName == "return")
            field.variateName = "RETURN";
    }
    return resources;
}

static int generateRasterPair(int argc, char** argv)
{
    std::filesystem::path vertex, fragment, vertexIR, fragmentIR, vertexHeader, fragmentHeader, output;
    for (int i = 2; i < argc; i += 2)
    {
        std::string_view flag = argv[i];
        if (i + 1 == argc)
            throw std::runtime_error("Missing value for " + std::string(flag));
        const char* value = argv[i + 1];
        if (flag == "-vs") vertex = value;
        else if (flag == "-fs") fragment = value;
        else if (flag == "-vs-ir") vertexIR = value;
        else if (flag == "-fs-ir") fragmentIR = value;
        else if (flag == "-vs-header") vertexHeader = value;
        else if (flag == "-fs-header") fragmentHeader = value;
        else if (flag == "-o") output = value;
        else throw std::runtime_error("Unknown raster pair flag " + std::string(flag));
    }
    if (vertex.empty() || fragment.empty() || vertexIR.empty() || fragmentIR.empty() ||
        vertexHeader.empty() || fragmentHeader.empty() || output.empty())
        throw std::runtime_error("A raster pair requires -vs, -fs, -vs-ir, -fs-ir, -vs-header, -fs-header and -o");

    auto loadBindings = [](const std::filesystem::path& shader, const std::filesystem::path& ir, ShaderStage stage) {
        std::ifstream input(ir, std::ios::binary);
        SlangModule module{shader.stem().string(), shader.string(), {}};
        module.binData.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (module.binData.empty() || input.bad())
            throw std::runtime_error("Cannot read Slang IR from " + ir.string());
        auto resources = reflectBindings(module, stage);
        if (std::none_of(resources.entryPointInfoPool.begin(), resources.entryPointInfoPool.end(),
                         [stage](const auto& entry) { return entry.stage == stage && entry.name == "main"; }))
            throw std::runtime_error("Shader lacks a main entry point for its registered stage: " + shader.string());
        return resources;
    };
    const auto vs = loadBindings(vertex, vertexIR, ShaderStage::VertexShader);
    const auto fs = loadBindings(fragment, fragmentIR, ShaderStage::FragmentShader);
    if (vs.uniformBufferSize && fs.uniformBufferSize && vs.uniformBufferSize != fs.uniformBufferSize)
        throw std::runtime_error("Uniform buffer block sizes differ in " + vertex.string() + " and " + fragment.string());

    const auto vertexType = sanitizeToIdentifier(vertex) + "_t";
    const auto fragmentType = sanitizeToIdentifier(fragment) + "_t";
    std::ostringstream code;
    code << "#pragma once\n#include \"Horizon.h\"\n"
         << "#include \"" << vertexHeader.generic_string() << "\"\n"
         << "#include \"" << fragmentHeader.generic_string() << "\"\n\n"
         << "template<> struct RasterizerPipeline<" << vertexType << ", " << fragmentType << "> {\n"
         << R"(private:
    ::RasterizerPipeline<> pipeline_;

public:
)"
         << "    RasterizerPipeline(uint32_t views = 1, const std::source_location& location = std::source_location::current())\n"
         << "        : pipeline_(" << vertexType << "::slangModule, " << fragmentType << "::slangModule, views, location) {}\n"
         << "    RasterizerPipeline(const " << vertexType << "&, const " << fragmentType
         << "&, uint32_t views = 1, const std::source_location& location = std::source_location::current())\n"
         << R"(        : RasterizerPipeline(views, location) {}

    RasterizerPipeline(RasterizerPipeline&& other) noexcept : pipeline_(std::move(other.pipeline_)) {}

    RasterizerPipeline& operator=(RasterizerPipeline&& other) noexcept
    {
        pipeline_ = std::move(other.pipeline_);
        return *this;
    }

    void setDepthEnabled(bool enabled) { pipeline_.setDepthEnabled(enabled); }
    void setRasterState(RasterState state) { pipeline_.setRasterState(state); }
    void setDepthImage(HardwareImage& image) { pipeline_.setDepthImage(image); }
    [[nodiscard]] HardwareImage getDepthImage() { return pipeline_.getDepthImage(); }

    [[nodiscard]] RasterBatch beginBatch(uint16_t width, uint16_t height, RasterPass pass = {})
    {
        return pipeline_.beginBatch(width, height, pass);
    }
    [[nodiscard]] RasterBatch beginBatch(RasterPass pass = {}) { return pipeline_.beginBatch(pass); }

    RasterizerPipeline& uses(std::initializer_list<ResourceUse> resources)
    {
        pipeline_.uses(resources);
        return *this;
    }

    template<class T>
    RasterizerPipeline& bindRenderTarget(uint32_t location, EmbeddedShader::Texture2DProxy<T>& proxy)
    {
        pipeline_.bindRenderTarget(location, proxy);
        return *this;
    }

)";
    const auto& resources = !vs.uniformBufferSize && fs.uniformBufferSize ? fs : vs;
    emitResourceFields(code, resources, "::RasterizerPipeline<>", nullptr);
    emitOutputFields(code, fs, "::RasterizerPipeline<>", nullptr);
    code << "};\n";

    std::ofstream header(output);
    header << code.str();
    header.close();
    if (!header) throw std::runtime_error("Cannot write raster pair header " + output.string());
    return 0;
}

int main(int argc, char** argv)
{
	std::cout << "Helicon Shader Compile Scripts\n";
	if (argc < 2)
	{
		std::cout << "NOTE:No input\n";
		return 1;
	}
    if (std::string_view(argv[1]) == "-raster-pair")
    {
        try { return generateRasterPair(argc, argv); }
        catch (const std::exception& error)
        {
            std::cerr << "ERROR: " << error.what() << '\n';
            return 1;
        }
    }
	std::filesystem::path path = "";
	std::filesystem::path outPath = "";
    std::filesystem::path moduleOutput;
    std::string_view rasterReady;
    std::vector<std::pair<std::string_view, std::string_view>> rasterPartners;
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
        else if (arg == "-module-output" && i + 1 < argc)
        {
            moduleOutput = argv[++i];
        }
        else if (arg == "-raster-ready")
        {
            if (i + 1 >= argc || argv[i + 1][0] == '-')
            {
                std::cerr << "ERROR: -raster-ready requires a stage macro name.\n";
                return 1;
            }
            rasterReady = argv[++i];
        }
        else if (arg == "-raster-partner")
        {
            if (i + 2 >= argc || argv[i + 1][0] == '-' || argv[i + 2][0] == '-')
            {
                std::cerr << "ERROR: -raster-partner requires a partner macro name and pair header path.\n";
                return 1;
            }
            rasterPartners.emplace_back(argv[i + 1], argv[i + 2]);
            i += 2;
        }
		else if (arg == "-l")
		{
			++i;
			arg = argv[i];
			if (arg == "glsl")
			{
			    inputLanguage = ShaderLanguage::GLSL;
			}
            else if (arg == "hlsl")
			{
                inputLanguage = ShaderLanguage::HLSL;
            }
            else if (arg == "slang")
		    {
		        inputLanguage = ShaderLanguage::Slang;
		    }
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

    if (!rasterPartners.empty() && rasterReady.empty())
    {
        std::cerr << "ERROR: -raster-partner requires -raster-ready for this stage.\n";
        return 1;
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

    bool stageDetected = stageExplicit;

	// 如果未显式指定 stage，则根据文件名后缀或内容推断
	if (!stageExplicit)
	{
		auto stem = path.stem().string();
		auto extension = path.extension().string();
		// 检查常见命名: xxx.vert, xxx.frag, xxx.comp, xxx.vert.glsl 等
		if (extension == ".frag" || extension == ".fs" || stem.ends_with(".frag") ||
		    stem.find("frag") != std::string::npos)
			inputStage = ShaderStage::FragmentShader, stageDetected = true;
		else if (extension == ".comp" || extension == ".cs" || stem.ends_with(".comp") ||
		         stem.find("compute") != std::string::npos)
			inputStage = ShaderStage::ComputeShader, stageDetected = true;
		else if (extension == ".vert" || extension == ".vs" || stem.ends_with(".vert"))
			inputStage = ShaderStage::VertexShader, stageDetected = true;
		else
		{
			// 扫描代码中的 gl_FragCoord / gl_Position / gl_GlobalInvocationID 等内置变量
			if (code.find("gl_FragCoord") != std::string::npos ||
			    code.find("gl_SampleID") != std::string::npos ||
			    (code.find("out vec4") != std::string::npos && code.find("gl_Position") == std::string::npos))
				inputStage = ShaderStage::FragmentShader, stageDetected = true;
			else if (code.find("gl_GlobalInvocationID") != std::string::npos ||
			         code.find("gl_WorkGroupID") != std::string::npos ||
			         code.find("gl_LocalInvocationID") != std::string::npos)
				inputStage = ShaderStage::ComputeShader, stageDetected = true;
			// else default remains VertexShader
		}
		if (stageDetected)
			std::cout << "INFO:Auto-detected shader stage: " << static_cast<int>(inputStage) << "\n";
	}

    const bool moduleOnly = !stageDetected && !hasShaderEntryPoint(code);
    if (moduleOnly)
    {
        std::cout << "INFO:No shader entry point detected; generating Slang module wrapper only.\n";
    }

    SlangModuleCompileArgs args;
    args.moduleName = path.stem().string();
    args.modulePath = path.string();
    args.shaderCode = code;
    args.sourceLanguage = inputLanguage;
    auto slangModule = ShaderLanguageConverter::slangModuleCompiler(args);

    auto resources = moduleOnly ? ShaderResources{} : reflectBindings(slangModule, inputStage);

    if (moduleOnly)
        std::cout << "SUCCESS:Generated Slang module reflection.\n";
    else
	    std::cout << "SUCCESS:Obtained shader reflection information through Slang.\n";

	std::cout << "INFO:Generate the final C++ shader...\n";
	std::stringstream out;
	out << "#pragma once\n#include <Codegen/VariateProxy.h>\n";

    // Wrap everything in a per-shader type and expose an object with the shader name.
    auto nsName = sanitizeToIdentifier(path);
    auto nsTypeName = nsName + "_t";
    out << "struct " << nsTypeName << " {\n";

    auto smName = "slang_module_" + sanitizePath(path);

	generateBinary(out, smName, slangModule);

    // 稳定别名：用户可通过 xxx_glsl::slangModule 引用预编译 shader。
    out << "static inline auto& slangModule = " << smName << ";\n";

    // Generate BindingKey struct declarations first, collect binding block names
	std::cout << "INFO:Generate binding keys for struct '" << nsTypeName << "'...\n";
	auto bindingBlockNames = generateBindingKeys(out, resources);

    SlangModuleReflectionArgs reflectionArgs;
    reflectionArgs.stage = inputStage;
    reflectionArgs.module = &slangModule;
    reflectionArgs.layoutCallback = [&](slang::ProgramLayout*) {};
    reflectionArgs.reflectionCallback = [&](slang::DeclReflection* reflection) {
        for (int i = 0; i < reflection->getChildren().count; ++i)
        {
            auto child = reflection->getChild(i);
            switch (child->getKind())
            {
            case slang::DeclReflection::Kind::Unsupported:
                break;
            case slang::DeclReflection::Kind::Struct:
                if (bindingBlockNames.contains(reflection->getName())) break;
                procStruct(out,child);
                out << "\n";
                break;
            case slang::DeclReflection::Kind::Func:
            {
                bool isEntryPoint = false;
                for (const auto& ep : resources.entryPointInfoPool)
                {
                    if (ep.name == child->getName())
                    {
                        isEntryPoint = true;
                        break;
                    }
                }
                if (isEntryPoint) break;
                procFunc(out, child->asFunction(), smName);
                out << "\n";
            }
                break;
            case slang::DeclReflection::Kind::Module:
                break;
            case slang::DeclReflection::Kind::Generic:
                break;
            case slang::DeclReflection::Kind::Variable:
                if (bindingBlockNames.contains(child->getName())) break;
                procStructInstance(out,child);
                break;
            case slang::DeclReflection::Kind::Namespace:
                break;
            case slang::DeclReflection::Kind::Enum:
                break;
            }
        }
    };
    ShaderLanguageConverter::slangModuleReflection(reflectionArgs);

    out << "static constexpr size_t pushConstantBlockSize = " << resources.pushConstantSize << ";\n"
        << "static constexpr size_t uniformBufferBlockSize = " << resources.uniformBufferSize << ";\n";
    if (inputStage == ShaderStage::ComputeShader ||
        std::any_of(resources.entryPointInfoPool.begin(), resources.entryPointInfoPool.end(),
                    [](const auto& entry) { return entry.stage == ShaderStage::ComputeShader; }))
        generateComputeBindings(out, resources);

	out << "}; // struct " << nsTypeName << "\n";
	out << "inline constexpr " << nsTypeName << " " << nsName << "{};\n";
    if (!rasterReady.empty())
    {
        out << "\n#define " << rasterReady << " 1\n";
        for (const auto& [partner, header] : rasterPartners)
            out << "#if defined(" << partner << ")\n#include \"" << header << "\"\n#endif\n";
    }

	//std::cout << out.str();

	auto stem = path.stem();
	auto outFilePath = outPath / (stem.string() + ext);
	file.open(outFilePath,std::ios::out);
	file << out.str();
	file.close();
    if (!file)
    {
        std::cerr << "ERROR: Cannot write shader header " << outFilePath << '\n';
        return 1;
    }
    if (!moduleOutput.empty())
    {
        std::ofstream module(moduleOutput, std::ios::binary);
        module.write(reinterpret_cast<const char*>(slangModule.binData.data()), slangModule.binData.size());
        module.close();
        if (!module)
        {
            std::cerr << "ERROR: Cannot write Slang IR to " << moduleOutput << '\n';
            return 1;
        }
    }

	std::cout << "SUCCESS:The C++ shader has been output to the following directory:\n\t" << outFilePath.string();

	return 0;
}
