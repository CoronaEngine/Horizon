#include <filesystem>
#include <iostream>
#include <Helicon.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <array>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <Compiler/ShaderUtils.h>
#include <Compiler/ShaderTypeMapping.h>
#include "Compiler/ShaderCodeCompiler.h"
using namespace EmbeddedShader;

void generateSlangModule(std::ostream& out, const SlangModule& slangModule)
{
    out << "static inline ::EmbeddedShader::SlangModule slangModule {{";
    emitCppStringLiteral(out, slangModule.name);
    out << "}, {";
    emitCppStringLiteral(out, slangModule.path);
    out << "}, {";
    emitSlangModuleLiteral(out, slangModule.binData);
    out << "}};\n";
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

using ShaderResources = ShaderCodeModule::ShaderResources;

// Nested binding groups receive p in their constructor. Direct fields bind
// to their containing pipeline, including after a move.
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
                               std::string_view stateType)
{
    auto emitBlock = [&](const std::string& name, ShaderResources::BindType kind) {
        if (name.empty()) return;
        out << "    struct " << name << "_t {\n";
        std::vector<std::string> blockInits;
        for (const auto& field : resources.bindInfoPool)
            if (field.bindType == kind && !isPushConstantBlockRecord(field, resources))
                emitBoundField(out, field, stateType, &blockInits, "        ");
        emitBindingConstructor(out, name + "_t", stateType, blockInits, "        ");
        out << "    } " << name << "{&pipeline_};\n";
    };
    emitBlock(resources.pushConstantName, ShaderResources::pushConstantMembers);
    emitBlock(resources.uniformBufferName, ShaderResources::uniformBufferMembers);
    for (const auto& field : resources.bindInfoPool)
        if (isDirectResourceBindType(field.bindType))
            emitBoundField(out, field, stateType, nullptr);
}

static void emitOutputFields(std::ostream& out, const ShaderResources& resources,
                             std::string_view stateType)
{
    for (const auto& field : resources.bindInfoPool)
        if (field.bindType == ShaderResources::stageOutputs)
            emitBoundField(out, field, stateType, nullptr);
}

static void generateComputePipeline(std::ostream& out, const ShaderResources& resources,
                                    std::string_view shaderType)
{
    out << "\ntemplate<> struct ComputePipeline<" << shaderType << "> {\n"
        << R"(private:
    ::ComputePipeline<> pipeline_;

public:
)"
        << "    ComputePipeline() : pipeline_(" << shaderType << "::shaderModule) {}\n"
        << "    ComputePipeline(const " << shaderType << "&) : ComputePipeline() {}\n"
        << R"(
    ComputePipeline(ComputePipeline&& other) noexcept : pipeline_(std::move(other.pipeline_)) {}

    ComputePipeline& operator=(ComputePipeline&& other) noexcept
    {
        pipeline_ = std::move(other.pipeline_);
        return *this;
    }

    [[nodiscard]] ComputeCommand operator()(uint16_t x, uint16_t y = 1, uint16_t z = 1)
    {
        return pipeline_(x, y, z);
    }

    ComputePipeline& uses(std::span<const ResourceUse> resources)
    {
        pipeline_.uses(resources);
        return *this;
    }
    ComputePipeline& uses(std::initializer_list<ResourceUse> resources) { return uses(std::span(resources)); }

)";
    emitResourceFields(out, resources, "::ComputePipeline<>");
    emitOutputFields(out, resources, "::ComputePipeline<>");
    out << "};\n\ntemplate<std::same_as<" << shaderType << "> CS>\n"
        << "ComputePipeline(const CS&) -> ComputePipeline<CS>;\n";
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

void procFunc(std::stringstream& ss, slang::FunctionReflection* reflection)
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
    ss << "},&slangModule};\n";
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

