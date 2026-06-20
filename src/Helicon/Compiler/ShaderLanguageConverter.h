#pragma once

#include <complex.h>
#include <optional>

#include"ShaderCodeCompiler.h"

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>
#include <variant>
#include <Compiler/ShaderCommon.h>

namespace EmbeddedShader
{
    //! 后续需要补全ShaderStage
    ShaderStage slangStageToShaderStage(SlangStage stage);

    struct ShaderOffset
    {
        size_t			byteOffset = 0;
        uint32_t 		arrayIndexInBindingRange = 0;
        uint32_t        bindingRangeIndex = 0;
        uint32_t        set = 0;        // RegisterSpace
        uint32_t        binding = 0;    // DescriptorTableSlot
    };
    struct ShaderCursor
    {
        ShaderOffset m_offset;
        slang::TypeLayoutReflection* m_typeLayout = nullptr;
        slang::VariableLayoutReflection* m_varLayout = nullptr;  // 必须正确传递
        size_t m_indent = 0;

        ShaderCursor field(int index) const;

        ShaderCursor element(int index) const;

        void collectBindings(EmbeddedShader::ShaderCodeModule::ShaderResources& resources,
                             const std::string& namePrefix = "") const;

        // 辅助：收集 UBO 内部字段（uniformBufferMembers）
        void collectUniformBufferMembers(EmbeddedShader::ShaderCodeModule::ShaderResources& resources,
                                         const std::string& namePrefix) const;

    private:
        // 填充语义和 location（主要用于 Varying Input/Output）
        void fillSemanticAndLocation(EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo& info) const;

        // 判断叶子节点的 BindType
        static EmbeddedShader::ShaderCodeModule::ShaderResources::BindType
        deduceLeafBindType(slang::TypeLayoutReflection* typeLayout);

        void collectPushConstantMembers(EmbeddedShader::ShaderCodeModule::ShaderResources& resources,
                                        const std::string& namePrefix) const;
    };

	struct ShaderLanguageConverter
	{
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
	    static uint32_t getScalarSizeInBytes(slang::TypeReflection::ScalarType st);
	    static void collectSlangReflection(slang::ProgramLayout* programLayout, slang::VariableLayoutReflection* varLayout, ShaderCodeModule::ShaderResources& resources, bool insidePushConstant, bool insideUniformBuffer, uint64_t baseByteOffset);
	    static void collectSlangParameterBlock(slang::TypeLayoutReflection* typeLayout, uint32_t& binding, ShaderCodeModule::ShaderResources::BindType& bindType, size_t& uboSize);
        static ShaderCodeModule::ShaderResources slangReflectBindInfo(slang::ProgramLayout* programLayout);
        static ShaderCodeModule::ShaderResources slangReflection(slang::ProgramLayout* programLayout);
    public:
        static ShaderCodeModule::ShaderResources slangModuleReflectShaderResource(SlangModuleReflectShaderResourceArgs arg);
    };
}
