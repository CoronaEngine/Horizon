#pragma once
#include "slang.h"

#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <Compiler/ShaderCodeCompiler.h>


namespace EmbeddedShader
{
    enum class ShaderLanguage : uint16_t
    {
        GLSL,
        HLSL,
        DXIL,
        DXBC,
        SpirV,
        Slang,
    };

    std::string enumToString(ShaderLanguage language);

    enum class ShaderStage : uint16_t
    {
        VertexShader = 0,
        FragmentShader = 1,
        ComputeShader = 2,
        // RayGenShader = 3,
        // IntersectShader = 4,
        // AnyHitShader = 5,
        // ClosestHitShader = 6,
        // MissShader = 7,
    };

    std::string enumToString(ShaderStage stage);

// 函数参数信息
	struct VariableInfo
	{
		std::string name;
		std::string typeName;
		uint32_t typeId = 0;
	};

	// 函数签名信息
	struct FunctionSignature
	{
		std::string name;
		std::string returnTypeName;
		uint32_t returnTypeId = 0;
		std::vector<VariableInfo> parameters;
		bool isEntryPoint = false;
	};

	struct StructInfo
	{
		std::string name;
		std::vector<VariableInfo> members;
	};

	struct IRReflection
	{
		enum class Type
		{
			Unknown = -1,
			FunctionSignature,
			Struct,
		};
		std::variant<FunctionSignature, StructInfo> info;
		Type type = Type::Unknown;
	};

    struct SlangCompileResult
    {
        using BinaryTarget = std::vector<uint32_t>;
        using StringTarget = std::string;
        std::unordered_map<ShaderLanguage, BinaryTarget> binaryTargets;
        std::unordered_map<ShaderLanguage, StringTarget> stringTargets;
        std::unordered_map<ShaderLanguage, ShaderCodeModule::ShaderResources> reflections;
    };

    struct SlangModule
    {
        std::string name;
        std::string path;
        std::vector<uint8_t> binData;
    };

    struct SlangModuleCompileArgs
    {
        std::string moduleName;
        std::optional<std::string> modulePath;
        std::string shaderCode;
        ShaderLanguage sourceLanguage;
        std::vector<SlangModule*> deps;
    };

    struct SlangModuleReflectionArgs0
    {
        const SlangModule* module = nullptr;
        std::string entrypointName = "main";
        ShaderStage stage{};
    };

    struct SlangModuleReflectionArgs : SlangModuleReflectionArgs0
    {
        std::function<void(slang::DeclReflection*)> reflectionCallback = [](slang::DeclReflection*){};
        std::function<void(slang::ShaderReflection*)> layoutCallback = [](slang::ProgramLayout*){};
    };

    struct SlangModuleReflectShaderResourceArgs : SlangModuleReflectionArgs0
    {

    };

    struct SlangCompileArgs0
    {
        ShaderLanguage sourceLanguage;
        std::vector<ShaderLanguage> targetLanguages;
        std::vector<SlangModule*> deps;
        std::string entrypointName = "main";
        //当入口点通过[shader(...)]属性注解指定stage时，stage参数可选
        ShaderStage stage{};

        bool enableReflection = false;
        std::function<void(slang::ShaderReflection*)> layoutCallback = [](slang::ProgramLayout*){};
    };

    struct SlangCompileArgs : SlangCompileArgs0
    {
        std::string source;
    };

    struct SlangCompileArgs2 : SlangCompileArgs0
    {
        SlangModule* module = nullptr;
    };
}