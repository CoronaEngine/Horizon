#include "Compiler/ShaderTypeMapping.h"

#include <Compiler/ShaderLanguageConverter.h>
#include <fstream>
#include <iostream>
#include <slang.h>
#include <sstream>
#include <Codegen/ControlFlows.h>
#include GLSL(shaders/edsl_header.glsl)

namespace
{
    std::string code = R"(struct aggregate_type_0 {
	float3 pos;
	float3 color;
	float2 tex_coord;
}
struct global_ubo_struct {
	float4x4 global_var_1;
	float4x4 global_var_2;
	float4x4 global_var_3;
}
ConstantBuffer<global_ubo_struct> global_ubo;
struct parameter_block_struct {
	Sampler2D<float4> global_var_0;
}
ParameterBlock<parameter_block_struct> global_parameter_block;
import edsl_header;
struct vertex_input {
	float3 var_1 : LOCATION0;
	float3 var_2 : LOCATION1;
	float2 var_3 : LOCATION2;
}
struct vertex_output {
	float4 position_output : SV_POSITION;
	float4 var_5 : LOCATION0;
}
[shader("vertex")]
vertex_output main(vertex_input input) {
	vertex_output output;
	aggregate_type_0 var_0;
	var_0.pos = input.var_1;
	var_0.color = input.var_2;
	var_0.tex_coord = input.var_3;
	output.position_output = mul(global_ubo.global_var_3,mul(global_ubo.global_var_2,mul(global_ubo.global_var_1,float4(var_0.pos,1.000000))));
	float3 var_4 = var_0.color;
	output.var_5 = float4(var_0.tex_coord,get_color_weight(var_4),1.000000);
	return output;
}
)";
    std::string code2 = R"(#version 450

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D texSampler2;

struct A {
    int a;
};
layout(push_constant,std430) uniform pc {
    float factor;
};

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    outColor = mix(texture(texSampler, fragTexCoord),texture(texSampler2, fragTexCoord), factor);
}
)";
}  // namespace

struct ShaderOffset
{
    size_t			byteOffset = 0;
    uint32_t 		arrayIndexInBindingRange = 0;
    uint32_t        bindingRangeIndex = 0;
    uint32_t        set = 0;        // RegisterSpace
    uint32_t        binding = 0;    // DescriptorTableSlot
};

void printArg()
{
}

template<typename T0, typename... T>
void printArg(T0 arg0,T... args)
{
    std::cout << arg0;
    printArg(args...);
}

template<typename T0>
void printArg(T0 arg0)
{
    std::cout << arg0;
}

template< typename... T>
void printWithIndent(size_t indent ,T... args)
{
    std::cout << std::string(indent, '\t');
    printArg(args...);
    std::cout << '\n';
}

std::string bindingTypeToString(slang::BindingType type)
{
    switch (type) {
    case slang::BindingType::Unknown:
        return "Unknown";
    case slang::BindingType::Sampler:
        return "Sampler";
    case slang::BindingType::Texture:
        return "Texture";
    case slang::BindingType::ConstantBuffer:
        return "ConstantBuffer";
    case slang::BindingType::ParameterBlock:
        return "ParameterBlock";
    case slang::BindingType::TypedBuffer:
        return "TypedBuffer";
    case slang::BindingType::RawBuffer:
        return "RawBuffer";
    case slang::BindingType::CombinedTextureSampler:
        return "CombinedTextureSampler";
    case slang::BindingType::InputRenderTarget:
        return "InputRenderTarget";
    case slang::BindingType::InlineUniformData:
        return "InlineUniformData";
    case slang::BindingType::RayTracingAccelerationStructure:
        return "RayTracingAccelerationStructure";
    case slang::BindingType::VaryingInput:
        return "VaryingInput";
    case slang::BindingType::VaryingOutput:
        return "VaryingOutput";
    case slang::BindingType::ExistentialValue:
        return "ExistentialValue";
    case slang::BindingType::PushConstant:
        return "PushConstant";
    case slang::BindingType::MutableFlag:
        return "MutableFlag";
    case slang::BindingType::MutableTexture:
        return "MutableTexture";
    case slang::BindingType::MutableTypedBuffer:
        return "MutableTypedBuffer";
    case slang::BindingType::MutableRawBuffer:
        return "MutableRawBuffer";
    case slang::BindingType::BaseMask:
        return "BaseMask";
    case slang::BindingType::ExtMask:
        return "ExtMask";
    }
    return "Invalid";
}

