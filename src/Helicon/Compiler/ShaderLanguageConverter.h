#pragma once

#include "ShaderCodeCompiler.h"

#include <Compiler/ShaderCommon.h>
#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>
#include <span>
#include <variant>

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
	    // 离线工具（tools/main.cpp）用：把源码编译成可序列化 Slang module，或做声明级反射。
	    static SlangModule slangModuleCompiler(SlangModuleCompileArgs arg);
	    static void slangModuleReflection(SlangModuleReflectionArgs arg);
        static ShaderCodeModule::ShaderResources slangModuleReflectShaderResource(SlangModuleReflectShaderResourceArgs arg);

	    // Slang 低层 helper：负责 session/module/entry point 的创建和序列化。
	    // 运行时分支切换由 ShaderCodeCompiler 直接使用首次编译缓存的 session/module，
	    // 不再经过这里的整包编译 API。
	    static Slang::ComPtr<slang::IGlobalSession> getGlobalSession();
	    static Slang::ComPtr<slang::IModule> loadModule(Slang::ComPtr<slang::ISession> session, std::string_view moduleName, std::string_view moduleSource);
	    static Slang::ComPtr<slang::IModule> loadModule(Slang::ComPtr<slang::ISession> session, const SlangModule& slangModule);
	    static SlangModule convertModule(Slang::ComPtr<slang::IModule> mod);
	    static ShaderLanguage convertShaderLanguage(SlangCompileTarget target);
	    static Slang::ComPtr<slang::IComponentType> link(Slang::ComPtr<slang::ISession> session, std::span<slang::IComponentType*> composeComponents);
	    static Slang::ComPtr<slang::IEntryPoint> findEntryPoint(Slang::ComPtr<slang::IModule> mod, std::string_view name);
	    static Slang::ComPtr<slang::IEntryPoint> tryFindEntryPoint(Slang::ComPtr<slang::IModule> mod,
                                                                   std::string_view name, ShaderStage stage);
        static Slang::ComPtr<slang::ISession> createSession(
                                        const Slang::ComPtr<slang::IGlobalSession> &globalSession,
                                        std::span<slang::TargetDesc> targetDesc,
                                        std::span<slang::CompilerOptionEntry> options);
	    static Slang::ComPtr<slang::IBlob> getFinalCode(Slang::ComPtr<slang::IComponentType> program, SlangInt targetIndex, bool isLibrary = false);
	    static void fillCompileResult(SlangCompileResult &result,Slang::ComPtr<slang::IComponentType> program,SlangInt targetIndex, SlangCompileTarget dstLang,bool isNeedReflection,bool isLibrary = false);

        static ShaderCodeModule::ShaderResources slangReflection(slang::ProgramLayout* programLayout);

	private:
	    static inline thread_local Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
	    static void initSlangGlobalSession();
	    static SlangStage toSlangStage(ShaderStage stage);
    };
}