static Slang::ComPtr<slang::ISession> createShaderSession()
{
    auto globalSession = ShaderLanguageConverter::getGlobalSession();
    slang::TargetDesc target{};
    target.format = SLANG_SPIRV;
    target.profile = globalSession->findProfile("spirv_1_6");
    std::array targets{target};
    std::array options{slang::CompilerOptionEntry{
        slang::CompilerOptionName::EmitSpirvDirectly,
        {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
    }};
    return ShaderLanguageConverter::createSession(globalSession, targets, options);
}

static Slang::ComPtr<slang::IComponentType> linkStage(const Slang::ComPtr<slang::ISession>& session,
                                                       const Slang::ComPtr<slang::IModule>& module,
                                                       std::optional<ShaderStage> expectedStage)
{
    // For an unannotated main, Slang needs the stage supplied by the build.
    auto entry = ShaderLanguageConverter::tryFindEntryPoint(module, "main", expectedStage.value_or(ShaderStage::VertexShader));
    if (!entry)
        throw std::runtime_error("Shader lacks a main entry point: " + std::string(module->getFilePath()));
    std::array<slang::IComponentType*, 2> components{module.get(), entry.get()};
    auto program = ShaderLanguageConverter::link(session, components);
    auto stage = program->getLayout()->getEntryPointByIndex(0)->getStage();
    if (stage != SLANG_STAGE_VERTEX && stage != SLANG_STAGE_FRAGMENT && stage != SLANG_STAGE_COMPUTE)
        throw std::runtime_error("Unsupported shader stage: " + std::string(module->getFilePath()));
    if (expectedStage && slangStageToShaderStage(stage) != *expectedStage)
        throw std::runtime_error("Shader main does not match its registered stage: " + std::string(module->getFilePath()));
    return program;
}

static void prepareBindingNames(ShaderResources& resources)
{
    // The raster recorder owns the push constant root.
    if (resources.entryPointInfoPool.front().stage != ShaderStage::ComputeShader)
        resources.pushConstantName.clear();
    for (auto& field : resources.bindInfoPool)
    {
        field.variateName = extractMemberName(field.variateName);
        if (field.variateName == "return")
            field.variateName = "RETURN";
    }
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
        auto session = createShaderSession();
        auto loaded = ShaderLanguageConverter::loadModule(session, module);
        if (!loaded)
            throw std::runtime_error("Cannot load Slang IR from " + ir.string());
        auto program = linkStage(session, loaded, stage);
        auto resources = ShaderLanguageConverter::slangReflection(program->getLayout());
        prepareBindingNames(resources);
        return resources;
    };
    const auto vs = loadBindings(vertex, vertexIR, ShaderStage::VertexShader);
    const auto fs = loadBindings(fragment, fragmentIR, ShaderStage::FragmentShader);
    if (vs.uniformBufferSize && fs.uniformBufferSize && vs.uniformBufferSize != fs.uniformBufferSize)
        throw std::runtime_error("Uniform buffer block sizes differ in " + vertex.string() + " and " + fragment.string());

    const auto vertexType = sanitizeFilename(vertex) + "_t";
    const auto fragmentType = sanitizeFilename(fragment) + "_t";
    std::ostringstream code;
    code << "#pragma once\n#include \"Horizon.h\"\n"
         << "#include \"" << vertexHeader.generic_string() << "\"\n"
         << "#include \"" << fragmentHeader.generic_string() << "\"\n\n"
         << "template<> struct RasterizerPipeline<" << vertexType << ", " << fragmentType << "> {\n"
         << R"(private:
    ::RasterizerPipeline<> pipeline_;

public:
)"
         << "    RasterizerPipeline(uint32_t views = 1)\n"
         << "        : pipeline_(" << vertexType << "::shaderModule, " << fragmentType << "::shaderModule, views) {}\n"
         << "    RasterizerPipeline(const " << vertexType << "&, const " << fragmentType
         << "&, uint32_t views = 1)\n"
         << R"(        : RasterizerPipeline(views) {}

    RasterizerPipeline(RasterizerPipeline&& other) noexcept : pipeline_(std::move(other.pipeline_)) {}

    RasterizerPipeline& operator=(RasterizerPipeline&& other) noexcept
    {
        pipeline_ = std::move(other.pipeline_);
        return *this;
    }

    void setRasterState(RasterState state) { pipeline_.setRasterState(state); }
    void setDepthImage(HardwareImage& image) { pipeline_.setDepthImage(image); }
    [[nodiscard]] HardwareImage getDepthImage() { return pipeline_.getDepthImage(); }

    [[nodiscard]] RasterCommand drawIndirect(const HardwareBuffer& vertices, const HardwareBuffer& indices,
                                             const HardwareBuffer& packets, uint32_t count, const HardwareBuffer& parameters = {},
                                             std::optional<ScissorRect> scissor = {}, std::optional<RasterPass> pass = {},
                                             std::optional<ktm::uvec2> extent = {})
    {
        return pipeline_.drawIndirect(vertices, indices, packets, count, parameters, scissor, pass, extent);
    }

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
    emitResourceFields(code, resources, "::RasterizerPipeline<>");
    emitOutputFields(code, fs, "::RasterizerPipeline<>");
    code << "};\n";

    std::ofstream header(output);
    header << code.str();
    header.close();
    if (!header) throw std::runtime_error("Cannot write raster pair header " + output.string());
    return 0;
}

