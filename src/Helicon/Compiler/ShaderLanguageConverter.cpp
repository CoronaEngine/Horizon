#include <iostream>

#include "ShaderLanguageConverter.h"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <utility>

namespace EmbeddedShader
{

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

    namespace
    {
        uint32_t scalarSizeInBytes(slang::TypeReflection::ScalarType st)
        {
            switch (st)
            {
                case slang::TypeReflection::ScalarType::Float64:
                case slang::TypeReflection::ScalarType::Int64:
                case slang::TypeReflection::ScalarType::UInt64: return 8;
                case slang::TypeReflection::ScalarType::Float16:
                case slang::TypeReflection::ScalarType::Int16:
                case slang::TypeReflection::ScalarType::UInt16: return 2;
                case slang::TypeReflection::ScalarType::Int8:
                case slang::TypeReflection::ScalarType::UInt8:  return 1;
                default:                                        return 4; // Float32/Int32/UInt32/未知
            }
        }

        // varying(stage input/output)按标量类型给出下游 vertex_attribute_format 依赖的 typeName。
        const char* varyingScalarTypeName(slang::TypeReflection::ScalarType st)
        {
            switch (st)
            {
                case slang::TypeReflection::ScalarType::Int8:
                case slang::TypeReflection::ScalarType::Int16:
                case slang::TypeReflection::ScalarType::Int32:
                case slang::TypeReflection::ScalarType::Int64:  return "int";
                case slang::TypeReflection::ScalarType::UInt8:
                case slang::TypeReflection::ScalarType::UInt16:
                case slang::TypeReflection::ScalarType::UInt32:
                case slang::TypeReflection::ScalarType::UInt64: return "uint";
                default:                                        return "float";
            }
        }

        // 计算 varying 叶子的分量数 / 字节大小 / 标量类型名（对齐旧 spirv-cross 行为：
        // elementCount = vecsize*columns, typeSize = 4*vecsize*columns）。
        // varying 不占 uniform 存储，getSize() 返回 0，故必须按向量/矩阵宽度推导。
        void fillVaryingWidth(slang::TypeLayoutReflection* typeLayout,
                              EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo& info)
        {
            const auto kind = typeLayout->getKind();
            if (kind == slang::TypeReflection::Kind::Vector)
            {
                info.elementCount = static_cast<uint64_t>(typeLayout->getElementCount());
                info.typeSize = static_cast<uint32_t>(info.elementCount * scalarSizeInBytes(typeLayout->getScalarType()));
                info.typeName = varyingScalarTypeName(typeLayout->getScalarType());
            }
            else if (kind == slang::TypeReflection::Kind::Matrix)
            {
                info.elementCount = static_cast<uint64_t>(typeLayout->getRowCount() * typeLayout->getColumnCount());
                info.typeSize = static_cast<uint32_t>(info.elementCount * scalarSizeInBytes(typeLayout->getScalarType()));
                info.typeName = varyingScalarTypeName(typeLayout->getScalarType());
            }
            else if (kind == slang::TypeReflection::Kind::Scalar)
            {
                info.elementCount = 1;
                info.typeSize = scalarSizeInBytes(typeLayout->getScalarType());
                info.typeName = varyingScalarTypeName(typeLayout->getScalarType());
            }
        }
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

            // __DynamicResource<General>[] (bindless 句柄数组, codegen 在 set 0/1/2 生成)。
            // 这类泛型动态资源在 ProgramLayout 反射里 getBindingRangeCount()==0、kind=DynamicResource,
            // deduceLeafBindType 无法识别而返回 none —— 但它们正是 bindless 保留集的载体。
            // 只有落到 SPIR-V 才 lower 成真实 storage image/buffer;此处按 codegen 契约的 set 归类,
            // 使其进入 bindInfoPool,让 runtime 的 uses_bindless_descriptors()/is_bindless_reserved_binding()
            // 能正确判定 bindless。set 语义见 ResourceManager::bindless_*_set (0=texture,1=buffer,2=image)。
            if (elemBindType == EmbeddedShader::ShaderCodeModule::ShaderResources::none &&
                elemTypeLayout->getKind() == slang::TypeReflection::Kind::DynamicResource)
            {
                switch (m_offset.set)
                {
                case 0: // combinedTextureSamplerHandles → 采样图像/组合采样器
                    elemBindType = EmbeddedShader::ShaderCodeModule::ShaderResources::sampledImages;
                    break;
                case 1: // bufferHandles → 存储缓冲
                    elemBindType = EmbeddedShader::ShaderCodeModule::ShaderResources::storageBuffer;
                    break;
                case 2: // textureHandles → 存储图像
                    elemBindType = EmbeddedShader::ShaderCodeModule::ShaderResources::storageTexture;
                    break;
                default:
                    break;
                }
            }