struct ShaderCursor
{
    ShaderOffset m_offset;
    slang::TypeLayoutReflection* m_typeLayout = nullptr;
    slang::VariableLayoutReflection* m_varLayout = nullptr;  // 必须正确传递
    size_t m_indent = 0;

    ShaderCursor field(int index) const
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

    ShaderCursor element(int index) const
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

    template<typename... T>
    void print(T... args)
    {
        printWithIndent(m_indent, args...);
    }
#define KEY(code) ++m_indent; code; --m_indent;

    void printInfo()
    {
        print("Var Layout Name:", m_varLayout->getName() ? m_varLayout->getName() : "null");
        print("Type Layout Name:", m_typeLayout->getName() ? m_typeLayout->getName() : "null");

        if (m_varLayout)
        {
            const char* semanticName = m_varLayout->getSemanticName();
            if (semanticName)
            {
                print("Semantic:", semanticName, m_varLayout->getSemanticIndex());
            }


            if (m_typeLayout->getParameterCategory() == slang::ParameterCategory::VaryingInput)
            {
                // VaryingInput location（如 LOCATION0, LOCATION1...）
                int varyingInLoc = m_varLayout->getOffset(slang::ParameterCategory::VaryingInput);
                // 注意：对于非 varying 参数，getOffset 可能返回 0，需结合 semantic 判断
                // 这里直接打印，entry point 参数自然会有值
                print("VaryingInput Location:", varyingInLoc);
            }

            if (m_typeLayout->getParameterCategory() == slang::ParameterCategory::VaryingOutput)
            {
                // VaryingOutput location
                int varyingOutLoc = m_varLayout->getOffset(slang::ParameterCategory::VaryingOutput);
                print("VaryingOutput Location:", varyingOutLoc);
            }
        }

        print("Offset:");
        KEY(
            print("byteOffset:", m_offset.byteOffset);
            print("arrayIndexInBindingRange:", m_offset.arrayIndexInBindingRange);
            print("bindingRangeIndex:", m_offset.bindingRangeIndex);
            print("set (RegisterSpace):", m_offset.set);
            print("binding (DescriptorTableSlot):", m_offset.binding);
        )

        int rangeCount = m_typeLayout->getBindingRangeCount();
        if (rangeCount > 0)
        {
            print("BindingRange Count:", rangeCount);
            for (int r = 0; r < rangeCount; ++r)
            {
                printWithIndent(m_indent + 1,
                    "Range ", r,
                    " type:", bindingTypeToString(m_typeLayout->getBindingRangeType(r)),
                    " count:", m_typeLayout->getBindingRangeBindingCount(r),
                    " firstDescriptorIndex:", m_typeLayout->getBindingRangeFirstDescriptorRangeIndex(r));
            }
        }

        auto kind = m_typeLayout->getKind();

        if (kind == slang::TypeReflection::Kind::Struct)
        {
            print("Field Count:", m_typeLayout->getFieldCount());
            for (int i = 0; i < m_typeLayout->getFieldCount(); ++i)
                field(i).printInfo();
        }
        else if (kind == slang::TypeReflection::Kind::ConstantBuffer ||
                 kind == slang::TypeReflection::Kind::ParameterBlock)
        {
            // 关键修正：使用 ElementVarLayout 获取元素在容器内的相对偏移
            auto elementVarLayout = m_typeLayout->getElementVarLayout();
            ShaderCursor inner = *this;
            inner.m_varLayout = elementVarLayout;
            inner.m_typeLayout = elementVarLayout->getTypeLayout();

            // 累加 element 在 DescriptorTableSlot / Uniform 上的相对偏移
            inner.m_offset.byteOffset += elementVarLayout->getOffset(slang::ParameterCategory::Uniform);
            inner.m_offset.binding += elementVarLayout->getOffset(slang::ParameterCategory::DescriptorTableSlot);
            inner.m_offset.set += elementVarLayout->getBindingSpace(slang::ParameterCategory::DescriptorTableSlot);

            inner.m_indent = m_indent + 1;
            inner.printInfo();
        }
        else if (kind == slang::TypeReflection::Kind::Array)
        {
            print("Element Count:", m_typeLayout->getElementCount());
            for (int i = 0; i < m_typeLayout->getElementCount(); ++i)
                element(i).printInfo();
        }
    }

