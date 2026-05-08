#pragma once

#include <complex.h>
#include <filesystem>
#include <optional>

#include"ShaderCodeCompiler.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace EmbeddedShader
{
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

	struct ShaderLanguageConverter
	{
		// Compile HLSL or GLSL to SPIR-V.
		static std::vector<uint32_t> glslangSpirvCompiler(const std::string& shaderCode, ShaderLanguage inputLanguage, ShaderStage inputStage, const std::vector<std::filesystem::path>& includePaths = {}, bool isLink = true);

		//Compile SPIR-V to others
		static std::string spirvCrossConverter(std::vector<uint32_t> spirv_file, ShaderLanguage targetLanguage, int32_t targetVersion = -1);

		// 通过SPIRV-Cross IR层获取函数签名
		static std::vector<IRReflection> spirvCrossGetIRReflection(const std::vector<uint32_t>& spirv_file);

		// Compile Slang to others
		static std::string slangCompiler(std::string shaderCode, ShaderLanguage targetLanguage, Slang::ComPtr<slang::IComponentType>& program);
		static std::vector<ShaderCodeModule::ShaderResources> slangCompiler(
            const std::string &shaderCode,
            const std::vector<ShaderLanguage> &targetBinary,
            const std::vector<ShaderLanguage> &targetLanguage,
            std::vector<std::vector<uint32_t>> &binaryTargetsOutput,
            std::vector<std::string> &targetsOutput,
            bool isEnabledReflection, bool isEnabledLink = true);

		static std::vector<uint32_t> slangSpirvCompiler(const std::string& shaderCode, Slang::ComPtr<slang::IComponentType>& program);
#ifdef WIN32
		static std::vector<uint32_t> dxilCompiler(const std::string& hlslShader, ShaderStage stage);
		static std::vector<uint32_t> dxbcCompiler(const std::string& hlslShader, ShaderStage stage);
#endif

		//get Reflected Bind Info
		static ShaderCodeModule::ShaderResources spirvCrossReflectedBindInfo(std::vector<uint32_t> spirv_file, ShaderLanguage targetLanguage = ShaderLanguage::GLSL, int32_t targetVersion = 330);

	    static std::vector<uint32_t> spirvLinker(const std::vector<std::vector<uint32_t>> &binaries);
		//static ShaderCodeModule::ShaderResources slangReflectedBindInfo(const std::string& shaderCode);
	private:
	    static inline spvtools::Context spvToolContext{SPV_ENV_VULKAN_1_4};
		static void slangReflectField(slang::VariableLayoutReflection* field, std::string_view accessPath, size_t varBaseOffset, ShaderCodeModule::ShaderResources& reflection);
		static void slangReflectParameterBlock(slang::ProgramLayout* program, std::string_view uboName, ShaderCodeModule::ShaderResources& reflection);
		static void slangReflectDescriptor(slang::VariableLayoutReflection* var, int set, std::string_view name, size_t varBaseOffset, ShaderCodeModule::ShaderResources& resource);
	};
}