            if (elemBindType != EmbeddedShader::ShaderCodeModule::ShaderResources::none)
            {
                EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo info;
                info.variateName = namePrefix;
                info.typeName = elemTypeLayout->getName() ? elemTypeLayout->getName() : "unknown";
                info.set = m_offset.set;
                info.binding = m_offset.binding;
                info.byteOffset = m_offset.byteOffset;
                info.typeSize = static_cast<uint32_t>(elemTypeLayout->getSize());
                // 无界数组(bindless 句柄表)getElementCount() 返回哨兵值(unbounded/unknown),
                // 归一化为 0,既满足 bindless 检查的 elementCount!=1,又不把巨大哨兵值写进反射。
                info.elementCount = (elemCount == SLANG_UNBOUNDED_SIZE || elemCount == SLANG_UNKNOWN_SIZE ||
                                     elemCount == static_cast<uint64_t>(std::numeric_limits<int32_t>::max()) ||
                                     elemCount == static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()))
                                        ? 0
                                        : elemCount;
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

            // varying(顶点输入/输出)不占 uniform 存储, getSize()=0 且分量数需从向量宽度推导。
            // 下游 vertex_attribute_format 依赖 elementCount(分量数)+typeName 决定 VkFormat,
            // 顶点属性 offset 累加依赖 typeSize。仅对 varying 修正, 不影响 descriptor 语义。
            if (bindType == EmbeddedShader::ShaderCodeModule::ShaderResources::stageInputs ||
                bindType == EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs)
            {
                fillVaryingWidth(m_typeLayout, info);
            }

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

