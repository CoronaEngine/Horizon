#include <filesystem>
#include <iostream>
#include <Helicon.h>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <array>
#include <iterator>
#include <map>
#include <optional>
#include <span>
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
		case BindType::accelerationStructure:
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
                           std::string_view indent = "    ", bool aliased = false)
{
    if (field.bindType == ShaderResources::accelerationStructure) {
        if (stateType != "::RayTracePipeline<>" || field.set != 3 || field.binding != 1 || field.elementCount != 1)
            throw std::runtime_error("A scene must be a scalar acceleration structure at set 3 binding 1");
        out << indent << stateType << "::SceneField " << field.variateName << "{pipeline_};\n";
        return;
    }
    std::ostringstream args;
    const bool descriptor = isDirectResourceBindType(field.bindType);
    args << ", " << field.byteOffset << ", " << field.typeSize << ", " << static_cast<int32_t>(field.bindType)
         << ", " << field.location;
    if (field.bindType != ShaderResources::stageOutputs)
        args << ", " << field.set << ", " << field.binding;
    if (descriptor)
        args << ", " << field.elementCount << ", static_cast<::EmbeddedShader::Ast::AccessPermissions>("
             << static_cast<unsigned>(field.access) << ")";
    if (aliased) args << ", true";

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

static void emitResourceFields(std::ostream& out, std::span<const ShaderResources> stages,
                               std::string_view stateType)
{
    auto emitBlock = [&](const ShaderResources& resources, const std::string& name, ShaderResources::BindType kind) {
        out << "    struct " << name << "_t {\n";
        std::vector<std::string> blockInits;
        for (const auto& field : resources.bindInfoPool)
            if (field.bindType == kind && !isPushConstantBlockRecord(field, resources))
                emitBoundField(out, field, stateType, &blockInits, "        ");
        emitBindingConstructor(out, name + "_t", stateType, blockInits, "        ");
        out << "    } " << name << "{&pipeline_};\n";
    };
    const auto push = std::ranges::find_if(stages, [](const auto& r) { return !r.pushConstantName.empty(); });
    const auto ubo = std::ranges::find_if(stages, [](const auto& r) { return !r.uniformBufferName.empty(); });
    if (push != stages.end()) emitBlock(*push, push->pushConstantName, ShaderResources::pushConstantMembers);
    if (ubo != stages.end()) emitBlock(*ubo, ubo->uniformBufferName, ShaderResources::uniformBufferMembers);
    // Every alias of a descriptor binding covers the accesses declared by all stages.
    std::vector<ShaderResources::ShaderBindInfo> fields;
    std::map<std::pair<uint32_t, uint32_t>, Ast::AccessPermissions> accessByBinding;
    for (const auto& stage : stages)
    {
        for (const auto& field : stage.bindInfoPool)
        {
            if (!isDirectResourceBindType(field.bindType)) continue;
            auto& access = accessByBinding[{field.set, field.binding}];
            access = access | field.access;
            const auto previous = std::ranges::find(fields, field.variateName,
                                                    &ShaderResources::ShaderBindInfo::variateName);
            if (previous == fields.end()) fields.push_back(field);
            else if (previous->set != field.set || previous->binding != field.binding ||
                     ((previous->bindType == ShaderResources::accelerationStructure) != (field.bindType == ShaderResources::accelerationStructure)))
                throw std::runtime_error("Ambiguous descriptor binding for shader field " + field.variateName);
            else if (!previous->elementCount || (field.elementCount && field.elementCount < previous->elementCount))
                previous->elementCount = field.elementCount;
        }
    }
    for (auto& field : fields)
    {
        field.access = accessByBinding.at({field.set, field.binding});
        const bool aliased = std::ranges::any_of(fields, [&](const auto& other) {
            return other.set == field.set && other.binding == field.binding && other.variateName != field.variateName;
        });
        emitBoundField(out, field, stateType, nullptr, "    ", aliased);
    }
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
        << "    ComputePipeline(const " << shaderType << "& shader = {})\n"
        << "        : pipeline_(::EmbeddedShader::ComputeEntry{shader}) {}\n"
        << R"(
    ComputePipeline(ComputePipeline&& other) noexcept : pipeline_(std::move(other.pipeline_)) {}

    ComputePipeline& operator=(ComputePipeline&& other) noexcept
    {
        pipeline_ = std::move(other.pipeline_);
        return *this;
    }

    [[nodiscard]] HardwareExecutor::Command operator()(uint16_t x, uint16_t y = 1, uint16_t z = 1)
    {
        return pipeline_(x, y, z);
    }

)";
    emitResourceFields(out, {&resources, 1}, "::ComputePipeline<>");
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
        ss << "{\"";
        if (child->findModifier(slang::Modifier::InOut)) ss << "inout ";
        else if (child->findModifier(slang::Modifier::Out)) ss << "out ";
        ss << typeNameToSlang(procType(child->getType())) << "\",\"" << child->getName() << "\"},";
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
    if (stage != SLANG_STAGE_VERTEX && stage != SLANG_STAGE_FRAGMENT && stage != SLANG_STAGE_COMPUTE &&
        stage != SLANG_STAGE_MESH && stage != SLANG_STAGE_AMPLIFICATION && stage != SLANG_STAGE_RAY_GENERATION &&
        stage != SLANG_STAGE_MISS && stage != SLANG_STAGE_CLOSEST_HIT)
        throw std::runtime_error("Unsupported shader stage: " + std::string(module->getFilePath()));
    if (expectedStage && slangStageToShaderStage(stage) != *expectedStage)
        throw std::runtime_error("Shader main does not match its registered stage: " + std::string(module->getFilePath()));
    return program;
}