int main(int argc, char** argv) try
{
	std::cout << "Helicon Shader Compile Scripts\n";
	if (argc < 2)
	{
		std::cout << "NOTE:No input\n";
		return 1;
	}
    if (std::string_view(argv[1]) == "-raster-pair")
        return generateRasterPair(argc, argv);
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

    SlangModuleCompileArgs args;
    args.moduleName = path.stem().string();
    args.modulePath = path.string();
    args.shaderCode = code;
    args.sourceLanguage = inputLanguage;
    auto slangModule = ShaderLanguageConverter::slangModuleCompiler(args);

    auto session = createShaderSession();
    auto module = ShaderLanguageConverter::loadModule(session, slangModule);
    if (!module)
        throw std::runtime_error("Cannot load Slang IR from " + path.string());
    auto declarations = module->getModuleReflection();
    bool moduleOnly = true;
    for (auto function : declarations->getChildrenOfKind<slang::DeclReflection::Kind::Func>())
    {
        if (std::string_view(function->getName()) == "main")
        {
            moduleOnly = false;
            break;
        }
    }
    if (moduleOnly && !rasterReady.empty())
        throw std::runtime_error("Raster shader lacks a main entry point: " + path.string());

	std::cout << "INFO:Generate the final C++ shader...\n";
	std::stringstream out;

    // Wrap everything in a per-shader type and expose an object with the shader name.
    auto nsName = sanitizeFilename(path);
    auto nsTypeName = nsName + "_t";
    out << "struct " << nsTypeName << " {\n";

    ShaderResources resources;
    if (!moduleOnly)
    {
        auto program = linkStage(session, module, stageDetected ? std::optional{inputStage} : std::nullopt);
        SlangCompileResult result;
        ShaderLanguageConverter::fillCompileResult(result, program, 0, SLANG_SPIRV, true);
        resources = std::move(result.reflections.at(ShaderLanguage::SpirV));
        for (auto& field : resources.bindInfoPool)
            field.variateName = extractMemberName(field.variateName);
        out << "static inline const ::EmbeddedShader::ShaderCodeModule shaderModule{std::vector<uint32_t>{";
        emitSpirVLiteral(out, result.binaryTargets.at(ShaderLanguage::SpirV));
        out << "}, " << serializeShaderResources(resources) << "};\n";
        prepareBindingNames(resources);
    }

    using Kind = slang::DeclReflection::Kind;
    std::stringstream functions;
    for (auto child : declarations->getChildren())
    {
        const auto kind = child->getKind();
        if (kind == Kind::Struct || kind == Kind::Variable)
        {
            const std::string_view name = child->getName();
            if (!name.empty() && (name == resources.pushConstantName || name == resources.uniformBufferName))
                continue;
        }
        switch (kind)
        {
            case Kind::Struct:
                procStruct(out, child);
                out << "\n";
                break;
            case Kind::Func:
                if (moduleOnly || std::string_view(child->getName()) != "main")
                    procFunc(functions, child->asFunction());
                break;
            case Kind::Variable:
                procStructInstance(out, child);
                break;
            default:
                break;
        }
    }
    if (moduleOnly || !functions.view().empty())
    {
        generateSlangModule(out, slangModule);
        out << functions.view();
    }

    out << "static constexpr size_t pushConstantBlockSize = " << resources.pushConstantSize << ";\n"
        << "static constexpr size_t uniformBufferBlockSize = " << resources.uniformBufferSize << ";\n";
	out << "}; // struct " << nsTypeName << "\n";
	out << "inline constexpr " << nsTypeName << " " << nsName << "{};\n";
    const bool computeStage = !moduleOnly && resources.entryPointInfoPool.front().stage == ShaderStage::ComputeShader;
    if (computeStage)
        generateComputePipeline(out, resources, nsTypeName);
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
    file << "#pragma once\n"
         << (computeStage ? "#include \"Horizon.h\"\n" : "#include <Codegen/VariateProxy.h>\n")
         << out.str();
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
catch (const std::exception& error)
{
    std::cerr << "ERROR: " << error.what() << '\n';
    return 1;
}
