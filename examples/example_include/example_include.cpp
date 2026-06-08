#include "Compiler/ShaderTypeMapping.h"

#include <Compiler/ShaderLanguageConverter.h>
#include <fstream>
#include <iostream>
#include <slang.h>
#include <sstream>
void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
{
    if (diagnosticsBlob != nullptr)
    {
        std::cout << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
    }
}

namespace
{
    std::string_view testShaderPath = "C:/test/header.txt";
    std::string_view outPath = "C:/test/header-via-api.slang-module";
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
        break;
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

void procFunc(std::stringstream& ss, slang::FunctionReflection* reflection)
{
    ss << EmbeddedShader::typeNameToCpp(procType(reflection->getReturnType()))
    << " " << reflection->getName()
    << "(";

    if (reflection->getParameterCount() != 0)
        ss << procVarDefinition(reflection->getParameterByIndex(0));
    for (int i = 1; i < reflection->getParameterCount(); ++i)
    {
        ss << "," << procVarDefinition(reflection->getParameterByIndex(i));
    }

    ss <<");";
}

void procStruct(std::stringstream& ss, slang::DeclReflection* reflection)
{
    for (int i = 0; i < reflection->getChildren().count; ++i)
    {
        auto field = reflection->getChild(i);
        procVarDefinition(field->asVariable());
    }
}

void testSlangCompileWithModules()
{
    EmbeddedShader::SlangModuleCompileArgs moduleArg;
    moduleArg.moduleName = "tool";
    moduleArg.shaderCode = R"(
#version 430 core
vec4 getColor(float r)
{
    return vec4(r,1.0,1.0,1.0);
}
)";
    std::stringstream ss;
    // moduleArg.reflectionCallback = [&](slang::DeclReflection* reflection) {
    //     for (int i = 0; i < reflection->getChildren().count; ++i)
    //     {
    //         auto child = reflection->getChild(i);
    //         switch (child->getKind())
    //         {
    //         case slang::DeclReflection::Kind::Unsupported:
    //             break;
    //         case slang::DeclReflection::Kind::Struct:
    //             procStruct(ss,child);
    //             break;
    //         case slang::DeclReflection::Kind::Func:
    //             procFunc(ss,child->asFunction());
    //             break;
    //         case slang::DeclReflection::Kind::Module:
    //             break;
    //         case slang::DeclReflection::Kind::Generic:
    //             break;
    //         case slang::DeclReflection::Kind::Variable:
    //             break;
    //         case slang::DeclReflection::Kind::Namespace:
    //             break;
    //         case slang::DeclReflection::Kind::Enum:
    //             break;
    //         }
    //     }
    // };
    EmbeddedShader::SlangModule slangModule = EmbeddedShader::ShaderLanguageConverter::slangModuleCompiler(moduleArg);
    std::cout << ss.str() << std::endl;
    EmbeddedShader::SlangCompileArgs arg;
    arg.targetLanguages = {EmbeddedShader::ShaderLanguage::GLSL,EmbeddedShader::ShaderLanguage::SpirV,};
    arg.source = R"(
import tool;
[shader("fragment")]
float4 main() : SV_TARGET
{
    return getColor(0.5);
}
)";
    arg.deps.push_back(&slangModule);
    auto result = EmbeddedShader::ShaderLanguageConverter::slangCompilerWithModules(arg);
    std::cout << result.stringTargets[EmbeddedShader::ShaderLanguage::GLSL];
}

void run_example_include()
{
    testSlangCompileWithModules();
}