	void diagnoseIfNeeded(slang::IBlob* diagnosticsBlob)
	{
		if (diagnosticsBlob != nullptr)
		{
			std::cout << static_cast<const char*>(diagnosticsBlob->getBufferPointer()) << std::endl;
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

        //load modules
        {
	        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
	        for (auto module : arg.deps)
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


    Slang::ComPtr<slang::IGlobalSession> ShaderLanguageConverter::getGlobalSession()
    {
        initSlangGlobalSession();
        return slangGlobalSession;
    }
    Slang::ComPtr<slang::IModule> ShaderLanguageConverter::loadModule(Slang::ComPtr<slang::ISession> session,
                                                                      std::string_view moduleName,
                                                                      std::string_view moduleSource)
    {
        Slang::ComPtr<slang::IModule> srcModule;
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        srcModule = session->loadModuleFromSourceString(moduleName.data(), moduleName.data(), moduleSource.data(),
                                                        diagnosticsBlob.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
        if (!srcModule)
        {
            throw std::runtime_error("Failed to load Slang module.");
        }
        return srcModule;
    }
    Slang::ComPtr<slang::IModule> ShaderLanguageConverter::loadModule(Slang::ComPtr<slang::ISession> session,
                                                                      const SlangModule &slangModule)
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        Slang::ComPtr<slang::IModule> mod;
        auto dataBlob = slang_createBlob(slangModule.binData.data(), slangModule.binData.size());
        mod = session->loadModuleFromIRBlob(slangModule.name.c_str(), slangModule.path.c_str(), dataBlob,
                                                 diagnosticsBlob.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
        if (!mod)
        {
            std::cout << "Load Module From IR Blob failed: " << slangModule.name << std::endl;
        }
        return mod;
    }
    SlangModule ShaderLanguageConverter::convertModule(Slang::ComPtr<slang::IModule> mod)
    {
        Slang::ComPtr<slang::IBlob> moduleBlob;
        {
            auto result = mod->serialize(moduleBlob.writeRef());
            if (result != SLANG_OK || !moduleBlob)
            {
                throw std::runtime_error("Failed to serialize Slang module.");
            }
        }
        SlangModule module;
        module.name = mod->getName();
        module.path = mod->getFilePath();
        module.binData =
            std::vector(static_cast<uint8_t const *>(moduleBlob->getBufferPointer()),
                        static_cast<uint8_t const *>(moduleBlob->getBufferPointer()) + moduleBlob->getBufferSize());
        return module;
    }
    ShaderLanguage ShaderLanguageConverter::convertShaderLanguage(SlangCompileTarget target)
    {
        switch (target)
        {
            case SLANG_SPIRV:
                return ShaderLanguage::SpirV;
            case SLANG_DXIL:
                return ShaderLanguage::DXIL;
            case SLANG_HLSL:
                return ShaderLanguage::HLSL;
            case SLANG_GLSL:
                return ShaderLanguage::GLSL;
            case SLANG_DXBC:
                return ShaderLanguage::DXBC;
            default:
                return ShaderLanguage::Slang;
        }
    }

    Slang::ComPtr<slang::IComponentType>
    ShaderLanguageConverter::link(Slang::ComPtr<slang::ISession> session,
                                  std::span<slang::IComponentType *> composeComponents)
    {
        Slang::ComPtr<slang::IComponentType> target;
        Slang::ComPtr<slang::IComponentType> composedProgram;
        {
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            SlangResult result =
                session->createCompositeComponentType(composeComponents.data(), composeComponents.size(),
                                                      composedProgram.writeRef(), diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (SLANG_FAILED(result))
                throw std::runtime_error("Failed to create composite component type in Slang.");
        }

        {
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            SlangResult result = composedProgram->link(target.writeRef(), diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (SLANG_FAILED(result))
                throw std::runtime_error("Failed to link Slang program.");
        }
        return target;
    }
    Slang::ComPtr<slang::IEntryPoint> ShaderLanguageConverter::findEntryPoint(Slang::ComPtr<slang::IModule> mod,
                                                                              std::string_view name)
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        mod->findEntryPointByName(name.data(), entryPoint.writeRef());
        diagnoseIfNeeded(diagnosticsBlob);
        if (!entryPoint)
        {
            throw std::runtime_error("Failed to find entry point.");
        }
        return entryPoint;
    }
    Slang::ComPtr<slang::IEntryPoint> ShaderLanguageConverter::tryFindEntryPoint(Slang::ComPtr<slang::IModule> mod,
                                                                                 std::string_view name,
                                                                                 ShaderStage stage)
    {
        Slang::ComPtr<slang::IBlob> diagnosticsBlob;
        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        mod->findEntryPointByName(name.data(), entryPoint.writeRef());

        if (!entryPoint)
        {
            // 针对非shader attr标注的入口点查找
            mod->findAndCheckEntryPoint(name.data(), toSlangStage(stage), entryPoint.writeRef(),
                                        diagnosticsBlob.writeRef());
        }
        return entryPoint;
    }
    Slang::ComPtr<slang::ISession>
    ShaderLanguageConverter::createSession(const Slang::ComPtr<slang::IGlobalSession> &globalSession,
                                           std::span<slang::TargetDesc> targetDesc,
                                           std::span<slang::CompilerOptionEntry> options,
                                           bool columnMajor)
    {
        slang::SessionDesc sessionDesc = {};
        sessionDesc.targets = targetDesc.data();
        sessionDesc.targetCount = targetDesc.size();
        sessionDesc.compilerOptionEntries = options.data();
        sessionDesc.compilerOptionEntryCount = options.size();
        sessionDesc.defaultMatrixLayoutMode = columnMajor
            ? SLANG_MATRIX_LAYOUT_COLUMN_MAJOR
            : SLANG_MATRIX_LAYOUT_ROW_MAJOR;
        Slang::ComPtr<slang::ISession> session;
        globalSession->createSession(sessionDesc, session.writeRef());
        return session;
    }

    Slang::ComPtr<slang::IBlob> ShaderLanguageConverter::getFinalCode(Slang::ComPtr<slang::IComponentType> program, SlangInt targetIndex, bool isLibrary)
    {
        Slang::ComPtr<slang::IBlob> finalCode;
        if (!isLibrary)
        {
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            SlangResult result = program->getEntryPointCode(
                0,
                targetIndex,
                finalCode.writeRef(),
                diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (SLANG_FAILED(result))
                throw std::runtime_error("Failed to get target code from Slang program.");
        }
        else
        {
            Slang::ComPtr<slang::IBlob> diagnosticsBlob;
            SlangResult result = program->getTargetCode(
                targetIndex,
                finalCode.writeRef(),
                diagnosticsBlob.writeRef());
            diagnoseIfNeeded(diagnosticsBlob);
            if (SLANG_FAILED(result))
                throw std::runtime_error("Failed to get target code from Slang program.");
        }
        return finalCode;
    }

    void ShaderLanguageConverter::fillCompileResult(SlangCompileResult &result,
                                                      Slang::ComPtr<slang::IComponentType> program,
                                                      SlangInt targetIndex, SlangCompileTarget dstLang,bool isNeedReflection,bool isLibrary)
    {
        auto finalCode = getFinalCode(program, targetIndex, isLibrary);
        bool isBin = false;
	    switch (dstLang)
	    {
	    case SLANG_SPIRV:
	    case SLANG_DXBC:
	    case SLANG_DXIL:
	        isBin = true;
	        break;
	    default:
	        break;
	    }
	    auto targetLang = convertShaderLanguage(dstLang);
        if (isNeedReflection)
        {
            auto resource = slangReflection(program->getLayout(targetIndex));
            result.reflections.insert({targetLang, std::move(resource)});
        }

	    if (isBin)
	    {
	        SlangCompileResult::BinaryTarget target(finalCode->getBufferSize() / sizeof(uint32_t));
	        memcpy(target.data(), finalCode->getBufferPointer(),
                   finalCode->getBufferSize());


	        result.binaryTargets.insert({targetLang, std::move(target)});
	        return;
	    }

	    SlangCompileResult::StringTarget target;
	    target.resize(finalCode->getBufferSize() / sizeof(char));
	    memcpy(target.data(), finalCode->getBufferPointer(), finalCode->getBufferSize());
	    result.stringTargets.insert({targetLang, std::move(target)});
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

            // 顶层参数的 descriptor set 取法(已实测 SpirV target 逐 compile/逐字段确认):
            //   - ParameterBlock(kind=11): 其内容独占一个寄存器空间, set 来自
            //     getOffset(SubElementRegisterSpace)。non-bindless 的 global_parameter_block 由此得 set=1。
            //   - 其余全部(ConstantBuffer UBO + 普通纹理/采样器/buffer + bindless 句柄数组 __DynamicResource[]):
            //     set 来自 getBindingSpace(DescriptorTableSlot)。
            //       · bindless global_ubo([[vk::binding(0,3)]] ConstantBuffer) → 3
            //       · textureHandles([vk::binding(0,2)]) → 2, bufferHandles → 1, combinedTextureSamplerHandles → 0
            //       · non-bindless global_ubo(无显式 binding) → 0
            //       · 真 push_constant(getParameterCategory==PushConstantBuffer)DTS=0, 但 set 对 push constant 无意义
            //         (collectBindings 走 PushConstantBuffer 分支, 进 VkPushConstantRange 而非 descriptor set)。
            // 旧代码对非 ParameterBlock 统一用 getOffset(RegisterSpace) 恒返回 0 → set 2/3 资源全被误报到 set 0。
            // 注意: ConstantBuffer 不能并入 ParameterBlock 用 SubElementRegisterSpace —— 那对 global_ubo 返回 0,
            // 会让 bindless UBO 落到 set 0(bindless 保留集)从而崩溃(rasterizer add_descriptor_binding 抛异常)。
            if (param->getTypeLayout()->getKind() == slang::TypeReflection::Kind::ParameterBlock)
            {
                cursor.m_offset.set = param->getOffset(slang::ParameterCategory::SubElementRegisterSpace);
            }
            else
            {
                cursor.m_offset.set = param->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot);
            }
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

                if (epInfo.stage == ShaderStage::ComputeShader)
                {
                    SlangUInt sizes[3] = { 1, 1, 1 };
                    entryPoint->getComputeThreadGroupSize(std::size(sizes), sizes);
                    epInfo.numthreads = ktm::uvec3(
                        static_cast<unsigned>(sizes[0]),
                        static_cast<unsigned>(sizes[1]),
                        static_cast<unsigned>(sizes[2])
                    );
                }
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