static void prepareBindingNames(ShaderResources& resources)
{
    for (auto& field : resources.bindInfoPool)
    {
        field.variateName = extractMemberName(field.variateName);
        if (field.variateName == "return")
            field.variateName = "RETURN";
    }
}

// Compare the shader-visible ABI, including members with equal byte sizes but different types.
static bool sameParameterLayout(slang::TypeLayoutReflection* a, slang::TypeLayoutReflection* b)
{
    using Kind = slang::TypeReflection::Kind;
    if (a->getKind() != b->getKind() || a->getSize() != b->getSize()) return false;
    switch (a->getKind()) {
    case Kind::Struct:
        if (a->getFieldCount() != b->getFieldCount()) return false;
        for (unsigned i = 0; i < a->getFieldCount(); ++i) {
            auto* left = a->getFieldByIndex(i);
            auto* right = b->getFieldByIndex(i);
            if (std::string_view(left->getName()) != right->getName() || left->getOffset() != right->getOffset() ||
                !sameParameterLayout(left->getTypeLayout(), right->getTypeLayout())) return false;
        }
        return true;
    case Kind::Array:
        return a->getElementCount() == b->getElementCount() && a->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM) ==
               b->getElementStride(SLANG_PARAMETER_CATEGORY_UNIFORM) &&
               sameParameterLayout(a->getElementTypeLayout(), b->getElementTypeLayout());
    case Kind::Matrix:
        if (a->getMatrixLayoutMode() != b->getMatrixLayoutMode()) return false;
        [[fallthrough]];
    case Kind::Scalar:
    case Kind::Vector:
        return a->getScalarType() == b->getScalarType() && a->getRowCount() == b->getRowCount() &&
               a->getColumnCount() == b->getColumnCount();
    default:
        return false;
    }
}