    void collectBindings(EmbeddedShader::ShaderCodeModule::ShaderResources& resources,
                         const std::string& namePrefix = "") const
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

    // 辅助：收集 UBO 内部字段（uniformBufferMembers）
    void collectUniformBufferMembers(EmbeddedShader::ShaderCodeModule::ShaderResources& resources,
                                     const std::string& namePrefix) const
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

private:
    // 填充语义和 location（主要用于 Varying Input/Output）
    void fillSemanticAndLocation(EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo& info) const
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

    // 判断叶子节点的 BindType
    static EmbeddedShader::ShaderCodeModule::ShaderResources::BindType
    deduceLeafBindType(slang::TypeLayoutReflection* typeLayout)
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

    void collectPushConstantMembers(EmbeddedShader::ShaderCodeModule::ShaderResources& resources,
                                const std::string& namePrefix) const
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
};

void run_example_include()
{
    EmbeddedShader::SlangCompileArgs arg{};
    arg.source = code2;
    //arg.deps = {&edsl_header_glsl.slangModule};
    arg.targetLanguages = {EmbeddedShader::ShaderLanguage::SpirV};
    arg.layoutCallback = [&](slang::ProgramLayout* layout) {
    EmbeddedShader::ShaderCodeModule::ShaderResources resources;

    // ---- 1. 全局参数（UniformBuffers, ParameterBlocks, Textures 等） ----
    for (int i = 0; i < layout->getParameterCount(); ++i)
    {
        auto param = layout->getParameterByIndex(i);
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
    for (int i = 0; i < layout->getEntryPointCount(); ++i)
    {
        auto entryPoint = layout->getEntryPointByIndex(i);
        printWithIndent(0, "Processing Entry Point: ", entryPoint->getName());
        if (entryPoint)
        {
            EmbeddedShader::ShaderCodeModule::ShaderResources::EntryPointInfo epInfo;
            epInfo.name = entryPoint->getName();
            // 注意：需要确保 ShaderStage 枚举与 slang::Stage 兼容，或自行映射
            //epInfo.stage = static_cast<<ShaderStage>(entryPoint->getStage());
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

    // ---- 3. 验证输出（可删除） ----
    std::cout << "=== Collected " << resources.bindInfoPool.size() << " bindings ===\n";
    for (const auto& info : resources.bindInfoPool)
    {
        std::cout << "[" << (int)info.bindType << "] "
                  << info.variateName
                  << " | set=" << info.set
                  << " binding=" << info.binding
                  << " loc=" << info.location
                  << " offset=" << info.byteOffset
                  << " size=" << info.typeSize
                  << " count=" << info.elementCount
                  << " semantic=" << info.semantic << "\n";
    }
    std::cout << "UBO: " << resources.uniformBufferName
              << " size=" << resources.uniformBufferSize << "\n";
    std::cout << "push constant: " << resources.pushConstantName
              << " size=" << resources.pushConstantSize << "\n";
};
    arg.sourceLanguage = EmbeddedShader::ShaderLanguage::GLSL;
    auto result = EmbeddedShader::ShaderLanguageConverter::slangCompilerWithModules(arg);

    //std::cout << "[GLSL]: \n" << result.stringTargets[EmbeddedShader::ShaderLanguage::GLSL] << "\n";
}
