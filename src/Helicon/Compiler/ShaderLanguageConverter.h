#pragma once

#include <complex.h>
#include <filesystem>
#include <optional>

#include"ShaderCodeCompiler.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>
#include <variant>
#include <Compiler/ShaderCommon.h>

namespace EmbeddedShader
{

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

	    static SlangModule slangModuleCompiler(SlangModuleCompileArgs arg);
	    static void slangModuleReflection(SlangModuleReflectionArgs arg);
	    static SlangCompileResult slangCompilerWithModules(SlangCompileArgs arg);
	    static SlangCompileResult slangCompilerWithModules(SlangCompileArgs2 arg);
	    static void testSlangModule(const std::vector<uint8_t>& moduleData);

		static std::vector<uint32_t> slangSpirvCompiler(const std::string& shaderCode, Slang::ComPtr<slang::IComponentType>& program);

		//get Reflected Bind Info
		static ShaderCodeModule::ShaderResources spirvCrossReflectedBindInfo(std::vector<uint32_t> spirv_file, ShaderLanguage targetLanguage = ShaderLanguage::GLSL, int32_t targetVersion = 330);

	    static std::vector<uint32_t> spirvLinker(const std::vector<std::vector<uint32_t>> &binaries);
		//static ShaderCodeModule::ShaderResources slangReflectedBindInfo(const std::string& shaderCode);
	    static bool isSpirvValid(const std::vector<uint32_t>& spirvCode);
	private:
	    static inline spvtools::Context spvToolContext{SPV_ENV_VULKAN_1_4};
	    static inline thread_local Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
		static void slangReflectField(slang::VariableLayoutReflection* field, std::string_view accessPath, size_t varBaseOffset, ShaderCodeModule::ShaderResources& reflection);
		static void slangReflectParameterBlock(slang::ProgramLayout* program, std::string_view uboName, ShaderCodeModule::ShaderResources& reflection);
		static void slangReflectDescriptor(slang::VariableLayoutReflection* var, int set, std::string_view name, size_t varBaseOffset, ShaderCodeModule::ShaderResources& resource);
	    static void initSlangGlobalSession();

	    static SlangCompileResult compileBySlangModule(
            const std::function<Slang::ComPtr<slang::IModule>(Slang::ComPtr<slang::ISession>)>& callback,SlangCompileArgs0& arg0);
	    static SlangCompileResult getCompileResult(std::vector<slang::TargetDesc> targetDescs, Slang::ComPtr<slang::IComponentType> slangTarget,SlangCompileArgs0& arg0, bool isLibrary);
	    static SlangStage toSlangStage(ShaderStage stage);
	//slang resource layout reflection//

	    static void slangReflectGlobalScope(slang::ProgramLayout* programLayout, ShaderCodeModule::ShaderResources& resources);
	    static void slangReflectEntryPoints(slang::ProgramLayout* programLayout, ShaderCodeModule::ShaderResources& resources);
	    static ShaderStage slangStageToShaderStage(SlangStage stage);
	    static uint32_t getScalarSizeInBytes(slang::TypeReflection::ScalarType st);
	    static void collectSlangReflection(slang::ProgramLayout* programLayout, slang::VariableLayoutReflection* varLayout, ShaderCodeModule::ShaderResources& resources, bool insidePushConstant, bool insideUniformBuffer, uint64_t baseByteOffset);
        static ShaderCodeModule::ShaderResources slangReflectBindInfo(slang::ProgramLayout* programLayout);
    public:
        static ShaderCodeModule::ShaderResources slangModuleReflectShaderResource(SlangModuleReflectShaderResourceArgs arg);
    };
}
