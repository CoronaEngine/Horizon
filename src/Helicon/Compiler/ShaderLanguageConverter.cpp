#include <iostream>

#include <SPIRV/GlslangToSpv.h>
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
// #include <glslang/Include/ResourceLimits.h>

#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>
#include <spirv_hlsl.hpp>
#include <spirv_msl.hpp>
#include <spirv_parser.hpp>

#include "ShaderLanguageConverter.h"

#include "spirv-tools/linker.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <utility>

namespace EmbeddedShader
{

	class Includer : public glslang::TShader::Includer
	{
	public:
		Includer() = default;

		IncludeResult* includeLocal(const char* includeName,const char* includerName,size_t inclusionDepth) override
		{
			std::string content;
			for (const auto& path: includePaths)
			{
				std::ifstream f(path / includeName);
				if (!f.is_open()) continue;
				content = std::string((std::istreambuf_iterator(f)),
								 std::istreambuf_iterator<char>());
			}
			if (content.empty()) return nullptr;

			auto storage = new std::string(std::move(content));
			return new IncludeResult(includeName, storage->data(), storage->size(), storage);
		}
		IncludeResult* includeSystem(const char* n, const char* i, size_t d) override
		{
			return includeLocal(n,i,d);
		}
		void releaseInclude(IncludeResult* r) override
		{
			if (!r) return;
			delete static_cast<std::string*>(r->userData);
			delete r;
		}

		std::vector<std::filesystem::path> includePaths;
	};

    ShaderStage slangStageToShaderStage(SlangStage stage)
    {
        switch (stage)
        {
        case SLANG_STAGE_VERTEX:     return ShaderStage::VertexShader;
        case SLANG_STAGE_FRAGMENT:   return ShaderStage::FragmentShader;
        case SLANG_STAGE_COMPUTE:    return ShaderStage::ComputeShader;
        case SLANG_STAGE_HULL:       return ShaderStage::VertexShader;
        case SLANG_STAGE_DOMAIN:     return ShaderStage::VertexShader;
        case SLANG_STAGE_GEOMETRY:   return ShaderStage::VertexShader;
        case SLANG_STAGE_RAY_GENERATION: return ShaderStage::ComputeShader;
        case SLANG_STAGE_INTERSECTION:   return ShaderStage::ComputeShader;
        case SLANG_STAGE_ANY_HIT:        return ShaderStage::ComputeShader;
        case SLANG_STAGE_CLOSEST_HIT:    return ShaderStage::ComputeShader;
        case SLANG_STAGE_MISS:           return ShaderStage::ComputeShader;
        case SLANG_STAGE_CALLABLE:       return ShaderStage::ComputeShader;
        case SLANG_STAGE_MESH:           return ShaderStage::VertexShader;
        case SLANG_STAGE_AMPLIFICATION:  return ShaderStage::VertexShader;
        default:                         return ShaderStage::VertexShader;
        }
    }

    ShaderCursor ShaderCursor::field(int index) const
    {
        slang::VariableLayoutReflection* field = m_typeLayout->getFieldByIndex(index);

        ShaderCursor result = *this;
        result.m_varLayout = field;  // 关键：保存字段的 VariableLayout
        result.m_typeLayout = field->getTypeLayout();
        result.m_offset.byteOffset += field->getOffset();
        result.m_offset.bindingRangeIndex += m_typeLayout->getFieldBindingRangeOffset(index);

        // 关键修正：对 DescriptorTableSlot 类别，offset 用 getOffset，space 用 getBindingSpace
        result.m_offset.binding += field->getOffset(slang::ParameterCategory::DescriptorTableSlot);
        result.m_offset.set += field->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot);