static int generateGraphicsPipeline(int argc, char** argv)
{
    struct Stage {
        std::string_view flag;
        ShaderStage stage;
        std::filesystem::path source, ir, header;
    };
    std::array stages{Stage{"-ts", ShaderStage::TaskShader}, Stage{"-vs", ShaderStage::VertexShader},
                      Stage{"-ms", ShaderStage::MeshShader}, Stage{"-fs", ShaderStage::FragmentShader},
                      Stage{"-rg", ShaderStage::RayGeneration}, Stage{"-mi", ShaderStage::Miss}, Stage{"-ch", ShaderStage::ClosestHit}};
    std::filesystem::path output;
    for (int i = 2; i < argc; i += 2) {
        std::string_view flag = argv[i];
        if (i + 1 == argc) throw std::runtime_error("Missing value for " + std::string(flag));
        if (flag == "-o") {
            output = argv[i + 1];
            continue;
        }
        auto stage = std::ranges::find(stages, flag.substr(0, 3), &Stage::flag);
        if (stage == stages.end()) throw std::runtime_error("Unknown graphics stage " + std::string(flag));
        const auto option = flag.substr(3);
        if (option.empty()) stage->source = argv[i + 1];
        else if (option == "-ir") stage->ir = argv[i + 1];
        else if (option == "-header") stage->header = argv[i + 1];
        else throw std::runtime_error("Unknown graphics stage option " + std::string(flag));
    }

    const bool task = !stages[0].source.empty();
    const bool vertex = !stages[1].source.empty();
    const bool mesh = !stages[2].source.empty();
    const bool ray = !stages[4].source.empty() || !stages[5].source.empty() || !stages[6].source.empty();
    const bool rayGroup = ray && !stages[4].source.empty() && !stages[5].source.empty() && !stages[6].source.empty()
                          && !task && !vertex && !mesh && stages[3].source.empty();
    if (output.empty() || (ray ? !rayGroup : stages[3].source.empty() || vertex == mesh || (task && !mesh)))
        throw std::runtime_error("Expected VS/FS, MS/FS, Task/MS/FS or raygen/miss/closest-hit and an output header");

    // foo.vert and foo.frag are distinct modules with the same Slang module name.
    std::vector<Slang::ComPtr<slang::ISession>> sessions;
    std::vector<Slang::ComPtr<slang::IComponentType>> programs;
    std::array<slang::VariableLayoutReflection*, 2> parameterBlocks{};
    std::vector<ShaderResources> resources;
    std::vector<std::string> types, names;
    std::ostringstream code;
    code << "#pragma once\n#include \"Horizon.h\"\n";

    for (const auto& stage : stages) {
        if (stage.source.empty()) continue;
        std::ifstream input(stage.ir, std::ios::binary);
        SlangModule module{stage.source.stem().string(), stage.source.string(), {}};
        module.binData.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (module.binData.empty() || input.bad() || stage.header.empty())
            throw std::runtime_error("Missing shader IR or header for " + stage.source.string());
        auto& session = sessions.emplace_back(createShaderSession());
        auto loaded = ShaderLanguageConverter::loadModule(session, module);
        if (!loaded) throw std::runtime_error("Cannot load Slang IR from " + stage.ir.string());
        auto& program = programs.emplace_back(linkStage(session, loaded, stage.stage));
        auto* layout = program->getLayout();
        auto& bindings = resources.emplace_back(ShaderLanguageConverter::slangReflection(layout));
        prepareBindingNames(bindings);
        if (mesh || ray) {
            for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
                auto* var = layout->getParameterByIndex(i);
                auto* type = var->getTypeLayout();
                if (type->getKind() != slang::TypeReflection::Kind::ConstantBuffer) continue;
                const bool push = type->getParameterCategory() == slang::ParameterCategory::PushConstantBuffer;
                if (!push && (var->getBindingSpace() != 3 || var->getBindingIndex() != 0))
                    throw std::runtime_error("Pipeline uniform buffer must use set 3 binding 0: " + stage.source.string());
                auto*& previous = parameterBlocks[push ? 1 : 0];
                if (previous && (std::string_view(previous->getName()) != var->getName() ||
                    !sameParameterLayout(previous->getTypeLayout()->getElementTypeLayout(), type->getElementTypeLayout())))
                    throw std::runtime_error("Incompatible pipeline parameter block in " + stage.source.string());
                previous = var;
            }
        } else {
            // The vertex-pulling recorder supplies the raster push constant root itself.
            bindings.pushConstantName.clear();
        }
        types.push_back(sanitizeFilename(stage.source) + "_t");
        names.emplace_back(stage.flag.substr(1));
        code << "#include \"" << stage.header.generic_string() << "\"\n";
    }
    if (vertex && resources.front().uniformBufferSize && resources.back().uniformBufferSize &&
        resources.front().uniformBufferSize != resources.back().uniformBufferSize)
        throw std::runtime_error("Uniform buffer block sizes differ in raster stages");

    const std::string name = ray ? "RayTracePipeline" : mesh ? "MeshPipeline" : "RasterPipeline";
    const auto runtime = "::" + name + "<>";
    auto emitList = [&](auto emit) {
        for (size_t i = 0; i < types.size(); ++i) {
            if (i) code << ", ";
            emit(i);
        }
    };
    code << "\ntemplate<> struct " << name << "<";
    emitList([&](size_t i) { code << types[i]; });
    code << "> {\nprivate:\n    " << runtime << " pipeline_;\n\npublic:\n";
    if (ray)
        code << "    using AccelerationStructure = " << runtime << "::AccelerationStructure;\n"
             << "    using Instance = " << runtime << "::Instance;\n"
             << "    " << name << "() : " << name << "(";
    else
        code << "    " << name << "(" << runtime << "::State state = {}, uint32_t views = 1) : " << name << "(";
    emitList([&](size_t) { code << "{}"; });
    code << (ray ? ") {}\n    " : ", state, views) {}\n    ") << name << "(";
    emitList([&](size_t i) { code << "const " << types[i] << "& " << names[i]; });
    if (!ray) code << ", " << runtime << "::State state = {}, uint32_t views = 1";
    code << ")\n        : pipeline_(";
    if (ray)
        code << "rg.shaderModule, mi.shaderModule, ch.shaderModule";
    else if (mesh)
        code << (task ? "&ts.shaderModule" : "nullptr") << ", ms.shaderModule, fs.shaderModule, state, views";
    else
        code << "::EmbeddedShader::VertexEntry{vs}, ::EmbeddedShader::FragmentEntry{fs}, state, {}, views";
    code << ") {}\n\n    " << name << "(" << name << "&& other) noexcept : pipeline_(std::move(other.pipeline_)) {}\n"
         << "    " << name << "& operator=(" << name << "&& other) noexcept {\n"
         << "        pipeline_ = std::move(other.pipeline_);\n        return *this;\n    }\n\n"
         << "    [[nodiscard]] HardwareExecutor::Command operator()(\n        ";
    if (ray)
        code << "uint32_t width, uint32_t height = 1, uint32_t depth = 1)\n"
             << "    {\n        return pipeline_(width, height, depth);\n    }\n\n";
    else {
        if (!mesh) code << "const HardwareBuffer& vertices, const HardwareBuffer& indices,\n        ";
        code << "const HardwareBuffer& packets, uint32_t count, ";
        if (!mesh) code << "const HardwareBuffer& parameters = {},\n        ";
        code << R"(const HardwareImage& depth = {},
        std::optional<::RasterPipeline<>::ScissorRect> scissor = {}, std::optional<::RasterPipeline<>::Pass> pass = {},
        std::optional<ktm::uvec2> extent = {})
    {
)" << "        return pipeline_(" << (mesh ? "packets, count" : "vertices, indices, packets, count, parameters")
             << ", depth, scissor, pass, extent);\n    }\n\n";
    }
    emitResourceFields(code, resources, runtime);
    if (!ray) emitOutputFields(code, resources.back(), runtime);
    code << "};\n\ntemplate<";
    emitList([&](size_t i) { code << "std::same_as<" << types[i] << "> " << names[i]; });
    code << ">\n" << name << "(";
    emitList([&](size_t i) { code << "const " << names[i] << "&"; });
    if (!ray) code << ", " << runtime << "::State = {}, uint32_t = 1";
    code << ") -> " << name << "<";
    emitList([&](size_t i) { code << names[i]; });
    code << ">;\n";

    std::ofstream header(output);
    header << code.str();
    header.close();
    if (!header) throw std::runtime_error("Cannot write graphics pipeline header " + output.string());
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
        return generateGraphicsPipeline(argc, argv);
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
			else if (arg == "raygen" || arg == "miss" || arg == "closesthit")
            {
                inputStage = arg == "raygen" ? ShaderStage::RayGeneration : arg == "miss" ? ShaderStage::Miss : ShaderStage::ClosestHit;
                stageExplicit = true;
            }
            else if (arg == "mesh" || arg == "task")
			{
				inputStage = arg == "mesh" ? ShaderStage::MeshShader : ShaderStage::TaskShader;
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
        out << "static constexpr auto shaderStage = ::EmbeddedShader::ShaderStage::"
            << enumToString(resources.entryPointInfoPool.front().stage) << ";\n";
        out << "static inline const ::EmbeddedShader::ShaderCodeModule shaderModule{std::vector<uint32_t>{";
        emitSpirVLiteral(out, result.binaryTargets.at(ShaderLanguage::SpirV));
        out << "}, " << serializeShaderResources(resources) << "};\n";
        prepareBindingNames(resources);
        const auto stage = resources.entryPointInfoPool.front().stage;
        if (stage == ShaderStage::VertexShader || stage == ShaderStage::FragmentShader)
            resources.pushConstantName.clear();
    }

    using Kind = slang::DeclReflection::Kind;
    std::stringstream functions;
    const bool bindingsOnly = !moduleOnly && (resources.entryPointInfoPool.front().stage == ShaderStage::MeshShader ||
                                           resources.entryPointInfoPool.front().stage == ShaderStage::TaskShader ||
                                           resources.entryPointInfoPool.front().stage == ShaderStage::RayGeneration ||
                                           resources.entryPointInfoPool.front().stage == ShaderStage::Miss ||
                                           resources.entryPointInfoPool.front().stage == ShaderStage::ClosestHit);
    for (auto child : declarations->getChildren())
    {
        if (bindingsOnly) break; // Only module-scope shader bindings are exposed to C++ for these stages.
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
        for (auto [partners, header] : rasterPartners) {
            out << "#if ";
            while (!partners.empty()) {
                auto comma = partners.find(',');
                out << "defined(" << partners.substr(0, comma) << ')';
                if (comma == partners.npos) break;
                out << " && ";
                partners.remove_prefix(comma + 1);
            }
            out << "\n#include \"" << header << "\"\n#endif\n";
        }
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