        result.m_indent = m_indent + 1;
        return result;
    }

    ShaderCursor ShaderCursor::element(int index) const
    {
        slang::TypeLayoutReflection* elementTypeLayout = m_typeLayout->getElementTypeLayout();

        ShaderCursor result = *this;
        result.m_typeLayout = elementTypeLayout;
        result.m_offset.byteOffset += index * elementTypeLayout->getStride();
        result.m_offset.arrayIndexInBindingRange *= m_typeLayout->getElementCount();
        result.m_offset.arrayIndexInBindingRange += index;
        // 数组元素共享父级的 set/binding，不额外累加
        return result;
    }

    void ShaderCursor::collectBindings(EmbeddedShader::ShaderCodeModule::ShaderResources& resources, const std::string& namePrefix) const
    {
        auto kind = m_typeLayout->getKind();

        // === ConstantBuffer ===
        // 本身生成 uniformBuffers 条目，内部字段生成 uniformBufferMembers
        if (kind == slang::TypeReflection::Kind::ConstantBuffer && m_typeLayout->getParameterCategory() != slang::ParameterCategory::PushConstantBuffer)
        {
            // 先获取元素布局（用于后续递归，也用于取正确的大小）
            auto elementVarLayout = m_typeLayout->getElementVarLayout();
            auto elementTypeLayout = elementVarLayout->getTypeLayout();  // 这是 global_ubo_struct

            EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo info;
            info.variateName = namePrefix;
            info.typeName = m_typeLayout->getName() ? m_typeLayout->getName() : "ConstantBuffer";
            info.set = m_offset.set;
            info.binding = m_offset.binding;
            info.byteOffset = m_offset.byteOffset;

            // 修正：取元素类型的大小，而不是 ConstantBuffer 包装器的大小
            info.typeSize = static_cast<uint32_t>(elementTypeLayout->getSize());

            info.elementCount = 1;
            info.bindType = EmbeddedShader::ShaderCodeModule::ShaderResources::uniformBuffers;
            fillSemanticAndLocation(info);
            resources.bindInfoPool.push_back(info);

            // 记录全局 UBO 元数据
            if (resources.uniformBufferSize == 0)
            {
                resources.uniformBufferSize = info.typeSize;  // 现在正确了：192
                resources.uniformBufferName = info.variateName;
            }

            // 递归收集内部字段（使用 elementTypeLayout）
            ShaderCursor inner = *this;
            inner.m_varLayout = elementVarLayout;
            inner.m_typeLayout = elementTypeLayout;
            inner.m_offset.byteOffset += elementVarLayout->getOffset(slang::ParameterCategory::Uniform);
            inner.m_offset.binding += elementVarLayout->getOffset(slang::ParameterCategory::DescriptorTableSlot);
            inner.m_offset.set += elementVarLayout->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot);
            inner.collectUniformBufferMembers(resources, namePrefix);
            return;
        }

        // === ParameterBlock ===
        // 不单独生成条目，展开内部元素（内部资源继承 ParameterBlock 的 set）
        if (kind == slang::TypeReflection::Kind::ParameterBlock)
        {
            auto elementVarLayout = m_typeLayout->getElementVarLayout();
            ShaderCursor inner = *this;
            inner.m_varLayout = elementVarLayout;
            inner.m_typeLayout = elementVarLayout->getTypeLayout();
            inner.m_offset.byteOffset += elementVarLayout->getOffset(slang::ParameterCategory::Uniform);
            inner.m_offset.binding += elementVarLayout->getOffset(slang::ParameterCategory::DescriptorTableSlot);
            inner.m_offset.set += elementVarLayout->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot);
            inner.collectBindings(resources, namePrefix);
            return;
        }

        // === PushConstantBuffer ===
        if (m_typeLayout->getParameterCategory() == slang::ParameterCategory::PushConstantBuffer)
        {
            // 先获取元素布局（PushConstant 内部是 struct，需要取元素大小）
            auto elementVarLayout = m_typeLayout->getElementVarLayout();
            auto elementTypeLayout = elementVarLayout->getTypeLayout();

            EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo info;
            info.variateName = namePrefix;
            info.typeName = m_typeLayout->getName() ? m_typeLayout->getName() : "PushConstant";
            info.set = m_offset.set;
            info.binding = m_offset.binding;
            info.byteOffset = m_offset.byteOffset;

            // 修正：取元素类型的大小（和 ConstantBuffer 一样）
            info.typeSize = static_cast<uint32_t>(elementTypeLayout->getSize());

            info.elementCount = 1;
            info.bindType = EmbeddedShader::ShaderCodeModule::ShaderResources::pushConstantMembers;
            fillSemanticAndLocation(info);
            resources.bindInfoPool.push_back(info);

            // 记录 Push Constant 元数据（不是 UBO！）
            if (resources.pushConstantSize == 0)
            {
                resources.pushConstantSize = info.typeSize;
                resources.pushConstantName = info.variateName;
            }

            // 递归收集内部字段
            ShaderCursor inner = *this;
            inner.m_varLayout = elementVarLayout;
            inner.m_typeLayout = elementTypeLayout;
            inner.m_offset.byteOffset += elementVarLayout->getOffset(slang::ParameterCategory::Uniform);
            inner.m_offset.binding += elementVarLayout->getOffset(slang::ParameterCategory::DescriptorTableSlot);
            inner.m_offset.set += elementVarLayout->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot);

            // Push Constant 内部字段也标记为 pushConstantMembers
            inner.collectPushConstantMembers(resources, namePrefix);
            return;
        }

        // === Struct ===
        if (kind == slang::TypeReflection::Kind::Struct)
        {
            for (int i = 0; i < m_typeLayout->getFieldCount(); ++i)
            {
                auto fieldVar = m_typeLayout->getFieldByIndex(i);
                std::string fieldName = fieldVar->getName() ? fieldVar->getName() : ("field_" + std::to_string(i));
                std::string fullName = namePrefix.empty() ? fieldName : (namePrefix + "." + fieldName);
                field(i).collectBindings(resources, fullName);
            }
            return;
        }

        // === Array ===
        if (kind == slang::TypeReflection::Kind::Array)
        {
            uint64_t elemCount = m_typeLayout->getElementCount();
            auto elemTypeLayout = m_typeLayout->getElementTypeLayout();

            // 如果元素是叶子资源，合并为一个条目，记录 elementCount
            auto elemBindType = deduceLeafBindType(elemTypeLayout);
            if (elemBindType != EmbeddedShader::ShaderCodeModule::ShaderResources::none)
            {
                EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo info;
                info.variateName = namePrefix;
                info.typeName = elemTypeLayout->getName() ? elemTypeLayout->getName() : "unknown";
                info.set = m_offset.set;
                info.binding = m_offset.binding;
                info.byteOffset = m_offset.byteOffset;
                info.typeSize = static_cast<uint32_t>(elemTypeLayout->getSize());
                info.elementCount = elemCount;
                info.bindType = elemBindType;
                fillSemanticAndLocation(info);
                resources.bindInfoPool.push_back(info);
                return;
            }

            // 否则递归每个元素（Struct 数组等）
            for (int i = 0; i < static_cast<int>(elemCount); ++i)
            {
                std::string elemName = namePrefix + "[" + std::to_string(i) + "]";
                element(i).collectBindings(resources, elemName);
            }
            return;
        }

        // === 叶子节点（Texture, Sampler, Buffer, Varying 等）===
        auto bindType = deduceLeafBindType(m_typeLayout);
        if (bindType != EmbeddedShader::ShaderCodeModule::ShaderResources::none)
        {
            EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo info;
            info.variateName = namePrefix;
            info.typeName = m_typeLayout->getName() ? m_typeLayout->getName() : "unknown";
            info.set = m_offset.set;
            info.binding = m_offset.binding;
            info.byteOffset = m_offset.byteOffset;
            info.typeSize = static_cast<uint32_t>(m_typeLayout->getSize());
            info.elementCount = 1;
            info.bindType = bindType;
            fillSemanticAndLocation(info);
            resources.bindInfoPool.push_back(info);
            return;
        }
    }

    void ShaderCursor::collectUniformBufferMembers(EmbeddedShader::ShaderCodeModule::ShaderResources& resources, const std::string& namePrefix) const
    {
        auto kind = m_typeLayout->getKind();

        if (kind == slang::TypeReflection::Kind::Struct)
        {
            for (int i = 0; i < m_typeLayout->getFieldCount(); ++i)
            {
                auto fieldVar = m_typeLayout->getFieldByIndex(i);
                std::string fieldName = fieldVar->getName() ? fieldVar->getName() : ("field_" + std::to_string(i));
                std::string fullName = namePrefix.empty() ? fieldName : (namePrefix + "." + fieldName);
                field(i).collectUniformBufferMembers(resources, fullName);
            }
            return;
        }

        if (kind == slang::TypeReflection::Kind::Array)
        {
            uint64_t elemCount = m_typeLayout->getElementCount();
            for (int i = 0; i < static_cast<int>(elemCount); ++i)
            {
                std::string elemName = namePrefix + "[" + std::to_string(i) + "]";
                element(i).collectUniformBufferMembers(resources, elemName);
            }
            return;
        }

        // 普通数据成员
        EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo info;
        info.variateName = namePrefix;
        info.typeName = m_typeLayout->getName() ? m_typeLayout->getName() : "unknown";
        info.byteOffset = m_offset.byteOffset;
        info.typeSize = static_cast<uint32_t>(m_typeLayout->getSize());
        info.elementCount = 1;
        info.bindType = EmbeddedShader::ShaderCodeModule::ShaderResources::uniformBufferMembers;
        info.set = m_offset.set;
        info.binding = m_offset.binding;
        fillSemanticAndLocation(info);
        resources.bindInfoPool.push_back(info);
    }

    void ShaderCursor::fillSemanticAndLocation(EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo& info) const
    {
        if (!m_varLayout) return;

        const char* sem = m_varLayout->getSemanticName();
        if (sem)
        {
            info.semantic = sem;
            info.location = m_varLayout->getSemanticIndex();
        }

        if (info.bindType == EmbeddedShader::ShaderCodeModule::ShaderResources::stageInputs)
            info.location = m_varLayout->getOffset(slang::ParameterCategory::VaryingInput);
        else if (info.bindType == EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs)
            info.location = m_varLayout->getOffset(slang::ParameterCategory::VaryingOutput);
    }

    EmbeddedShader::ShaderCodeModule::ShaderResources::BindType ShaderCursor::deduceLeafBindType(slang::TypeLayoutReflection* typeLayout)
    {
        int rangeCount = typeLayout->getBindingRangeCount();
        if (rangeCount > 0)
        {
            auto rangeType = typeLayout->getBindingRangeType(0);
            switch (rangeType)
            {
            case slang::BindingType::Texture: return EmbeddedShader::ShaderCodeModule::ShaderResources::texture;
            case slang::BindingType::Sampler: return EmbeddedShader::ShaderCodeModule::ShaderResources::sampler;
            case slang::BindingType::CombinedTextureSampler: return EmbeddedShader::ShaderCodeModule::ShaderResources::sampledImages;
            case slang::BindingType::ConstantBuffer: return EmbeddedShader::ShaderCodeModule::ShaderResources::uniformBuffers;
            case slang::BindingType::TypedBuffer: return EmbeddedShader::ShaderCodeModule::ShaderResources::rawBuffer;
            case slang::BindingType::RawBuffer: return EmbeddedShader::ShaderCodeModule::ShaderResources::rawBuffer;
            case slang::BindingType::MutableTexture: return EmbeddedShader::ShaderCodeModule::ShaderResources::storageTexture;
            case slang::BindingType::MutableTypedBuffer:
            case slang::BindingType::MutableRawBuffer: return EmbeddedShader::ShaderCodeModule::ShaderResources::storageBuffer;
            default: break;
            }
        }

        auto cat = typeLayout->getParameterCategory();
        if (cat == slang::ParameterCategory::VaryingInput)
            return EmbeddedShader::ShaderCodeModule::ShaderResources::stageInputs;
        if (cat == slang::ParameterCategory::VaryingOutput)
            return EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs;
        if (cat == slang::ParameterCategory::PushConstantBuffer)
            return EmbeddedShader::ShaderCodeModule::ShaderResources::pushConstantMembers;

        return EmbeddedShader::ShaderCodeModule::ShaderResources::none;
    }

    void ShaderCursor::collectPushConstantMembers(EmbeddedShader::ShaderCodeModule::ShaderResources& resources, const std::string& namePrefix) const
    {
        auto kind = m_typeLayout->getKind();

        if (kind == slang::TypeReflection::Kind::Struct)
        {
            for (int i = 0; i < m_typeLayout->getFieldCount(); ++i)
            {
                auto fieldVar = m_typeLayout->getFieldByIndex(i);
                std::string fieldName = fieldVar->getName() ? fieldVar->getName() : ("field_" + std::to_string(i));
                std::string fullName = namePrefix.empty() ? fieldName : (namePrefix + "." + fieldName);
                field(i).collectPushConstantMembers(resources, fullName);
            }
            return;
        }

        if (kind == slang::TypeReflection::Kind::Array)
        {
            uint64_t elemCount = m_typeLayout->getElementCount();
            for (int i = 0; i < static_cast<int>(elemCount); ++i)
            {
                std::string elemName = namePrefix + "[" + std::to_string(i) + "]";
                element(i).collectPushConstantMembers(resources, elemName);
            }
            return;
        }

        // 普通数据成员
        EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo info;
        info.variateName = namePrefix;
        info.typeName = m_typeLayout->getName() ? m_typeLayout->getName() : "unknown";
        info.byteOffset = m_offset.byteOffset;
        info.typeSize = static_cast<uint32_t>(m_typeLayout->getSize());
        info.elementCount = 1;
        info.bindType = EmbeddedShader::ShaderCodeModule::ShaderResources::pushConstantMembers;
        info.set = m_offset.set;
        info.binding = m_offset.binding;
        fillSemanticAndLocation(info);
        resources.bindInfoPool.push_back(info);
    }

	std::vector<uint32_t> ShaderLanguageConverter::glslangSpirvCompiler(
		const std::string& shaderCode, ShaderLanguage inputLanguage, ShaderStage inputStage, const std::vector<std::filesystem::
		path>& includePaths, bool isLink)
	{
		// GLSL version is default by 460
		// Higher versions are compatible with lower versions
		// Version in HLSL is disabled
		Includer a;
		std::vector<uint32_t> resultSpirvCode;

		glslang::EShSource shaderLang;
		switch (inputLanguage)
		{
			case ShaderLanguage::GLSL:
				shaderLang = glslang::EShSourceGlsl;
				break;
			case ShaderLanguage::HLSL:
				shaderLang = glslang::EShSourceHlsl;
				break;
			default:
				return resultSpirvCode;
		}

		EShLanguage stage = EShLangVertex;
		switch (inputStage)
		{
			case ShaderStage::VertexShader:
				stage = EShLangVertex;
				break;
			case ShaderStage::FragmentShader:
				stage = EShLangFragment;
				break;
			case ShaderStage::ComputeShader:
				stage = EShLangCompute;
				break;
			default:
				return resultSpirvCode;
		}

		std::vector<const char*> shaderSources;
		shaderSources.push_back(shaderCode.c_str());

		glslang::InitializeProcess();

		glslang::TShader shader(stage);
		shader.setStrings(shaderSources.data(), 1);
		shader.setEnvInput(shaderLang, stage, glslang::EShClientVulkan, 460);
		shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_4);
		shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_6);

		//shader.setSourceEntryPoint("__no_entrypoint");

		Includer includer;
		includer.includePaths = includePaths;
		if (!shader.parse(GetDefaultResources(), 460, false, EShMsgDefault, includer))
		{
			std::cerr << shader.getInfoLog();
			return resultSpirvCode;
		}

		glslang::TIntermediate* intermediate = nullptr;
	    spv::SpvBuildLogger logger;
		if (isLink)
		{
		    glslang::TProgram program;
			program.addShader(&shader);
			if (!program.link(EShMsgVulkanRules))
			{
				std::cerr << program.getInfoLog();
				return resultSpirvCode;
			}

			if (!program.buildReflection(EShReflectionAllBlockVariables | EShReflectionIntermediateIO))
			{
				// std::cout << "build Reflection Error" << std::endl;
			} else
			{
				// std::cout << program.getNumLiveUniformBlocks() << std::endl;
				// program.dumpReflection();
			}

			intermediate = program.getIntermediate(stage);

		    glslang::GlslangToSpv(*intermediate, resultSpirvCode,&logger);

		}
	    else
		{
		    intermediate = shader.getIntermediate();

		    glslang::GlslangToSpv(*intermediate, resultSpirvCode,&logger);
		}
	    auto log = logger.getAllMessages();
	    if (!log.empty()) {
	        std::cerr << "SpvBuildLogger: " << log << "\n";
	    }

		glslang::FinalizeProcess();

		return resultSpirvCode;
	}

	std::string ShaderLanguageConverter::spirvCrossConverter(std::vector<uint32_t> spirv_file,
	                                                         ShaderLanguage targetLanguage, int32_t targetVersion)
	{
		std::string resultCode = "";

		try
		{
			switch (targetLanguage)
			{
				case ShaderLanguage::GLSL:
					// case ShaderLanguage::ESSL:
				{
					spirv_cross::CompilerGLSL compiler(spirv_file);

					spirv_cross::CompilerGLSL::Options opts = compiler.get_common_options();
					opts.enable_420pack_extension = false;
					opts.version = 460;
					opts.vulkan_semantics = true;
					opts.es = false;
					compiler.set_common_options(opts);

					resultCode = compiler.compile();
					break;
				}
				case ShaderLanguage::HLSL:
				{
					spirv_cross::CompilerHLSL compiler(spirv_file);

					spirv_cross::CompilerHLSL::Options opts = compiler.get_hlsl_options();
					opts.shader_model = 67;
					compiler.set_hlsl_options(opts);

					resultCode = compiler.compile();
					break;
				}
				default:
					break;
			}
		} catch (const spirv_cross::CompilerError& error)
		{
			std::cout << error.what();
		}

		return resultCode;
	}

	// 辅助函数：将SPIRType转换为类型名字符串
	static std::string spirTypeToString(const spirv_cross::Compiler& compiler, const spirv_cross::SPIRType& type)
	{
		std::string result;

		switch (type.basetype)
		{
			case spirv_cross::SPIRType::Void:
				result = "void";
				break;
			case spirv_cross::SPIRType::Boolean:
				result = "bool";
				break;
			case spirv_cross::SPIRType::SByte:
				result = "int8_t";
				break;
			case spirv_cross::SPIRType::UByte:
				result = "uint8_t";
				break;
			case spirv_cross::SPIRType::Short:
				result = "int16_t";
				break;
			case spirv_cross::SPIRType::UShort:
				result = "uint16_t";
				break;
			case spirv_cross::SPIRType::Int:
				result = "int";
				break;
			case spirv_cross::SPIRType::UInt:
				result = "uint";
				break;
			case spirv_cross::SPIRType::Int64:
				result = "int64_t";
				break;
			case spirv_cross::SPIRType::UInt64:
				result = "uint64_t";
				break;
			case spirv_cross::SPIRType::Half:
				result = "half";
				break;
			case spirv_cross::SPIRType::Float:
				result = "float";
				break;
			case spirv_cross::SPIRType::Double:
				result = "double";
				break;
			case spirv_cross::SPIRType::Struct:
				result = compiler.get_name(type.self);
				if (result.empty())
					result = "struct_" + std::to_string(type.self);
				break;
			case spirv_cross::SPIRType::Image:
			{
				const auto& imgTexelType = compiler.get_type(type.image.type);
				std::string prefix;
				if (imgTexelType.basetype == spirv_cross::SPIRType::Int ||
				    imgTexelType.basetype == spirv_cross::SPIRType::Short ||
				    imgTexelType.basetype == spirv_cross::SPIRType::SByte)
					prefix = "i";
				else if (imgTexelType.basetype == spirv_cross::SPIRType::UInt ||
				         imgTexelType.basetype == spirv_cross::SPIRType::UShort ||
				         imgTexelType.basetype == spirv_cross::SPIRType::UByte)
					prefix = "u";

				switch (type.image.dim)
				{
					case spv::Dim2D:   result = prefix + (type.image.arrayed ? "image2DArray" : "image2D"); break;
					case spv::Dim3D:   result = prefix + "image3D"; break;
					case spv::DimCube: result = prefix + (type.image.arrayed ? "imageCubeArray" : "imageCube"); break;
					case spv::Dim1D:   result = prefix + "image1D"; break;
					default:           result = prefix + "image2D"; break;
				}
				break;
			}
			case spirv_cross::SPIRType::SampledImage:
			{
				const auto& imgTexelType = compiler.get_type(type.image.type);
				std::string prefix;
				if (imgTexelType.basetype == spirv_cross::SPIRType::Int ||
				    imgTexelType.basetype == spirv_cross::SPIRType::Short ||
				    imgTexelType.basetype == spirv_cross::SPIRType::SByte)
					prefix = "i";
				else if (imgTexelType.basetype == spirv_cross::SPIRType::UInt ||
				         imgTexelType.basetype == spirv_cross::SPIRType::UShort ||
				         imgTexelType.basetype == spirv_cross::SPIRType::UByte)
					prefix = "u";

				switch (type.image.dim)
				{
					case spv::Dim2D:   result = prefix + (type.image.arrayed ? "sampler2DArray" : "sampler2D"); break;
					case spv::Dim3D:   result = prefix + "sampler3D"; break;
					case spv::DimCube: result = prefix + (type.image.arrayed ? "samplerCubeArray" : "samplerCube"); break;
					case spv::Dim1D:   result = prefix + "sampler1D"; break;
					default:           result = prefix + "sampler2D"; break;
				}
				break;
			}
			case spirv_cross::SPIRType::Sampler:
				result = "sampler";
				break;
			case spirv_cross::SPIRType::AccelerationStructure:
				result = "acceleration_structure";
				break;
			case spirv_cross::SPIRType::RayQuery:
				result = "ray_query";
				break;
			default:
				result = "unknown";
				break;
		}

		// 处理向量类型
		if (type.vecsize > 1 && type.columns == 1)
		{
			result = result + std::to_string(type.vecsize);
		}
		// 处理矩阵类型
		else if (type.columns > 1)
		{
			result = result + std::to_string(type.columns) + "x" + std::to_string(type.vecsize);
		}

		// 处理数组类型
		for (auto& dim: type.array)
		{
			if (dim == 0)
				result += "[]";
			else
				result += "[" + std::to_string(dim) + "]";
		}

		return result;
	}

	static uint64_t normalizeReflectedElementCount(size_t elementCount)
	{
		if (elementCount == SLANG_UNBOUNDED_SIZE || elementCount == SLANG_UNKNOWN_SIZE ||
		    elementCount == static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
		    elementCount == static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
		{
			return 0;
		}

		return static_cast<uint64_t>(elementCount);
	}

	static bool spirvTypeContainsStorageImage(const spirv_cross::Compiler& compiler,
	                                          const spirv_cross::SPIRType& type,
	                                          uint32_t depth = 0)
	{
		if (depth > 16)
			return false;

		if ((type.basetype == spirv_cross::SPIRType::Image ||
		     type.basetype == spirv_cross::SPIRType::SampledImage) &&
		    type.image.sampled == 2)
		{
			return true;
		}

		if (type.basetype == spirv_cross::SPIRType::Struct)
		{
			for (uint32_t memberTypeId : type.member_types)
			{
				if (spirvTypeContainsStorageImage(compiler, compiler.get_type(memberTypeId), depth + 1))
					return true;
			}
		}

		return false;
	}

	static ShaderCodeModule::ShaderResources::BindType bindTypeFromSlangBinding(slang::BindingType bindingType)
	{
		switch (bindingType)
		{
			case slang::BindingType::ConstantBuffer:
				return ShaderCodeModule::ShaderResources::uniformBuffers;
			case slang::BindingType::Texture:
				return ShaderCodeModule::ShaderResources::texture;
			case slang::BindingType::Sampler:
				return ShaderCodeModule::ShaderResources::sampler;
			case slang::BindingType::MutableTexture:
				return ShaderCodeModule::ShaderResources::storageTexture;
			case slang::BindingType::MutableRawBuffer:
			case slang::BindingType::MutableTypedBuffer:
				return ShaderCodeModule::ShaderResources::storageBuffer;
			case slang::BindingType::RawBuffer:
				return ShaderCodeModule::ShaderResources::rawBuffer;
			case slang::BindingType::CombinedTextureSampler:
				return ShaderCodeModule::ShaderResources::sampledImages;
			default:
				return ShaderCodeModule::ShaderResources::none;
		}
	}

	static bool isMutableResourceAccess(SlangResourceAccess access)
	{
		switch (access)
		{
			case SLANG_RESOURCE_ACCESS_READ_WRITE:
			case SLANG_RESOURCE_ACCESS_RASTER_ORDERED:
			case SLANG_RESOURCE_ACCESS_APPEND:
			case SLANG_RESOURCE_ACCESS_CONSUME:
			case SLANG_RESOURCE_ACCESS_WRITE:
				return true;
			default:
				return false;
		}
	}

	static ShaderCodeModule::ShaderResources::BindType bindTypeFromSlangResourceAccess(
		slang::TypeLayoutReflection* descriptorTypeLayout)
	{
		if (!descriptorTypeLayout ||
		    descriptorTypeLayout->getKind() != slang::TypeReflection::Kind::Resource)
		{
			return ShaderCodeModule::ShaderResources::none;
		}

		const SlangResourceShape shape = descriptorTypeLayout->getResourceShape();
		const SlangResourceShape baseShape = static_cast<SlangResourceShape>(shape & SLANG_RESOURCE_BASE_SHAPE_MASK);
		if (!isMutableResourceAccess(descriptorTypeLayout->getResourceAccess()))
			return ShaderCodeModule::ShaderResources::none;

		if (baseShape == SLANG_STRUCTURED_BUFFER || baseShape == SLANG_BYTE_ADDRESS_BUFFER)
			return ShaderCodeModule::ShaderResources::storageBuffer;

		return ShaderCodeModule::ShaderResources::storageTexture;
	}

	static ShaderCodeModule::ShaderResources::BindType bindTypeFromSlangLayout(
		slang::TypeLayoutReflection* typeLayout,
		slang::TypeLayoutReflection* descriptorTypeLayout)
	{
		for (slang::TypeLayoutReflection* layout : { typeLayout, descriptorTypeLayout })
		{
			if (!layout)
				continue;

			const SlangInt bindingRangeCount = layout->getBindingRangeCount();
			for (SlangInt i = 0; i < bindingRangeCount; ++i)
			{
				auto bindType = bindTypeFromSlangBinding(layout->getBindingRangeType(i));
				if (bindType == ShaderCodeModule::ShaderResources::texture)
				{
					auto accessBindType = bindTypeFromSlangResourceAccess(descriptorTypeLayout);
					if (accessBindType != ShaderCodeModule::ShaderResources::none)
						return accessBindType;
				}
				if (bindType != ShaderCodeModule::ShaderResources::none)
					return bindType;
			}
		}

		return bindTypeFromSlangResourceAccess(descriptorTypeLayout);
	}

	std::vector<IRReflection> ShaderLanguageConverter::spirvCrossGetIRReflection(
		const std::vector<uint32_t>& spirv_file)
	{
		std::vector<IRReflection> irReflections;

		if (spirv_file.empty())
			return irReflections;

		try
		{
			// 使用spirv_cross::Parser来解析SPIR-V并获取IR
			spirv_cross::Parser parser(spirv_file);
			parser.parse();

			// 获取解析后的IR
			spirv_cross::ParsedIR& ir = parser.get_parsed_ir();

			// 使用Compiler来获取更多信息（类型名称等）
			spirv_cross::Compiler compiler(ir);

			// 获取入口点列表
			/*           auto entryPoints = compiler.get_entry_points_and_stages();
			           std::set<std::string> entryPointNames;
			           for (const auto& ep : entryPoints)
			           {
			               entryPointNames.insert(ep.name);
			           }*/

			// 重新解析以获取IR（因为之前的ir被move了）
			// spirv_cross::Parser parser2(spirv_file);
			// parser2.parse();

			// 遍历所有ID，查找函数
			for (size_t i = 0; i < ir.ids.size(); ++i)
			{
				auto& idHolder = ir.ids[i];
				auto type = idHolder.get_type();
				if (type == spirv_cross::TypeFunction)
				{
					const auto& func = idHolder.get<spirv_cross::SPIRFunction>();
					FunctionSignature sig;

					// 获取函数名称
					auto nameIt = ir.meta.find(func.self);
					if (nameIt != ir.meta.end() && !nameIt->second.decoration.alias.empty())
						sig.name = nameIt->second.decoration.alias.substr(0,nameIt->second.decoration.alias.find('('));
					else continue;

					// 获取返回类型
					const auto& funcType = ir.ids[func.function_type].get<spirv_cross::SPIRFunctionPrototype>();
					sig.returnTypeId = funcType.return_type;

					if (sig.returnTypeId != 0 && ir.ids[sig.returnTypeId].get_type() == spirv_cross::TypeType)
					{
						const auto& returnType = ir.ids[sig.returnTypeId].get<spirv_cross::SPIRType>();
						sig.returnTypeName = spirTypeToString(compiler, returnType);
					} else
					{
						sig.returnTypeName = "void";
					}

					// 获取函数参数
					for (size_t j = 0; j < func.arguments.size(); ++j)
					{
						const auto& arg = func.arguments[j];
						VariableInfo param;

						// 获取参数名称
						auto argNameIt = ir.meta.find(arg.id);
						if (argNameIt != ir.meta.end() && !argNameIt->second.decoration.alias.empty())
							param.name = argNameIt->second.decoration.alias;
						else
							param.name = "param_" + std::to_string(j);

						// 获取参数类型
						param.typeId = arg.type;
						if (ir.ids[arg.type].get_type() == spirv_cross::TypeType)
						{
							const auto& argType = ir.ids[arg.type].get<spirv_cross::SPIRType>();
							param.typeName = spirTypeToString(compiler, argType);
						} else
						{
							param.typeName = "unknown";
						}

						sig.parameters.push_back(std::move(param));
					}

					IRReflection reflection;
					reflection.type = IRReflection::Type::FunctionSignature;
					reflection.info = std::move(sig);

					irReflections.push_back(std::move(reflection));
				}

				if (type == spirv_cross::TypeType)
				{
					const auto& spvType = idHolder.get<spirv_cross::SPIRType>();
					if (spvType.basetype != spirv_cross::SPIRType::Struct) continue;

					StructInfo inf;
					auto it = ir.meta.find(spvType.self);
					if (it != ir.meta.end() && !it->second.decoration.alias.empty())
						inf.name = it->second.decoration.alias;
					else continue;

					inf.members.resize(spvType.member_types.size());
					for (size_t memberIndex = 0; memberIndex < spvType.member_types.size(); ++memberIndex)
					{
						auto& member = inf.members[memberIndex];
						member.name = compiler.get_member_name(spvType.self, memberIndex);
						member.typeId = spvType.member_types[memberIndex];
						member.typeName = spirTypeToString(compiler, compiler.get_type(member.typeId));
					}

					IRReflection reflection;
					reflection.type = IRReflection::Type::Struct;
					reflection.info = std::move(inf);

					irReflections.push_back(std::move(reflection));
				}
			}
		} catch (const spirv_cross::CompilerError& error)
		{
			std::cerr << "SPIRV-Cross error while getting function signatures: " << error.what() << std::endl;
		}

		//查重
		for (auto comp1 = irReflections.begin(); comp1 != irReflections.end(); ++comp1)
		{
			for (auto comp2 = comp1 + 1; comp2 != irReflections.end();)
			{
				if (comp1->type == comp2->type)
				{
					switch (comp1->type)
					{
						case IRReflection::Type::Unknown:
							break;
						case IRReflection::Type::FunctionSignature:
						{
							auto& sig1 = std::get<FunctionSignature>(comp1->info);
							auto& sig2 = std::get<FunctionSignature>(comp2->info);
							if (sig1.name == sig2.name)
							{
								comp2 = irReflections.erase(comp2);
								continue;
							}
						}
						break;
						case IRReflection::Type::Struct:
						{
							auto& struct1 = std::get<StructInfo>(comp1->info);
							auto& struct2 = std::get<StructInfo>(comp2->info);
							if (struct1.name == struct2.name)
							{
								comp2 = irReflections.erase(comp2);
								continue;
							}
						}
						break;
					}
				}
				++comp2;
			}
		}

		return irReflections;
	}

	void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
	{
		if (diagnosticsBlob != nullptr)
		{
			std::cout << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
		}
	}

	std::string ShaderLanguageConverter::slangCompiler(std::string shaderCode, ShaderLanguage targetLanguage,
	                                                   Slang::ComPtr<slang::IComponentType>& program)
	{
		std::string result;
		Slang::ComPtr<slang::IGlobalSession> slangGlobalSession;
		slang::createGlobalSession(slangGlobalSession.writeRef());
		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc targetDesc = {};
		switch (targetLanguage)
		{
			case ShaderLanguage::GLSL:
				// case ShaderLanguage::ESSL:
			{
				targetDesc.format = SLANG_GLSL;
				slangGlobalSession->findProfile("glsl_460");
				break;
			}
			case ShaderLanguage::HLSL:
			{
				targetDesc.format = SLANG_HLSL;
				slangGlobalSession->findProfile("sm_6_7");
				break;
			}
			// case ShaderLanguage::SpirV: {
			//     targetDesc.format = SLANG_SPIRV;
			//     slangGlobalSession->findProfile("spirv_1_6");
			//     targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
			//     break;
			// }
			// case ShaderLanguage::MSL:
			//	targetDesc.format = SLANG_METAL; break;
			// case ShaderLanguage::DXIL:
			//	targetDesc.format = SLANG_DXIL; break;
			default:
				return result;
				break;
		}
		sessionDesc.targets = &targetDesc;
		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
		sessionDesc.targetCount = 1;
		Slang::ComPtr<slang::ISession> session;
		(slangGlobalSession->createSession(sessionDesc, session.writeRef()));
		slang::IModule* slangModule = nullptr; {
			Slang::ComPtr<slang::IBlob> diagnosticBlob;
			// slangModule = session->loadModule(shaderCode.c_str(), diagnosticBlob.writeRef());
			slangModule = session->loadModuleFromSourceString(
				std::to_string(std::hash<std::string>()(shaderCode)).c_str(), "", shaderCode.c_str(),
				diagnosticBlob.writeRef());
		}
		Slang::ComPtr<slang::IEntryPoint> entryPoint;
		slangModule->findEntryPointByName("main", entryPoint.writeRef());
		std::vector<slang::IComponentType*> componentTypes;
		componentTypes.push_back(slangModule);
		componentTypes.push_back(entryPoint);
		Slang::ComPtr<slang::IComponentType> composedProgram; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = session->createCompositeComponentType(
				componentTypes.data(), componentTypes.size(), composedProgram.writeRef(), diagnosticsBlob.writeRef());
		}
		Slang::ComPtr<slang::IComponentType> linkedProgram; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = composedProgram->link(
				linkedProgram.writeRef(),
				diagnosticsBlob.writeRef());
			if (SLANG_FAILED(result))
				return {};
		}
		Slang::ComPtr<slang::IBlob> spirvCode; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = linkedProgram->getEntryPointCode(0, 0, spirvCode.writeRef(),
			                                                      diagnosticsBlob.writeRef());
			if (SLANG_FAILED(result))
				return {};
		}
		result.resize(spirvCode->getBufferSize() / sizeof(char));
		memcpy(result.data(), spirvCode->getBufferPointer(), spirvCode->getBufferSize());
		program = linkedProgram;
		return result;
	}

	std::vector<ShaderCodeModule::ShaderResources> ShaderLanguageConverter::slangCompiler(const std::string &shaderCode,
                                                                                          const std::vector<ShaderLanguage> &targetBinary, const std::vector<ShaderLanguage> &targetLanguage,
                                                                                          std::vector<std::vector<uint32_t>> &binaryTargetsOutput,
                                                                                          std::vector<std::string> &targetsOutput, bool isEnabledReflection, bool isEnabledLink)
	{
	    if (targetBinary.empty() && targetLanguage.empty())
	    {
	        throw std::logic_error("No target language specified for Slang compilation.");
	    }

		initSlangGlobalSession();
		Slang::ComPtr<slang::IGlobalSession> globalSession = slangGlobalSession;

		slang::SessionDesc sessionDesc = {};

	    std::vector<slang::TargetDesc> targets(targetLanguage.size() + targetBinary.size());

	    for (size_t i = 0; i < targetBinary.size(); ++i)
	    {
	        auto& target = targets[i];
	        auto language = targetBinary[i];
	        switch (language)
	        {
	        case ShaderLanguage::SpirV:
	            target.format = SLANG_SPIRV;
	            target.profile = globalSession->findProfile("spirv_1_6");
	            target.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;
	            break;
	        case ShaderLanguage::DXIL:
	            target.format = SLANG_DXIL;
	            target.profile = globalSession->findProfile("sm_6_6");
	            break;
	        case ShaderLanguage::DXBC:
	            target.format = SLANG_DXBC;
	            break;
	        default:
	            throw std::logic_error("Unsupported binary target for Slang compilation.");
	        }
	    }

	    for (size_t i = 0; i < targetLanguage.size(); ++i)
	    {
	        auto& target = targets[i + targetBinary.size()];
	        auto language = targetLanguage[i];
	        switch (language)
	        {
	        case ShaderLanguage::GLSL:
	            // case ShaderLanguage::ESSL:
	        {
	            target.format = SLANG_GLSL;
	            break;
	        }
	        case ShaderLanguage::HLSL:
	        {
	            target.format = SLANG_HLSL;
	            break;
	        }
	            // case ShaderLanguage::MSL:
	            //	targetDesc.format = SLANG_METAL; break;
	        case ShaderLanguage::DXIL:
	            target.format = SLANG_DXIL;
	            break;
	        case ShaderLanguage::DXBC:
	            target.format = SLANG_DXBC;
	            break;
	        default:
	            throw std::logic_error("Unsupported target language for Slang compilation.");
	        }
	    }

	    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
	    sessionDesc.targets = targets.data();
	    sessionDesc.targetCount = static_cast<SlangInt>(targets.size());

	    std::array options =
	    {
	        slang::CompilerOptionEntry{
	            slang::CompilerOptionName::EmitSpirvDirectly,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
	        },
	        slang::CompilerOptionEntry{
	            slang::CompilerOptionName::NoMangle,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
	        },
	        slang::CompilerOptionEntry{
	            slang::CompilerOptionName::IncompleteLibrary,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
	        },
        };
	    sessionDesc.compilerOptionEntries = options.data();
	    sessionDesc.compilerOptionEntryCount = options.size();

	    Slang::ComPtr<slang::ISession> session;
	    globalSession->createSession(sessionDesc, session.writeRef());

	    // 3. Load module
	    Slang::ComPtr<slang::IModule> slangModule; {
	        auto hashStr = std::to_string(std::hash<std::string>()(shaderCode));
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        slangModule = session->loadModuleFromSourceString(hashStr.c_str(), (hashStr + ".slang").c_str(),
                                                              shaderCode.c_str(),
                                                              diagnosticsBlob.writeRef());
	        // Optional diagnostic container
	        diagnoseIfNeeded(diagnosticsBlob);
	        if (!slangModule)
	        {
	            throw std::runtime_error("Failed to load Slang module.");
	        }
	    }

	    // 4. Query Entry Points
	    Slang::ComPtr<slang::IEntryPoint> entryPoint; {
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        slangModule->findEntryPointByName("main", entryPoint.writeRef());
	        if (!entryPoint)
	        {
	            std::cout << "Error getting entry point" << std::endl;
	            throw std::runtime_error("Failed to find entry point 'main' in Slang module.");
	        }
	    }

	    // 5. Compose Modules + Entry Points
	    std::array<slang::IComponentType*, 2> componentTypes =
	    {
	        slangModule,
            entryPoint
        };

	    Slang::ComPtr<slang::IComponentType> composedProgram; {
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        SlangResult result = session->createCompositeComponentType(
                componentTypes.data(),
                componentTypes.size(),
                composedProgram.writeRef(),
                diagnosticsBlob.writeRef());
	        diagnoseIfNeeded(diagnosticsBlob);
	        if (SLANG_FAILED(result))
	            throw std::runtime_error("Failed to create composite component type in Slang.");
	    }

	    Slang::ComPtr<slang::IComponentType> program = composedProgram;
	    if (isEnabledLink) {
	        // 6. Link
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        SlangResult result = composedProgram->link(
            program.writeRef(),
            diagnosticsBlob.writeRef());
	        diagnoseIfNeeded(diagnosticsBlob);
	        if (SLANG_FAILED(result))
	            throw std::runtime_error("Failed to link Slang program.");
	    }

		binaryTargetsOutput.resize(targetBinary.size());
		for (size_t i = 0; i < binaryTargetsOutput.size(); ++i)
		{
			Slang::ComPtr<slang::IBlob> targetCodeBlob;
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = program->getEntryPointCode(
			    0,
				static_cast<SlangInt>(i),
				targetCodeBlob.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				throw std::runtime_error("Failed to get target code from Slang program.");
			if (targetCodeBlob)
			{
				binaryTargetsOutput[i].resize(targetCodeBlob->getBufferSize() / sizeof(uint32_t));
				memcpy(binaryTargetsOutput[i].data(), targetCodeBlob->getBufferPointer(),
				       targetCodeBlob->getBufferSize());
			}
		}

		targetsOutput.resize(targetLanguage.size());
		for (size_t i = 0; i < targetsOutput.size(); ++i)
		{
			Slang::ComPtr<slang::IBlob> targetCodeBlob;
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
		    SlangResult result = program->getEntryPointCode(
                0,
				static_cast<SlangInt>(i + targetBinary.size()),
				// Skip the first entry point if there are binary targets
				targetCodeBlob.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				throw std::runtime_error("Failed to get target code from Slang program.");
			if (targetCodeBlob)
			{
				targetsOutput[i].resize(targetCodeBlob->getBufferSize() / sizeof(char));
				memcpy(targetsOutput[i].data(), targetCodeBlob->getBufferPointer(), targetCodeBlob->getBufferSize());
			}
		}

		if (!isEnabledReflection)
			return {};

		std::vector<ShaderCodeModule::ShaderResources> reflectedResources(targets.size());
		for (size_t i = 0; i < targets.size(); ++i)
		{
			auto& reflection = reflectedResources[i];
			auto programLayout = composedProgram->getLayout(static_cast<SlangInt>(i));

			slangReflectParameterBlock(programLayout, "global_ubo", reflection);

			for (int ii = 0; ii < programLayout->getEntryPointCount(); ++ii)
			{
				auto input = programLayout->getEntryPointByIndex(ii)->getVarLayout();
				auto inputType = input->getTypeLayout();
				//仅针对Helicon Shader特殊处理，简化反射流程
				inputType->getFieldCount();
				for (int j = 0; j < inputType->getFieldCount(); ++j)
				{
					auto param = inputType->getFieldByIndex(j);
					slangReflectField(param, "", 0, reflection);
				}
			}
		}
		return reflectedResources;
	}

void printFunc(slang::DeclReflection* decl, int indent = 0)
	{
	    std::string prefix(indent * 2, ' ');
	    auto func = decl->asFunction();
	    std::cout << prefix << "func_return_type: " << func->getReturnType()->getName() << std::endl;
	}

void printVar(slang::DeclReflection* decl, int indent = 0)
	{
	    std::string prefix(indent * 2, ' ');
	    auto func = decl->asVariable();
	    std::cout << prefix << "var_type: " << func->getType()->getName() << std::endl;
	}

void printDecl(slang::DeclReflection* decl, int indent = 0)
	{
	    std::string prefix(indent * 2, ' ');

	    // 获取声明名称和类型
	    const char* name = decl->getName();
	    auto kind = decl->getKind();

	    std::cout << prefix;

	    switch (kind)
	    {
	    case slang::DeclReflection::Kind::Module:
	        std::cout << "module ";
	        break;
	    case slang::DeclReflection::Kind::Struct:
	        std::cout << "struct ";
	        break;
	    case slang::DeclReflection::Kind::Enum:
	        std::cout << "enum ";
	        break;
	    case slang::DeclReflection::Kind::Variable:
	        std::cout << "var ";
	        printVar(decl, indent + 1);
	        break;
	    case slang::DeclReflection::Kind::Func:
	        std::cout << "func ";
	        printFunc(decl, indent + 1);
	        break;
        case slang::DeclReflection::Kind::Unsupported:
	        std::cout << "unsupported ";
            break;
        case slang::DeclReflection::Kind::Generic:
	        std::cout << "generic ";
            break;
        case slang::DeclReflection::Kind::Namespace:
	        std::cout << "namespace ";
            break;
	    }

	    if (name)
	        std::cout << name;
	    std::cout << std::endl;

	    // 递归遍历子声明
	    unsigned childCount = decl->getChildren().count;
	    for (unsigned i = 0; i < childCount; ++i)
	    {
	        printDecl(decl->getChild(i), indent + 1);
	    }
	}
    SlangModule ShaderLanguageConverter::slangModuleCompiler(SlangModuleCompileArgs arg)
    {
	    initSlangGlobalSession();

        slang::SessionDesc sessionDesc{};
	    slang::TargetDesc targetDesc{};
	    targetDesc.format = SLANG_CPP_SOURCE;
	    sessionDesc.targets = &targetDesc;
	    sessionDesc.targetCount = 1;

	    std::string_view srcStr = "slang";
	    switch (arg.sourceLanguage)
	    {

        case ShaderLanguage::GLSL:
	        srcStr = "glsl";
            break;
        case ShaderLanguage::HLSL:
	        srcStr = "hlsl";
            break;
        case ShaderLanguage::Slang:
	        break;
	    case ShaderLanguage::DXIL:
	    case ShaderLanguage::DXBC:
	    case ShaderLanguage::SpirV:
	        throw std::runtime_error("Unsupported source language for Slang module compilation.");
        }

	    std::array options =
        {
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::NoMangle,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::IncompleteLibrary,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
	        slang::CompilerOptionEntry{
	            slang::CompilerOptionName::Language,
                {slang::CompilerOptionValueKind::String, 0, 0, srcStr.data(), nullptr}
            },
        };

	    sessionDesc.compilerOptionEntries = options.data();
	    sessionDesc.compilerOptionEntryCount = options.size();
	    Slang::ComPtr<slang::ISession> session;
        slangGlobalSession->createSession(sessionDesc, session.writeRef());
        Slang::ComPtr<slang::IModule> slangModule;
	    {
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            slangModule = session->loadModuleFromSourceString(arg.moduleName.c_str(), arg.modulePath.has_value()? arg.modulePath.value().c_str() : nullptr,
                                                              arg.shaderCode.c_str(),
                                                              diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (!slangModule)
            {
                throw std::runtime_error("Failed to load Slang module.");
            }
        }

	    Slang::ComPtr<slang::IBlob> moduleBlob;
	    {
	        auto result = slangModule->serialize(moduleBlob.writeRef());
	        if (result != SLANG_OK || !moduleBlob)
	        {
	            throw std::runtime_error("Failed to serialize Slang module.");
	        }
	    }
	    SlangModule module;
	    module.name = arg.moduleName;
	    module.path = arg.modulePath.has_value()? arg.modulePath.value() : "";
	    module.binData = std::vector(static_cast<uint8_t const*>(moduleBlob->getBufferPointer()),static_cast<uint8_t const*>(moduleBlob->getBufferPointer()) + moduleBlob->getBufferSize());
	    return module;
    }

    void ShaderLanguageConverter::slangModuleReflection(SlangModuleReflectionArgs arg)
    {
	    initSlangGlobalSession();

        slang::SessionDesc sessionDesc{};
	    slang::TargetDesc targetDesc{};
	    targetDesc.format = SLANG_SPIRV;
	    sessionDesc.targets = &targetDesc;
	    sessionDesc.targetCount = 1;

	    std::array options =
        {
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::NoMangle,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::IncompleteLibrary,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
        };

	    sessionDesc.compilerOptionEntries = options.data();
	    sessionDesc.compilerOptionEntryCount = options.size();
	    Slang::ComPtr<slang::ISession> session;
        slangGlobalSession->createSession(sessionDesc, session.writeRef());
	    Slang::ComPtr<slang::IModule> srcModule;
	    Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	    auto dataBlob = slang_createBlob(arg.module->binData.data(), arg.module->binData.size());
	    srcModule = session->loadModuleFromIRBlob(arg.module->name.c_str(),arg.module->path.c_str(),dataBlob,diagnosticsBlob.writeRef());
	    diagnoseIfNeeded(diagnosticsBlob);
	    if (!srcModule)
	    {
	        std::cout << "Load Module From IR Blob failed: " << arg.module->name << std::endl;
	    }
        diagnoseIfNeeded(diagnosticsBlob);
	    arg.reflectionCallback(srcModule->getModuleReflection());

	    Slang::ComPtr<slang::IComponentType> slangTarget;
	    Slang::ComPtr<slang::IEntryPoint> entryPoint;
	    bool isLibrary = false;
	    {
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        srcModule->findEntryPointByName(arg.entrypointName.c_str(), entryPoint.writeRef());
	        if (!entryPoint)
	        {
	            //针对非shader attr标注的入口点查找
                srcModule->findAndCheckEntryPoint(arg.entrypointName.c_str(),
                                                  toSlangStage(arg.stage),
                                                  entryPoint.writeRef(),
                                                  diagnosticsBlob.writeRef());
	            if (!entryPoint)
	            {
	                //切换到库模式
	                isLibrary = true;
	            }
            }
	    }
	    if (!isLibrary)
	    {
	        // 5. Compose Modules + Entry Points
	        std::array<slang::IComponentType *, 2> componentTypes =
            {
	            srcModule,
                entryPoint
            };

	        Slang::ComPtr<slang::IComponentType> composedProgram;
	        {
	            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	            SlangResult result = session->createCompositeComponentType(
                    componentTypes.data(),
                    componentTypes.size(),
                    composedProgram.writeRef(),
                    diagnosticsBlob.writeRef());
	            diagnoseIfNeeded(diagnosticsBlob);
	            if (SLANG_FAILED(result))
	                throw std::runtime_error("Failed to create composite component type in Slang.");
	        }

	        {
	            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	            SlangResult result = composedProgram->link(
                    slangTarget.writeRef(),
                    diagnosticsBlob.writeRef());
	            diagnoseIfNeeded(diagnosticsBlob);
	            if (SLANG_FAILED(result))
	                throw std::runtime_error("Failed to link Slang program.");
	        }
	    }
        else
        {
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            auto result = srcModule->link(slangTarget.writeRef(), diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (SLANG_FAILED(result))
                throw std::runtime_error("Failed to link Slang program.");
        }

	    arg.layoutCallback(slangTarget->getLayout(0));
    }

    SlangCompileResult ShaderLanguageConverter::slangCompilerWithModules(SlangCompileArgs arg)
    {
	    return compileBySlangModule([&](Slang::ComPtr<slang::ISession> session)
        {
            Slang::ComPtr<slang::IModule> srcModule;
            auto hashStr = std::to_string(std::hash<std::string>()(arg.source));
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            srcModule = session->loadModuleFromSourceString(hashStr.c_str(), (hashStr + ".slang").c_str(),
                                                            arg.source.c_str(),
                                                            diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (!srcModule)
            {
                throw std::runtime_error("Failed to load Slang module.");
            }
            return srcModule;
        },arg);
    }

    SlangCompileResult ShaderLanguageConverter::slangCompilerWithModules(SlangCompileArgs2 arg)
    {
	    return compileBySlangModule([&](Slang::ComPtr<slang::ISession> session)
        {
	        Slang::ComPtr<slang::IModule> srcModule;
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        auto dataBlob = slang_createBlob(arg.module->binData.data(), arg.module->binData.size());
            srcModule = session->loadModuleFromIRBlob(arg.module->name.c_str(),arg.module->path.c_str(),dataBlob,diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (!srcModule)
            {
                std::cout << "Load Module From IR Blob failed: " << arg.module->name << std::endl;
            }
            return srcModule;
        },arg);
    }

    void ShaderLanguageConverter::testSlangModule(const std::vector<uint8_t> &moduleData)
    {
	    initSlangGlobalSession();

	    slang::SessionDesc sessionDesc{};
	    slang::TargetDesc targetDesc{};
	    targetDesc.format = SLANG_CPP_SOURCE;
	    sessionDesc.targets = &targetDesc;
	    sessionDesc.targetCount = 1;

	    std::array options =
        {
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::NoMangle,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::IncompleteLibrary,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
        };
	    sessionDesc.compilerOptionEntries = options.data();
	    sessionDesc.compilerOptionEntryCount = options.size();
	    Slang::ComPtr<slang::ISession> session;
	    slangGlobalSession->createSession(sessionDesc, session.writeRef());


	    Slang::ComPtr irBlob{slang_createBlob(moduleData.data(),moduleData.size())};
	    Slang::ComPtr<slang::IModule> slangModule;
	    {
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        slangModule = session->loadModuleFromIRBlob("test-module","test-module",irBlob,diagnosticsBlob.writeRef());
	        diagnoseIfNeeded(diagnosticsBlob);
	        if (!slangModule)
	        {
	            throw std::runtime_error("Failed to load Slang module.");
	        }
	    }
	    std::cout << "slang-module loaded successfully!" << std::endl;
    }

    std::vector<uint32_t> ShaderLanguageConverter::slangSpirvCompiler(const std::string& shaderCode,
                                                                      Slang::ComPtr<slang::IComponentType>& program)
	{
		std::vector<uint32_t> result;
		// 1. Create Global Session
		Slang::ComPtr<slang::IGlobalSession> globalSession;
		createGlobalSession(globalSession.writeRef());

		// 2. Create Session
		slang::SessionDesc sessionDesc = {};
		slang::TargetDesc targetDesc = {};
		targetDesc.format = SLANG_SPIRV;
		// targetDesc.profile = globalSession->findProfile("spirv_1_6");
		targetDesc.flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY;

		sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
		sessionDesc.targets = &targetDesc;
		sessionDesc.targetCount = 1;

		std::array options =
		{
			slang::CompilerOptionEntry{
				slang::CompilerOptionName::EmitSpirvDirectly,
				{slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
			}
		};
		sessionDesc.compilerOptionEntries = options.data();
		sessionDesc.compilerOptionEntryCount = options.size();

		Slang::ComPtr<slang::ISession> session;
		globalSession->createSession(sessionDesc, session.writeRef());

		// 3. Load module
		Slang::ComPtr<slang::IModule> slangModule; {
			auto hashStr = std::to_string(std::hash<std::string>()(shaderCode));
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			slangModule = session->loadModuleFromSourceString(hashStr.c_str(), (hashStr + ".slang").c_str(),
			                                                  shaderCode.c_str(),
			                                                  diagnosticsBlob.writeRef());
			// Optional diagnostic container
			diagnoseIfNeeded(diagnosticsBlob);
			if (!slangModule)
			{
				return {};
			}
		}

		// 4. Query Entry Points
		Slang::ComPtr<slang::IEntryPoint> entryPoint; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			slangModule->findEntryPointByName("main", entryPoint.writeRef());
			if (!entryPoint)
			{
				std::cout << "Error getting entry point" << std::endl;
				return {};
			}
		}

		// 5. Compose Modules + Entry Points
		std::array<slang::IComponentType*, 2> componentTypes =
		{
			slangModule,
			entryPoint
		};

		Slang::ComPtr<slang::IComponentType> composedProgram; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = session->createCompositeComponentType(
				componentTypes.data(),
				componentTypes.size(),
				composedProgram.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				return {};
		}

		// 6. Link
		Slang::ComPtr<slang::IComponentType> linkedProgram; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = composedProgram->link(
				linkedProgram.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				return {};
		}

		// 7. Get Target Kernel Code
		Slang::ComPtr<slang::IBlob> spirvCode; {
			Slang::ComPtr<slang::IBlob> diagnosticsBlob;
			SlangResult result = linkedProgram->getEntryPointCode(
				0,
				0,
				spirvCode.writeRef(),
				diagnosticsBlob.writeRef());
			diagnoseIfNeeded(diagnosticsBlob);
			if (SLANG_FAILED(result))
				return {};
		}
		result.resize(spirvCode->getBufferSize() / sizeof(uint32_t));
		memcpy(result.data(), spirvCode->getBufferPointer(), spirvCode->getBufferSize());
		program = linkedProgram;
		return result;
	}

	// get Reflected Bind Info
	ShaderCodeModule::ShaderResources ShaderLanguageConverter::spirvCrossReflectedBindInfo(
		std::vector<uint32_t> spirv_file, ShaderLanguage targetLanguage, int32_t targetVersion)
	{
		ShaderCodeModule::ShaderResources result = {};
		spirv_cross::ShaderResources res{};

		spirv_cross::CompilerGLSL* compiler{};
		switch (targetLanguage)
		{
			case ShaderLanguage::GLSL:
			{
				compiler = new spirv_cross::CompilerGLSL(std::move(spirv_file));

				spirv_cross::CompilerGLSL::Options opts = compiler->get_common_options();
				opts.enable_420pack_extension = false;
				if (targetVersion > 0)
				{
					opts.version = targetVersion;
				}
				// opts.es = (targetLanguage == ShaderLanguage::ESSL);
				opts.es = false;
				compiler->set_common_options(opts);
				res = compiler->get_shader_resources();
				break;
			}
			case ShaderLanguage::HLSL:
			{
				auto hlsl_compiler = new spirv_cross::CompilerHLSL{std::move(spirv_file)};
				compiler = hlsl_compiler;
				auto hlsl_options = hlsl_compiler->get_hlsl_options();
				hlsl_options.shader_model = 67;
				hlsl_compiler->set_hlsl_options(hlsl_options);

				res = compiler->get_shader_resources();
				break;
			}
			default:
				throw std::runtime_error("unsupported shader language");
		}

		for (auto& item: res.uniform_buffers)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};

		    std::string name = item.name;
		    if (auto pos = item.name.find_last_of('.'); pos != std::string::npos)
		    {
		        name = item.name.substr(pos + 1);
		    }
		    bindInfo.variateName = name;
			bindInfo.typeName = "uniform";
			bindInfo.elementCount = compiler->get_type((uint64_t) item.base_type_id).member_types.size();
			bindInfo.typeSize = (uint32_t) compiler->get_declared_struct_size(compiler->get_type(item.base_type_id));

			bindInfo.set = compiler->get_decoration(item.id, spv::DecorationDescriptorSet);
			bindInfo.binding = compiler->get_decoration(item.id, spv::DecorationBinding);
			bindInfo.location = compiler->get_decoration(item.id, spv::DecorationLocation);

			bindInfo.bindType = ShaderCodeModule::ShaderResources::uniformBuffers;
			result.bindInfoPool.push_back(bindInfo);

			// 记录 UBO 的总大小和名称
			result.uniformBufferSize = bindInfo.typeSize;
			result.uniformBufferName = item.name;

			// 为 UBO 的每个成员生成独立的 ShaderBindInfo（类似 pushConstantMembers）
			const auto& uboType = compiler->get_type(item.base_type_id);
			for (size_t memberIdx = 0; memberIdx < uboType.member_types.size(); ++memberIdx)
			{
				ShaderCodeModule::ShaderResources::ShaderBindInfo memberInfo = {};
				memberInfo.variateName = compiler->get_member_name(item.base_type_id, memberIdx);
				memberInfo.byteOffset = compiler->type_struct_member_offset(uboType, memberIdx);
				memberInfo.typeSize = (uint32_t) compiler->get_declared_struct_member_size(uboType, memberIdx);
				memberInfo.bindType = ShaderCodeModule::ShaderResources::uniformBufferMembers;
				memberInfo.set = bindInfo.set;
				memberInfo.binding = bindInfo.binding;
				result.bindInfoPool.push_back(memberInfo);
			}
		}

		auto isStorageImageResource = [&](const spirv_cross::Resource& item)
		{
			const spirv_cross::SPIRType& baseType = compiler->get_type(item.base_type_id);
			if (spirvTypeContainsStorageImage(*compiler, baseType))
				return true;

			const spirv_cross::SPIRType& descriptorType = compiler->get_type(item.type_id);
			return spirvTypeContainsStorageImage(*compiler, descriptorType);
		};

		auto appendDescriptorBindInfo = [&](const spirv_cross::Resource& item,
		                                    ShaderCodeModule::ShaderResources::BindType bindType)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};

		    std::string name = item.name;
		    if (auto pos = item.name.find_last_of('.'); pos != std::string::npos)
		    {
		        name = item.name.substr(pos + 1);
		    }
		    bindInfo.variateName = name;

			bindInfo.variateName = item.name.empty() ? compiler->get_name(item.id) : name;
			bindInfo.typeName = spirTypeToString(*compiler, compiler->get_type(item.base_type_id));

			bindInfo.set = compiler->get_decoration(item.id, spv::DecorationDescriptorSet);
			bindInfo.binding = compiler->get_decoration(item.id, spv::DecorationBinding);
			bindInfo.location = compiler->get_decoration(item.id, spv::DecorationLocation);
			const spirv_cross::SPIRType& descriptorType = compiler->get_type(item.type_id);
			bindInfo.elementCount = descriptorType.array.empty()
				? 1u
				: normalizeReflectedElementCount(descriptorType.array[0]);

			bindInfo.bindType = isStorageImageResource(item)
				? ShaderCodeModule::ShaderResources::storageTexture
				: bindType;
			result.bindInfoPool.push_back(bindInfo);
		};

		for (auto& item: res.sampled_images)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::sampledImages);
		}

		for (auto& item: res.separate_images)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::texture);
		}

		for (auto& item: res.separate_samplers)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::sampler);
		}

		for (auto& item: res.storage_images)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::storageTexture);
		}

		for (auto& item: res.storage_buffers)
		{
			appendDescriptorBindInfo(item, ShaderCodeModule::ShaderResources::storageBuffer);
		}

		for (auto& item: res.stage_inputs)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};

		    std::string name = item.name;
		    if (auto pos = item.name.find_last_of('.'); pos != std::string::npos)
		    {
		        name = item.name.substr(pos + 1);
		    }
		    bindInfo.variateName = name;

			const spirv_cross::SPIRType& base_type = compiler->get_type(item.base_type_id);
			bindInfo.elementCount = base_type.vecsize * base_type.columns;
			bindInfo.typeSize = 4 * base_type.vecsize * base_type.columns;

			switch (base_type.basetype)
			{
				case spirv_cross::SPIRType::Float:
					bindInfo.typeName = "float";
					break;
				case spirv_cross::SPIRType::UInt:
					bindInfo.typeName = "uint";
					break;
				case spirv_cross::SPIRType::Int:
					bindInfo.typeName = "int";
					break;
				default:
					break;
			}

			bindInfo.set = compiler->get_decoration(item.id, spv::DecorationDescriptorSet);
			bindInfo.binding = compiler->get_decoration(item.id, spv::DecorationBinding);
			bindInfo.location = compiler->get_decoration(item.id, spv::DecorationLocation);

			bindInfo.bindType = ShaderCodeModule::ShaderResources::stageInputs;

			result.bindInfoPool.push_back(bindInfo);
		}

		for (auto& item: res.stage_outputs)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};

		    std::string name = item.name;
		    if (auto pos = item.name.find_last_of('.'); pos != std::string::npos)
		    {
                name = item.name.substr(pos + 1);
		    }
			bindInfo.variateName = name;

			const spirv_cross::SPIRType& base_type = compiler->get_type(item.base_type_id);
			bindInfo.elementCount = base_type.vecsize * base_type.columns;
			bindInfo.typeSize = 4 * base_type.vecsize * base_type.columns;

			switch (base_type.basetype)
			{
				case spirv_cross::SPIRType::Float:
					bindInfo.typeName = "float";
					break;
				case spirv_cross::SPIRType::UInt:
					bindInfo.typeName = "uint";
					break;
				case spirv_cross::SPIRType::Int:
					bindInfo.typeName = "int";
					break;
				default:
					break;
			}

			bindInfo.set = compiler->get_decoration(item.id, spv::DecorationDescriptorSet);
			bindInfo.binding = compiler->get_decoration(item.id, spv::DecorationBinding);
			bindInfo.location = compiler->get_decoration(item.id, spv::DecorationLocation);

			bindInfo.bindType = ShaderCodeModule::ShaderResources::stageOutputs;

			result.bindInfoPool.push_back(bindInfo);
		}

		for (auto& item: res.push_constant_buffers)
		{
		    std::string name = item.name;
		    if (auto pos = item.name.find_last_of('.'); pos != std::string::npos)
		    {
		        name = item.name.substr(pos + 1);
		    }
			result.pushConstantName = name;
			result.pushConstantSize = (uint32_t) compiler->get_declared_struct_size(
				compiler->get_type((uint64_t) item.base_type_id));

			// obtain all the push constant member
			spirv_cross::SmallVector<spirv_cross::BufferRange> ranges = compiler->get_active_buffer_ranges(item.id);
			for (auto& range: ranges)
			{
				ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo = {};
				bindInfo.typeSize = (uint32_t) range.range;
				bindInfo.byteOffset = (uint32_t) range.offset;

			    std::string memberName = compiler->get_member_name(item.base_type_id, range.index);
			    if (auto pos = memberName.find_last_of('.'); pos != std::string::npos)
			    {
			        memberName = memberName.substr(pos + 1);
			    }
			    bindInfo.variateName = memberName;

				bindInfo.bindType = ShaderCodeModule::ShaderResources::pushConstantMembers;

				result.bindInfoPool.push_back(bindInfo);
			}
		}

		delete compiler;
		return result;
	}

    std::vector<uint32_t> ShaderLanguageConverter::spirvLinker(const std::vector<std::vector<uint32_t>> &binaries)
    {
	    spvToolContext.SetMessageConsumer(
        [](spv_message_level_t level, const char* source,
           const spv_position_t& position, const char* message) {
            switch (level) {
                case SPV_MSG_FATAL:
            case SPV_MSG_INTERNAL_ERROR:
            case SPV_MSG_ERROR:
                std::cerr << "error: " << position.index << ": "
                          << message << std::endl;
                break;
            case SPV_MSG_WARNING:
                std::cout << "warning: " << position.index << ": "
                          << message << std::endl;
                break;
            case SPV_MSG_INFO:
                std::cout << "info: " << position.index << ": "
                          << message << std::endl;
                break;
            default:
                break;
        }
    });
        std::vector<uint32_t> result;

        auto spv_result = spvtools::Link(spvToolContext,binaries, &result);
        if (SPV_SUCCESS != spv_result)
        {
            throw std::runtime_error("Failed to link SPIR-V binaries: " + std::to_string(spv_result));
        }

        isSpirvValid(result);
        return result;
    }

    bool ShaderLanguageConverter::isSpirvValid(const std::vector<uint32_t> &spirvCode)
    {
	    spv_diagnostic diagnostic = nullptr;
	    spv_const_binary_t binary = { spirvCode.data(), spirvCode.size() };

	    spv_result_t result = spvValidate(spvToolContext.CContext(), &binary, &diagnostic);
	    if (result == SPV_SUCCESS) {
	        spvDiagnosticDestroy(diagnostic);
	        return true;
	    }
        std::cerr << "❌ SPIR-V 验证失败 (错误码: " << result << ")\n";

        if (diagnostic) {
            std::cerr << "位置: 字索引 " << diagnostic->position.index
                << ", 行 " << diagnostic->position.line
                << ", 列 " << diagnostic->position.column << "\n";
            std::cerr << "详情: " << diagnostic->error << "\n";
        }
	    spvDiagnosticDestroy(diagnostic);
	    return false;
    }

    void ShaderLanguageConverter::slangReflectField(slang::VariableLayoutReflection* field, std::string_view accessPath,
                                                    size_t varBaseOffset, ShaderCodeModule::ShaderResources& reflection)
	{
		auto type = field->getTypeLayout();
		auto name = accessPath.empty() ? field->getName() : accessPath.data() + std::string(".") + field->getName();

		int set = 0;

		if (type->getKind() == slang::TypeReflection::Kind::Struct ||
		    type->getKind() == slang::TypeReflection::Kind::ParameterBlock)
		{
			for (uint32_t i = 0; i < type->getFieldCount(); ++i)
			{
				auto innerField = type->getFieldByIndex(i);
				slangReflectField(innerField, name, varBaseOffset + field->getOffset(), reflection);
			}
		} else slangReflectDescriptor(field, set, name, varBaseOffset, reflection);
	}

	void ShaderLanguageConverter::slangReflectParameterBlock(slang::ProgramLayout* program, std::string_view uboName,
	                                                         ShaderCodeModule::ShaderResources& reflection)
	{
		for (uint32_t i = 0; i < program->getParameterCount(); ++i)
		{
			auto type = program->getParameterByIndex(i)->getTypeLayout()->getElementTypeLayout();
			auto index = type->findFieldIndexByName(uboName.data());
			if (index == -1)
				continue;
			auto ubo = type->getFieldByIndex(index);
			auto uboType = ubo->getTypeLayout()->getElementTypeLayout();
			slangReflectDescriptor(ubo, 0, uboName, 0, reflection);
			for (uint32_t j = 0; j < uboType->getFieldCount(); ++j)
			{
				auto field = uboType->getFieldByIndex(j);
				slangReflectField(field, "", 0, reflection);
			}
		}
	}

	void ShaderLanguageConverter::slangReflectDescriptor(slang::VariableLayoutReflection* var,
	                                                     int set, std::string_view name, size_t varBaseOffset,
	                                                     ShaderCodeModule::ShaderResources& resource)
	{
		auto type = var->getTypeLayout();
		auto rangeCount = type->getDescriptorSetDescriptorRangeCount(set);
		if (rangeCount > 0)
		{
			ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo {};
			bindInfo.set = set;
			bindInfo.binding = type->getBindingRangeFirstDescriptorRangeIndex(0);
			bindInfo.typeName = type->getName();
			bindInfo.typeSize = type->getSize();
			bindInfo.variateName = var->getName();
			switch (type->getDescriptorSetDescriptorRangeType(set, 0))
			{
				case slang::BindingType::ConstantBuffer:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::uniformBuffers;
					bindInfo.typeSize = type->getElementTypeLayout()->getSize();
					break;
				case slang::BindingType::Texture:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::texture;
					break;
				case slang::BindingType::Sampler:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::sampler;
					break;
				case slang::BindingType::MutableTexture:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::storageTexture;
					break;
				case slang::BindingType::MutableRawBuffer:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::storageBuffer;
					break;
				case slang::BindingType::RawBuffer:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::rawBuffer;
					break;
				case slang::BindingType::CombinedTextureSampler:
					bindInfo.bindType = ShaderCodeModule::ShaderResources::sampledImages;
					break;
				case slang::BindingType::Unknown:
				default: return;
			}
			resource.bindInfoPool.push_back(bindInfo);
			return;
		}

		ShaderCodeModule::ShaderResources::ShaderBindInfo bindInfo {};
		bindInfo.bindType = ShaderCodeModule::ShaderResources::none;
		bindInfo.byteOffset = var->getOffset(var->getCategory()) + varBaseOffset;
		bindInfo.typeName = type->getName();
		bindInfo.typeSize = type->getSize(var->getCategory());
		bindInfo.variateName = var->getName();
		bindInfo.location = var->getSemanticName() ? var->getSemanticIndex() : 0;
		bindInfo.semantic = var->getSemanticName() ? var->getSemanticName() : "";
		resource.bindInfoPool.push_back(bindInfo);
	}

    void ShaderLanguageConverter::initSlangGlobalSession()
    {
	    if (!slangGlobalSession)
	    {
	        SlangGlobalSessionDesc desc{};
	        desc.enableGLSL = true;
	        createGlobalSession(&desc,slangGlobalSession.writeRef());
	        if (!slangGlobalSession)
	        {
	            throw std::runtime_error("Failed to create Slang global session.");
	        }
	    }
    }

    SlangCompileResult ShaderLanguageConverter::compileBySlangModule(const std::function<Slang::ComPtr<slang::IModule>(Slang::ComPtr<slang::ISession>)>& callback, SlangCompileArgs0& arg0)
    {
	    initSlangGlobalSession();
        slang::SessionDesc sessionDesc{};
        std::vector<slang::TargetDesc> targetDescs;
        for (auto it = arg0.targetLanguages.begin(); it != arg0.targetLanguages.end();)
        {
            auto lang = *it;
            slang::TargetDesc target{};
            switch (lang)
            {
            case ShaderLanguage::GLSL:
                target.format = SLANG_GLSL;
                break;
            case ShaderLanguage::HLSL:
                target.format = SLANG_HLSL;
                break;
            case ShaderLanguage::DXIL:
                target.format = SLANG_DXIL;
                target.profile = slangGlobalSession->findProfile("sm_6_6");
                break;
            case ShaderLanguage::DXBC:
                target.format = SLANG_DXBC;
                break;
            case ShaderLanguage::SpirV:
                target.format = SLANG_SPIRV;
                target.flags |= SLANG_EMIT_SPIRV_DIRECTLY;
                break;
            case ShaderLanguage::Slang:
                it = arg0.targetLanguages.erase(it);
                continue;
            }
            targetDescs.push_back(target);
            ++it;
        }
	    sessionDesc.targets = targetDescs.data();
	    sessionDesc.targetCount = static_cast<SlangInt>(targetDescs.size());

	    std::string_view srcStr = "slang";
	    switch (arg0.sourceLanguage)
	    {
	    case ShaderLanguage::GLSL:
	        srcStr = "glsl";
	        break;
	    case ShaderLanguage::HLSL:
	        srcStr = "hlsl";
	        break;
	    case ShaderLanguage::Slang:
	        break;
	    case ShaderLanguage::DXIL:
	    case ShaderLanguage::DXBC:
	    case ShaderLanguage::SpirV:
	        throw std::runtime_error("Unsupported source language for Slang module compilation.");
	    }

	    std::array options =
	    {
	        slang::CompilerOptionEntry{
	            slang::CompilerOptionName::EmitSpirvDirectly,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
	        },
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::NoMangle,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::IncompleteLibrary,
                {slang::CompilerOptionValueKind::Int, 1, 0, nullptr, nullptr}
            },
            slang::CompilerOptionEntry{
                slang::CompilerOptionName::Language,
                {slang::CompilerOptionValueKind::String, 0, 0, srcStr.data(), nullptr}
            },
        };

	    sessionDesc.compilerOptionEntries = options.data();
	    sessionDesc.compilerOptionEntryCount = options.size();

	    Slang::ComPtr<slang::ISession> session;
	    slangGlobalSession->createSession(sessionDesc, session.writeRef());

	    //load modules
        {
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            for (auto module : arg0.deps)
            {
                auto dataBlob = slang_createBlob(module->binData.data(), module->binData.size());
                auto mod = session->loadModuleFromIRBlob(module->name.c_str(),module->path.c_str(),dataBlob,diagnosticsBlob.writeRef());
                diagnoseIfNeeded(diagnosticsBlob);
                if (!mod)
                {
                    std::cout << "Load Module From IR Blob failed: " << module->name << std::endl;
                }
            }
        }

	    Slang::ComPtr<slang::IModule> srcModule = callback(session);

	    Slang::ComPtr<slang::IComponentType> slangTarget;
	    Slang::ComPtr<slang::IEntryPoint> entryPoint;
	    bool isLibrary = false;
	    {
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        srcModule->findEntryPointByName(arg0.entrypointName.c_str(), entryPoint.writeRef());
	        if (!entryPoint)
	        {
	            //针对非shader attr标注的入口点查找
                srcModule->findAndCheckEntryPoint(arg0.entrypointName.c_str(),
                                                  toSlangStage(arg0.stage),
                                                  entryPoint.writeRef(),
                                                  diagnosticsBlob.writeRef());
	            if (!entryPoint)
	            {
	                //切换到库模式编译
	                isLibrary = true;
	            }
            }
	    }
	    if (!isLibrary)
	    {
	        // 5. Compose Modules + Entry Points
	        std::array<slang::IComponentType *, 2> componentTypes =
            {
	            srcModule,
                entryPoint
            };

	        Slang::ComPtr<slang::IComponentType> composedProgram;
	        {
	            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	            SlangResult result = session->createCompositeComponentType(
                    componentTypes.data(),
                    componentTypes.size(),
                    composedProgram.writeRef(),
                    diagnosticsBlob.writeRef());
	            diagnoseIfNeeded(diagnosticsBlob);
	            if (SLANG_FAILED(result))
	                throw std::runtime_error("Failed to create composite component type in Slang.");
	        }

	        {
	            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	            SlangResult result = composedProgram->link(
                    slangTarget.writeRef(),
                    diagnosticsBlob.writeRef());
	            diagnoseIfNeeded(diagnosticsBlob);
	            if (SLANG_FAILED(result))
	                throw std::runtime_error("Failed to link Slang program.");
	        }
	    }
        else
        {
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            auto result = srcModule->link(slangTarget.writeRef(), diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (SLANG_FAILED(result))
                throw std::runtime_error("Failed to link Slang program.");
        }

        return getCompileResult(targetDescs, slangTarget, arg0, isLibrary);
    }

    SlangCompileResult ShaderLanguageConverter::getCompileResult(std::vector<slang::TargetDesc> targetDescs, Slang::ComPtr<slang::IComponentType> slangTarget,SlangCompileArgs0& arg0,bool isLibrary)
    {
	    SlangCompileResult finalResult;
	    for (size_t i = 0; i < targetDescs.size(); ++i)
	    {
	        Slang::ComPtr<slang::IBlob> targetCodeBlob;
            if (!isLibrary)
            {
                Slang::ComPtr<slang::IBlob> diagnosticsBlob;
                SlangResult result = slangTarget->getEntryPointCode(
                    0,
                    static_cast<SlangInt>(i),
                    targetCodeBlob.writeRef(),
                    diagnosticsBlob.writeRef());
                diagnoseIfNeeded(diagnosticsBlob);
                if (SLANG_FAILED(result))
                    throw std::runtime_error("Failed to get target code from Slang program.");
            }
            else
            {
                Slang::ComPtr<slang::IBlob> diagnosticsBlob;
                SlangResult result = slangTarget->getTargetCode(
                    static_cast<SlangInt>(i),
                    targetCodeBlob.writeRef(),
                    diagnosticsBlob.writeRef());
                diagnoseIfNeeded(diagnosticsBlob);
                if (SLANG_FAILED(result))
                    throw std::runtime_error("Failed to get target code from Slang program.");
            }

	        bool isBin = false;
	        switch (targetDescs[i].format)
	        {
	        case SLANG_SPIRV:
	        case SLANG_DXBC:
	        case SLANG_DXIL:
	            isBin = true;
	            break;
	        default:
	            break;
	        }
	        auto targetLang = arg0.targetLanguages[i];
	        if (arg0.enableReflection)
	        {
	            auto resource = slangReflection(slangTarget->getLayout(static_cast<SlangInt>(i)));
	            finalResult.reflections.insert({targetLang, std::move(resource)});
	        }
	        arg0.layoutCallback(slangTarget->getLayout(static_cast<SlangInt>(i)));

	        if (isBin)
	        {
	            SlangCompileResult::BinaryTarget target(targetCodeBlob->getBufferSize() / sizeof(uint32_t));
	            memcpy(target.data(), targetCodeBlob->getBufferPointer(),
                       targetCodeBlob->getBufferSize());
	            finalResult.binaryTargets.insert({targetLang, std::move(target)});
	            continue;
	        }

	        SlangCompileResult::StringTarget target;
	        target.resize(targetCodeBlob->getBufferSize() / sizeof(char));
	        memcpy(target.data(), targetCodeBlob->getBufferPointer(), targetCodeBlob->getBufferSize());
	        finalResult.stringTargets.insert({targetLang, std::move(target)});
	    }

        return finalResult;
    }

    SlangStage ShaderLanguageConverter::toSlangStage(ShaderStage stage)
    {
	    switch (stage)
	    {
	    case ShaderStage::VertexShader:
	        return SLANG_STAGE_VERTEX;
	    case ShaderStage::FragmentShader:
	        return SLANG_STAGE_PIXEL;
	    case ShaderStage::ComputeShader:
	        return SLANG_STAGE_COMPUTE;
	    default:
	        throw std::logic_error("Unsupported shader stage.");
	    }
    }

    uint32_t ShaderLanguageConverter::getScalarSizeInBytes(slang::TypeReflection::ScalarType st)
    {
        switch (st)
        {
            case slang::TypeReflection::ScalarType::Float32:
            case slang::TypeReflection::ScalarType::Int32:
            case slang::TypeReflection::ScalarType::UInt32: return 4;
            case slang::TypeReflection::ScalarType::Float64:
            case slang::TypeReflection::ScalarType::Int64:
            case slang::TypeReflection::ScalarType::UInt64: return 8;
            case slang::TypeReflection::ScalarType::Float16:
            case slang::TypeReflection::ScalarType::Int16:
            case slang::TypeReflection::ScalarType::UInt16: return 2;
            case slang::TypeReflection::ScalarType::Int8:
            case slang::TypeReflection::ScalarType::UInt8:   return 1;
            default:                                         return 4;
        }
    }

    void ShaderLanguageConverter::collectSlangReflection(
        slang::ProgramLayout* programLayout,
        slang::VariableLayoutReflection* varLayout,
        ShaderCodeModule::ShaderResources& resources,
        bool insidePushConstant,
        bool insideUniformBuffer,
        uint64_t baseByteOffset)
    {
        if (!varLayout) return;
        auto typeLayout = varLayout->getTypeLayout();
        if (!typeLayout) return;

        const char* varName = varLayout->getName();
        if (!varName) varName = "";

        auto kind = typeLayout->getKind();
        bool isBufferWrapper = (kind == slang::TypeReflection::Kind::ConstantBuffer ||
                                kind == slang::TypeReflection::Kind::ParameterBlock);

        bool isPushConstant = false;
        int catCount = varLayout->getCategoryCount();
        for (int i = 0; i < catCount; ++i)
        {
            if (varLayout->getCategoryByIndex(i) == slang::ParameterCategory::PushConstantBuffer)
            {
                isPushConstant = true;
                break;
            }
        }

        if (isBufferWrapper && isPushConstant)
        {
            resources.pushConstantName = varName;
            size_t pcSize = typeLayout->getSize(slang::ParameterCategory::Uniform);
            if (pcSize == 0)
            {
                if (auto ev = typeLayout->getElementVarLayout())
                    if (auto et = ev->getTypeLayout())
                        pcSize = et->getSize(slang::ParameterCategory::Uniform);
            }
            resources.pushConstantSize = static_cast<uint32_t>(pcSize);

            if (auto elementVar = typeLayout->getElementVarLayout())
            {
                if (auto et = elementVar->getTypeLayout())
                {
                    if (et->getKind() == slang::TypeReflection::Kind::Struct)
                    {
                        for (int f = 0; f < et->getFieldCount(); ++f)
                            collectSlangReflection(programLayout, et->getFieldByIndex(f), resources, true, false, 0);
                    }
                }
            }
            return;
        }

        if (isBufferWrapper && !isPushConstant)
        {
            const char* instanceName = varName;

            //resources.uniformBufferName = instanceName;
            size_t uboSize = typeLayout->getSize(slang::ParameterCategory::Uniform);
            if (uboSize == 0)
            {
                if (auto ev = typeLayout->getElementVarLayout())
                    if (auto et = ev->getTypeLayout())
                        uboSize = et->getSize(slang::ParameterCategory::Uniform);
            }
            //resources.uniformBufferSize = static_cast<uint32_t>(uboSize);
            if (uboSize != 0)
            {
                resources.uniformBufferName = instanceName;
                resources.uniformBufferSize = static_cast<uint32_t>(uboSize);

                uint32_t set = 0, binding = 0;
                for (int i = 0; i < catCount; ++i)
                {
                    auto cat = varLayout->getCategoryByIndex(i);
                    if (cat == slang::ParameterCategory::ConstantBuffer ||
                        cat == slang::ParameterCategory::DescriptorTableSlot)
                    {
                        set = static_cast<uint32_t>(varLayout->getBindingSpace(cat));
                        binding = static_cast<uint32_t>(varLayout->getOffset(cat));
                        break;
                    }
                }

                auto bindType = ShaderCodeModule::ShaderResources::uniformBuffers;

                ShaderCodeModule::ShaderResources::ShaderBindInfo info {};
                info.set = set;
                info.binding = binding;
                info.variateName = varName;
                info.typeName = typeLayout->getName() ? typeLayout->getName() : "ConstantBuffer";
                info.bindType = bindType;
                info.typeSize = static_cast<uint32_t>(uboSize);
                resources.bindInfoPool.push_back(info);
            }

            if (kind == slang::TypeReflection::Kind::ParameterBlock)
            {
                //collectSlangParameterBlock(typeLayout, binding, bindType,uboSize);
                std::cout << "Parameter Block: " << instanceName << std::endl;
                auto containerVarLayout = typeLayout->getContainerVarLayout();
                uint32_t setIndex = containerVarLayout->getOffset(slang::ParameterCategory::RegisterSpace);
                std::cout << "Descriptor Set: " << setIndex << std::endl;


                std::cout << "binding:" << (uint32_t)varLayout->getOffset(slang::ParameterCategory::DescriptorTableSlot)<< std::endl;
                std::cout << "space:" << (uint32_t)varLayout->getOffset(slang::ParameterCategory::RegisterSpace)<< std::endl;
                std::cout << "child space:" << (uint32_t)varLayout->getOffset(slang::ParameterCategory::SubElementRegisterSpace)<< std::endl;

                // === 元素信息（Material 结构体内部）===
                auto elementVarLayout = typeLayout->getElementVarLayout();
                auto elementTypeLayout = elementVarLayout->getTypeLayout();

                // 遍历 Material 的字段
                uint32_t fieldCount = elementTypeLayout->getFieldCount();
                for (uint32_t i = 0; i < fieldCount; i++) {
                    auto field = elementTypeLayout->getFieldByIndex(i);
                    auto fieldType = field->getType();

                    std::cout << "Field: " << field->getName()
                              << " Type: " << fieldType->getName()
                              << " Binding: " << field->getBindingIndex()
                              << std::endl;
                }

                // === 获取普通数据大小（用于分配 Uniform Buffer）===
                size_t ordinaryDataSize = elementTypeLayout->getSize();
                std::cout << "Ordinary data size: " << ordinaryDataSize << " bytes" << std::endl;
            }

            if (auto elementVar = typeLayout->getElementVarLayout())
            {
                if (auto et = elementVar->getTypeLayout())
                {
                    if (et->getKind() == slang::TypeReflection::Kind::Struct)
                    {
                        for (int f = 0; f < et->getFieldCount(); ++f)
                            collectSlangReflection(programLayout, et->getFieldByIndex(f), resources, false, true, 0);
                    }
                }
            }
            return;
        }

        slang::TypeLayoutReflection* descriptorTypeLayout = typeLayout;
        while (descriptorTypeLayout && descriptorTypeLayout->getKind() == slang::TypeReflection::Kind::Array)
            descriptorTypeLayout = descriptorTypeLayout->getElementTypeLayout();
        if (!descriptorTypeLayout)
            descriptorTypeLayout = typeLayout;

        const auto descriptorKind = descriptorTypeLayout->getKind();
        const auto descriptorBindType = bindTypeFromSlangLayout(typeLayout, descriptorTypeLayout);
        ShaderCodeModule::ShaderResources::ShaderBindInfo info {};
        info.variateName = varName;
        info.typeName = descriptorTypeLayout->getName() ? descriptorTypeLayout->getName() : "";

        if (kind == slang::TypeReflection::Kind::Scalar)
        {
            info.elementCount = 1;
            info.typeSize = getScalarSizeInBytes(typeLayout->getScalarType());
        }
        else if (kind == slang::TypeReflection::Kind::Vector)
        {
            info.elementCount = static_cast<uint64_t>(typeLayout->getElementCount());
            info.typeSize = info.elementCount * getScalarSizeInBytes(typeLayout->getScalarType());
        }
        else if (kind == slang::TypeReflection::Kind::Matrix)
        {
            info.elementCount = static_cast<uint64_t>(typeLayout->getRowCount() * typeLayout->getColumnCount());
            info.typeSize = info.elementCount * getScalarSizeInBytes(typeLayout->getScalarType());
        }
        else if (kind == slang::TypeReflection::Kind::Array)
        {
            size_t elementCount = typeLayout->getElementCount(programLayout);
            info.elementCount = normalizeReflectedElementCount(elementCount);
        }

        for (int i = 0; i < catCount; ++i)
        {
            auto cat = varLayout->getCategoryByIndex(i);
            switch (cat)
            {
                case slang::ParameterCategory::DescriptorTableSlot:
                case slang::ParameterCategory::ShaderResource:
                    if (descriptorKind == slang::TypeReflection::Kind::Resource)
                    {
                        info.bindType = descriptorBindType != ShaderCodeModule::ShaderResources::BindType::none
                            ? descriptorBindType
                            : ShaderCodeModule::ShaderResources::BindType::sampledImages;
                        info.set = static_cast<uint32_t>(varLayout->getBindingSpace(cat));
                        info.binding = static_cast<uint32_t>(varLayout->getOffset(cat));
                    }
                    else if (descriptorKind == slang::TypeReflection::Kind::SamplerState)
                    {
                        info.bindType = ShaderCodeModule::ShaderResources::BindType::sampler;
                        info.set = static_cast<uint32_t>(varLayout->getBindingSpace(cat));
                        info.binding = static_cast<uint32_t>(varLayout->getOffset(cat));
                    }
                    break;
                case slang::ParameterCategory::VaryingInput:
                    info.bindType = ShaderCodeModule::ShaderResources::BindType::stageInputs;
                    info.location = static_cast<uint32_t>(varLayout->getOffset(cat));
                    if (varLayout->getSemanticName())
                    {
                        info.semantic = varLayout->getSemanticName();
                        info.location = static_cast<uint32_t>(varLayout->getSemanticIndex());
                    }
                    break;
                case slang::ParameterCategory::VaryingOutput:
                    info.bindType = ShaderCodeModule::ShaderResources::BindType::stageOutputs;
                    info.location = static_cast<uint32_t>(varLayout->getOffset(cat));
                    if (varLayout->getSemanticName())
                    {
                        info.semantic = varLayout->getSemanticName();
                        info.location = static_cast<uint32_t>(varLayout->getSemanticIndex());
                    }
                    break;
                case slang::ParameterCategory::Uniform:
                    info.byteOffset = baseByteOffset + static_cast<uint64_t>(varLayout->getOffset(cat));
                    info.typeSize = static_cast<uint32_t>(typeLayout->getSize(cat));
                    if (insidePushConstant)
                        info.bindType = ShaderCodeModule::ShaderResources::BindType::pushConstantMembers;
                    else if (insideUniformBuffer)
                        info.bindType = ShaderCodeModule::ShaderResources::BindType::uniformBufferMembers;
                    break;
                case slang::ParameterCategory::UnorderedAccess:
                    info.bindType = descriptorBindType != ShaderCodeModule::ShaderResources::BindType::none
                        ? descriptorBindType
                        : ShaderCodeModule::ShaderResources::BindType::storageTexture;
                    info.set = static_cast<uint32_t>(varLayout->getBindingSpace(cat));
                    info.binding = static_cast<uint32_t>(varLayout->getOffset(cat));
                    break;
                default:
                    break;
            }
        }

        if (info.bindType == ShaderCodeModule::ShaderResources::BindType::none)
        {
            if (insidePushConstant) info.bindType = ShaderCodeModule::ShaderResources::BindType::pushConstantMembers;
            else if (insideUniformBuffer) info.bindType = ShaderCodeModule::ShaderResources::BindType::uniformBufferMembers;
        }

        if (info.bindType == ShaderCodeModule::ShaderResources::BindType::none &&
            descriptorKind == slang::TypeReflection::Kind::Resource)
        {
            info.bindType = ShaderCodeModule::ShaderResources::BindType::sampledImages;
        }

        if (info.bindType != ShaderCodeModule::ShaderResources::BindType::none)
            resources.bindInfoPool.push_back(info);

        if (kind == slang::TypeReflection::Kind::Struct)
        {
            uint64_t structBaseOffset = baseByteOffset;
            for (int c = 0; c < varLayout->getCategoryCount(); ++c)
            {
                if (varLayout->getCategoryByIndex(c) == slang::ParameterCategory::Uniform)
                {
                    structBaseOffset += static_cast<uint64_t>(varLayout->getOffset(slang::ParameterCategory::Uniform));
                    break;
                }
            }
            for (int f = 0; f < typeLayout->getFieldCount(); ++f)
                collectSlangReflection(programLayout, typeLayout->getFieldByIndex(f), resources, insidePushConstant, insideUniformBuffer, structBaseOffset);
        }
    }

    void ShaderLanguageConverter::collectSlangParameterBlock(slang::TypeLayoutReflection* typeLayout, uint32_t& binding, ShaderCodeModule::ShaderResources::BindType& bindType, size_t& uboSize)
    {
	    int set = 0;
	    auto rangeCount = typeLayout->getDescriptorSetDescriptorRangeCount(set);
	    if (rangeCount > 0)
	    {
	        binding = typeLayout->getBindingRangeFirstDescriptorRangeIndex(0);
	        switch (typeLayout->getDescriptorSetDescriptorRangeType(set, 0))
	        {
	        case slang::BindingType::ConstantBuffer:
	            bindType = ShaderCodeModule::ShaderResources::uniformBuffers;
	            uboSize = typeLayout->getElementTypeLayout()->getSize();
	            break;
	        case slang::BindingType::Texture:
	            bindType = ShaderCodeModule::ShaderResources::texture;
	            break;
	        case slang::BindingType::Sampler:
	            bindType = ShaderCodeModule::ShaderResources::sampler;
	            break;
	        case slang::BindingType::MutableTexture:
	            bindType = ShaderCodeModule::ShaderResources::storageTexture;
	            break;
	        case slang::BindingType::MutableRawBuffer:
	            bindType = ShaderCodeModule::ShaderResources::storageBuffer;
	            break;
	        case slang::BindingType::RawBuffer:
	            bindType = ShaderCodeModule::ShaderResources::rawBuffer;
	            break;
	        case slang::BindingType::CombinedTextureSampler:
	            bindType = ShaderCodeModule::ShaderResources::sampledImages;
	            break;
	        case slang::BindingType::Unknown:
	        default: break;
	        }
	    }
    }

    void ShaderLanguageConverter::slangReflectGlobalScope(slang::ProgramLayout* programLayout, ShaderCodeModule::ShaderResources& resources)
    {
        if (!programLayout) return;

        slang::VariableLayoutReflection* globalVar = programLayout->getGlobalParamsVarLayout();
        if (!globalVar) return;

        slang::TypeLayoutReflection* globalType = globalVar->getTypeLayout();
        if (!globalType) return;

        if (globalType->getKind() == slang::TypeReflection::Kind::Struct)
        {
            for (int i = 0; i < globalType->getFieldCount(); ++i)
                collectSlangReflection(programLayout, globalType->getFieldByIndex(i), resources, false, false, 0);
        }
        else
        {
            collectSlangReflection(programLayout, globalVar, resources, false, false, 0);
        }

        for (uint32_t i = 0; i < programLayout->getParameterCount(); ++i)
        {
            slang::VariableLayoutReflection* parameter = programLayout->getParameterByIndex(i);
            if (!parameter)
                continue;

            const auto beforeCount = resources.bindInfoPool.size();
            collectSlangReflection(programLayout, parameter, resources, false, false, 0);
            if (resources.bindInfoPool.size() <= beforeCount)
                continue;

            ShaderCodeModule::ShaderResources::ShaderBindInfo& added = resources.bindInfoPool.back();
            const auto duplicate = std::find_if(resources.bindInfoPool.begin(),
                                                std::prev(resources.bindInfoPool.end()),
                                                [&](const ShaderCodeModule::ShaderResources::ShaderBindInfo& existing) {
                                                    return existing.variateName == added.variateName &&
                                                           existing.bindType == added.bindType &&
                                                           existing.set == added.set &&
                                                           existing.binding == added.binding &&
                                                           existing.location == added.location &&
                                                           existing.byteOffset == added.byteOffset;
                                                });
            if (duplicate != std::prev(resources.bindInfoPool.end()))
                resources.bindInfoPool.pop_back();
        }
    }

    void ShaderLanguageConverter::slangReflectEntryPoints(slang::ProgramLayout* programLayout, ShaderCodeModule::ShaderResources& resources)
    {
        if (!programLayout) return;

        int epCount = programLayout->getEntryPointCount();
        for (int i = 0; i < epCount; ++i)
        {
            slang::EntryPointReflection* ep = programLayout->getEntryPointByIndex(i);
            if (!ep) continue;

            ShaderCodeModule::ShaderResources::EntryPointInfo info;
            info.name = ep->getName() ? ep->getName() : "";
            info.stage = slangStageToShaderStage(ep->getStage());

            if (ep->getStage() == SLANG_STAGE_COMPUTE)
            {
                SlangUInt sizes[3] = { 1, 1, 1 };
                ep->getComputeThreadGroupSize(3, sizes);
                info.numthreads = ktm::uvec3(
                    static_cast<unsigned>(sizes[0]),
                    static_cast<unsigned>(sizes[1]),
                    static_cast<unsigned>(sizes[2])
                );
            }
            else
            {
                info.numthreads = ktm::uvec3(1, 1, 1);
            }

            resources.entryPointInfoPool.push_back(info);

            for (uint32_t i = 0; i < ep->getParameterCount(); ++i)
            {
                slang::VariableLayoutReflection* parameter = ep->getParameterByIndex(i);
                if (!parameter)
                    continue;

                const auto beforeCount = resources.bindInfoPool.size();
                collectSlangReflection(programLayout, parameter, resources, false, false, 0);
                if (resources.bindInfoPool.size() <= beforeCount)
                    continue;

                ShaderCodeModule::ShaderResources::ShaderBindInfo& added = resources.bindInfoPool.back();
                const auto duplicate = std::find_if(resources.bindInfoPool.begin(),
                                                    std::prev(resources.bindInfoPool.end()),
                                                    [&](const ShaderCodeModule::ShaderResources::ShaderBindInfo& existing) {
                                                        return existing.variateName == added.variateName &&
                                                               existing.bindType == added.bindType &&
                                                               existing.set == added.set &&
                                                               existing.binding == added.binding &&
                                                               existing.location == added.location &&
                                                               existing.byteOffset == added.byteOffset;
                                                    });
                if (duplicate != std::prev(resources.bindInfoPool.end()))
                    resources.bindInfoPool.pop_back();
            }
        }
    }

    ShaderCodeModule::ShaderResources ShaderLanguageConverter::slangReflectBindInfo(slang::ProgramLayout* programLayout)
    {
        ShaderCodeModule::ShaderResources resources;
        resources.bindInfoPool.clear();
        resources.entryPointInfoPool.clear();
        resources.pushConstantSize = 0;
        resources.pushConstantName.clear();
        resources.uniformBufferSize = 0;
        resources.uniformBufferName.clear();

        slangReflectGlobalScope(programLayout, resources);
        slangReflectEntryPoints(programLayout, resources);

        return resources;
    }

    ShaderCodeModule::ShaderResources ShaderLanguageConverter::slangReflection(slang::ProgramLayout* programLayout)
    {
	    ShaderCodeModule::ShaderResources resources;

        // ---- 1. 全局参数（UniformBuffers, ParameterBlocks, Textures 等） ----
        for (int i = 0; i < programLayout->getParameterCount(); ++i)
        {
            auto param = programLayout->getParameterByIndex(i);
            ShaderCursor cursor;
            cursor.m_varLayout = param;
            cursor.m_typeLayout = param->getTypeLayout();

            if (param->getTypeLayout()->getKind() == slang::TypeReflection::Kind::ParameterBlock)
                cursor.m_offset.set = param->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
            else
                cursor.m_offset.set = param->getOffset(slang::ParameterCategory::RegisterSpace);
            cursor.m_offset.binding = param->getOffset(slang::ParameterCategory::DescriptorTableSlot);

            cursor.collectBindings(resources, param->getName());
        }

        // ---- 2. Entry Point（Varying Input / Output） ----
        for (int i = 0; i < programLayout->getEntryPointCount(); ++i)
        {
            auto entryPoint = programLayout->getEntryPointByIndex(i);
            if (entryPoint)
            {
                EmbeddedShader::ShaderCodeModule::ShaderResources::EntryPointInfo epInfo;
                epInfo.name = entryPoint->getName();
                // 注意：需要确保 ShaderStage 枚举与 slang::Stage 兼容，或自行映射
                epInfo.stage = slangStageToShaderStage(entryPoint->getStage());
                resources.entryPointInfoPool.push_back(epInfo);

                // Varying Inputs
                for (int i = 0; i < entryPoint->getParameterCount(); ++i)
                {
                    auto param = entryPoint->getParameterByIndex(i);
                    ShaderCursor cursor;
                    cursor.m_varLayout = param;
                    cursor.m_typeLayout = param->getTypeLayout();
                    cursor.collectBindings(resources, param->getName());
                }

                // Varying Output
                auto resultVar = entryPoint->getResultVarLayout();
                if (resultVar)
                {
                    ShaderCursor cursor;
                    cursor.m_varLayout = resultVar;
                    cursor.m_typeLayout = resultVar->getTypeLayout();
                    cursor.collectBindings(resources, "return");
                }
            }
        }
	    return resources;
    }

    ShaderCodeModule::ShaderResources ShaderLanguageConverter::slangModuleReflectShaderResource(SlangModuleReflectShaderResourceArgs arg)
    {
	    SlangModuleReflectionArgs reflectArgs;
	    static_cast<SlangModuleReflectionArgs0&>(reflectArgs) = std::move(arg);
	    ShaderCodeModule::ShaderResources resources;
	    reflectArgs.layoutCallback = [&](slang::ProgramLayout* programLayout)
        {
            resources = slangReflection(programLayout);
        };
	    slangModuleReflection(reflectArgs);
	    return resources;
    }
} // namespace EmbeddedShader
