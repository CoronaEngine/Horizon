//
// Created by Zero on 06/08/2022.
//

#include "dxc_compiler.h"
#include "shader_reflection.h"
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include "dxcapi.h"
#include <wrl/client.h>

using namespace Microsoft::WRL;
#endif

namespace ocarina {

class CustomIncludeHandler : public IDxcIncludeHandler {
public:
    CustomIncludeHandler(IDxcUtils* utils) 
    { 
        pUtils = utils; 
    }

    HRESULT try_load_file(const char *file_full_path, IDxcBlobEncoding** output) {
        std::ifstream is(file_full_path);
        if (is.is_open()) {
            is.seekg(0, std::ios::end);
            size_t size = is.tellg();
            is.seekg(0, std::ios::beg);
            char *shaderCode = new char[size + 1];
            shaderCode[size] = '\0';
            memset(shaderCode, 0, size + 1);
            is.read(shaderCode, size + 1);
            is.close();
            std::string shader_code(shaderCode);
            HRESULT hr = pUtils->CreateBlob(shader_code.data(), shader_code.size(), 0, output);
            
            delete[] shaderCode;
            return hr;
        }

        return E_FAIL;
    }

    HRESULT STDMETHODCALLTYPE 
        LoadSource(_In_ LPCWSTR pFilename, _COM_Outptr_result_maybenull_ IDxcBlob **ppIncludeSource) override {
        ComPtr<IDxcBlobEncoding> pEncoding;
        std::string file_name = wstring_to_string(pFilename);
        std::string path = get_file_directory(file_name);//Paths::Normalize(UNICODE_TO_MULTIBYTE(pFilename));
        //file_name = get_file_name(file_name);
        //gVerify(Paths::ResolveRelativePaths(path), == true);

        auto existingInclude = std::find_if(IncludedFiles.begin(), IncludedFiles.end(), [&path](const std::string &include) {
            return strcmp(include.c_str(), path.c_str()) == 0;
        });

        if (existingInclude != IncludedFiles.end()) {
            static const char nullStr[] = " ";
            pUtils->CreateBlobFromPinned(nullStr, ARRAYSIZE(nullStr), DXC_CP_ACP, pEncoding.GetAddressOf());
            *ppIncludeSource = pEncoding.Detach();
            return S_OK;
        }

        HRESULT hr = try_load_file(file_name.c_str(), &pEncoding);
        if (SUCCEEDED(hr)) {
            IncludedFiles.push_back(file_name);
            *ppIncludeSource = pEncoding.Detach();
        }
        return hr;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject) override { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }

    std::vector<std::string> IncludedFiles;
    IDxcUtils* pUtils;
    std::string shader_file_directory;
};

bool DXCCompiler::preprocess(const char *hlsl, uint32_t size, const std::string &full_file_path, VkShaderStageFlagBits stage, const std::vector<std::string> &include_paths, std::string &plattern_hlsl) {
    ComPtr<IDxcUtils> dxc_utils = {};
    ComPtr<IDxcCompiler3> dxc_compiler = {};

    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxc_utils.ReleaseAndGetAddressOf()));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxc_compiler.ReleaseAndGetAddressOf()));

    std::vector<LPCWSTR> args;
    args.push_back(DXC_ARG_PACK_MATRIX_COLUMN_MAJOR);
    args.push_back(L"-HV");
    args.push_back(L"2021");
    args.push_back(L"-T");
    if (stage == VK_SHADER_STAGE_VERTEX_BIT) {
        args.push_back(L"vs_6_0");
    } else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        args.push_back(L"ps_6_0");
    } else if (stage == VK_SHADER_STAGE_COMPUTE_BIT) {
        args.push_back(L"cs_6_0");
    }

    std::string file_dir = get_file_directory(full_file_path);
    args.push_back(L"-I");
    std::wstring wpath = string_to_wstring(file_dir);
    args.push_back(wpath.c_str());
    for (size_t i = 0; i < include_paths.size(); ++i)
    {
        args.push_back(L"-I");
        args.push_back(std::wstring(include_paths[i].begin(), include_paths[i].end()).c_str());
    }

    args.push_back(L"-P");
    args.push_back(L"-fspv-reflect");

    //args.push_back(L"-E");
    //std::wstring wEntry(entryPoint.begin(), entryPoint.end());
    //args.push_back(wEntry.c_str());
    //args.push_back(L"-spirv");
    //args.push_back(L"-fspv-target-env=vulkan1.1");

    DxcBuffer src_buffer = {
        hlsl,
        static_cast<std::uint32_t>(size),
        0,
    };

    CustomIncludeHandler preprocessIncludeHandler(dxc_utils.Get());

    ComPtr<IDxcResult> operationResult;
    HRESULT hr = dxc_compiler->Compile(&src_buffer, args.data(), args.size(), &preprocessIncludeHandler, IID_PPV_ARGS(&operationResult));

    ComPtr<IDxcBlobUtf8> errors = nullptr;
    hr = operationResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

    std::string errorLog;
    if (errors != nullptr && errors->GetStringLength() > 0) {
        errorLog = errors->GetStringPointer();
        spdlog::error("Preprocess failed with error: \"{}\"\n", errorLog);
    }

    ComPtr<IDxcBlobUtf8> shader_obj;
    hr = operationResult->GetOutput(DXC_OUT_HLSL, IID_PPV_ARGS(&shader_obj), nullptr);
    if (shader_obj != nullptr) {
        size_t string_len = shader_obj->GetStringLength();
        plattern_hlsl = shader_obj->GetStringPointer();
    }

    ComPtr<IDxcBlob> pReflectionData;
    if (SUCCEEDED(operationResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(pReflectionData.GetAddressOf()), nullptr))) {
        DxcBuffer reflectionBuffer;
        reflectionBuffer.Ptr = pReflectionData->GetBufferPointer();
        reflectionBuffer.Size = pReflectionData->GetBufferSize();
        reflectionBuffer.Encoding = 0;
    }

    return plattern_hlsl.size() > 0;
}

bool DXCCompiler::compile_hlsl_spriv(const CompileInput &input, CompileResult &result) {
    ComPtr<IDxcUtils> dxc_utils = {};
    ComPtr<IDxcCompiler3> dxc_compiler = {};

    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxc_utils.ReleaseAndGetAddressOf()));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxc_compiler.ReleaseAndGetAddressOf()));

    std::vector<LPCWSTR> args;
    args.push_back(DXC_ARG_PACK_MATRIX_COLUMN_MAJOR);
    args.push_back(L"-HV");
    args.push_back(L"2021");
    args.push_back(L"-T");
    if (input.shader_type == ShaderType::VertexShader) {
        args.push_back(L"vs_6_0");
    } else if (input.shader_type == ShaderType::PixelShader) {
        args.push_back(L"ps_6_0");
    } else if (input.shader_type == ShaderType::ComputeShader) {
        args.push_back(L"cs_6_0");
    }

    std::string file_dir = get_file_directory(input.full_file_path);
    args.push_back(L"-I");
    std::wstring wpath = string_to_wstring(file_dir);
    args.push_back(wpath.c_str());
    for (size_t i = 0; i < input.include_paths.size(); ++i) {
        args.push_back(L"-I");
        args.push_back(std::wstring(input.include_paths[i].begin(), input.include_paths[i].end()).c_str());
    }

    args.push_back(L"-E");
    std::wstring wEntry(input.entry.begin(), input.entry.end());
    args.push_back(wEntry.c_str());
    args.push_back(L"-spirv");
    args.push_back(L"-fspv-target-env=vulkan1.1");

    for (auto& option : input.macros)
    {
        args.push_back(L"-D");
        std::wstring woption(option.begin(), option.end());
        args.push_back(woption.c_str());
    }

    DxcBuffer src_buffer = {
        input.hlsl.data(),
        input.hlsl.size(),
        0};

    CustomIncludeHandler preprocessIncludeHandler(dxc_utils.Get());

    ComPtr<IDxcResult> operationResult;
    HRESULT hr = dxc_compiler->Compile(&src_buffer, args.data(), args.size(), &preprocessIncludeHandler, IID_PPV_ARGS(&operationResult));

    ComPtr<IDxcBlobUtf8> errors = nullptr;
    hr = operationResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

   
    if (errors != nullptr && errors->GetStringLength() > 0) {
        result.error = errors->GetStringPointer();
        spdlog::error("Preprocess failed with error: \"{}\"\n", result.error);
    }

    ComPtr<IDxcBlob> shader_obj;
    operationResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_obj), nullptr);
    result.spriv_codes.resize(shader_obj->GetBufferSize() / sizeof(uint32_t));

    for (size_t i = 0; i < result.spriv_codes.size(); ++i) {
        uint32_t spvCode = static_cast<uint32_t *>(shader_obj->GetBufferPointer())[i];
        result.spriv_codes[i] = spvCode;
    }

    return result.spriv_codes.size() > 0;
}

void DXCCompiler::run_spriv_reflection(const std::vector<uint32_t> &spriv, ShaderType shader_type, ShaderReflection &shader_reflection) {
    ComPtr<IDxcUtils> dxc_utils{};
    ComPtr<IDxcContainerReflection> dxc_container_reflection{};

    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxc_utils.ReleaseAndGetAddressOf()));
    DxcCreateInstance(CLSID_DxcContainerReflection, IID_PPV_ARGS(dxc_container_reflection.ReleaseAndGetAddressOf()));

    const void *shader_data = static_cast<const void*>(spriv.data());
    uint32_t shader_data_size = spriv.size() * sizeof(uint32_t);
    spirv_cross::Compiler spirvmodule((const uint32_t *)shader_data, shader_data_size / 4);

    // The SPIR-V is now parsed, and we can perform reflection on it.
    std::string entryPointName;
    spv::ExecutionModel executionModel;
    switch (shader_type) {
        case ShaderType::VertexShader:
            executionModel = spv::ExecutionModelVertex;
            break;
        case ShaderType::PixelShader:
            executionModel = spv::ExecutionModelFragment;
            break;
        case ShaderType::ComputeShader:
            executionModel = spv::ExecutionModelGLCompute;
            break;
        default:
            break;
    }

    //spirv_cross::SPIREntryPoint &execution = spirvmodule.get_entry_point(entryPointName, executionModel);
    spirv_cross::SmallVector<spirv_cross::EntryPoint> entrypoints = spirvmodule.get_entry_points_and_stages();
    spirv_cross::EntryPoint &entryPoint = entrypoints[0];

    spirv_cross::SPIREntryPoint &execution = spirvmodule.get_entry_point(entryPoint.name, entryPoint.execution_model);

    // remove unused variables
    auto active = spirvmodule.get_active_interface_variables();
    spirv_cross::ShaderResources resources = spirvmodule.get_shader_resources(active);
    spirvmodule.set_enabled_interface_variables(move(active));

    if (shader_type == ShaderType::ComputeShader) {
        spirv_cross::SpecializationConstant wg_x, wg_y, wg_z;
        spirvmodule.get_work_group_size_specialization_constants(wg_x, wg_y, wg_z);

        //reflectionData.m_ThreadGroupSize[0] = execution.workgroup_size.x;
        //reflectionData.m_ThreadGroupSize[1] = execution.workgroup_size.y;
        //reflectionData.m_ThreadGroupSize[2] = execution.workgroup_size.z;
        shader_reflection.thread_group_size[0] = execution.workgroup_size.x;
        shader_reflection.thread_group_size[1] = execution.workgroup_size.y;
        shader_reflection.thread_group_size[2] = execution.workgroup_size.z;
    }

    for (spirv_cross::Resource &resource : resources.uniform_buffers) {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        const spirv_cross::SPIRType &type = spirvmodule.get_type(resource.base_type_id);
        uint32_t size = spirvmodule.get_declared_struct_size(type);

        ShaderReflection::UniformBuffer ubo{};
        ubo.name = spirvmodule.get_name(resource.id);
        ubo.size = size;
        ubo.binding = binding;

        // Now loop through all members
        for (uint32_t i = 0; i < type.member_types.size(); ++i) {
            ShaderReflection::ShaderVariable shader_variable{};
            shader_variable.name = spirvmodule.get_member_name(resource.base_type_id, i);
            shader_variable.size = spirvmodule.get_declared_struct_member_size(type, i);
            
            shader_variable.offset = spirvmodule.type_struct_member_offset(type, i);
            const spirv_cross::SPIRType &member_type = spirvmodule.get_type(type.member_types[i]);
            uint32_t vec_size = member_type.vecsize;// e.g. 4 for float4
            uint32_t columns = member_type.columns; // 4 columns = matrix
            uint32_t array_size = member_type.array.size() > 0 ? member_type.array[0] : 0;

            shader_variable.variable_type = get_shader_variable_type(vec_size, columns, member_type);
            ubo.shader_variables.emplace_back(std::move(shader_variable)); 
        }
        shader_reflection.uniform_buffers.emplace_back(std::move(ubo));
    }

    for (spirv_cross::Resource &resource : resources.push_constant_buffers) {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        const spirv_cross::SPIRType &type = spirvmodule.get_type(resource.base_type_id);
        uint32_t size = spirvmodule.get_declared_struct_size(type);

        ShaderReflection::UniformBuffer push_constant{};
        push_constant.name = spirvmodule.get_name(resource.id);
        push_constant.size = size;

        // Now loop through all members
        for (uint32_t i = 0; i < type.member_types.size(); ++i) {
            ShaderReflection::ShaderVariable shader_variable{};
            shader_variable.name = spirvmodule.get_member_name(resource.base_type_id, i);
            shader_variable.size = spirvmodule.get_declared_struct_member_size(type, i);

            shader_variable.offset = spirvmodule.type_struct_member_offset(type, i);

            const spirv_cross::SPIRType &member_type = spirvmodule.get_type(type.member_types[i]);
            uint32_t vec_size = member_type.vecsize;// e.g. 4 for float4
            uint32_t columns = member_type.columns; // 4 columns = matrix
            uint32_t array_size = member_type.array.size() > 0 ? member_type.array[0] : 0;

            shader_variable.variable_type = get_shader_variable_type(vec_size, columns, member_type);
            push_constant.shader_variables.emplace_back(std::move(shader_variable));
        }
        shader_reflection.push_constant_buffers.emplace_back(std::move(push_constant));
    }

    for (spirv_cross::Resource& resource : resources.sampled_images)
    {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        ShaderReflection::ShaderResource shader_resource;
        shader_resource.name = spirvmodule.get_name(resource.id);
        shader_resource.descriptor_set = set;
        shader_resource.binding = binding;
        shader_resource.parameter_type = ShaderReflection::ResourceType::SRV;
        shader_resource.shader_type = (uint32_t)shader_type;
        shader_reflection.shader_resources.push_back(std::move(shader_resource));
    }

    for (spirv_cross::Resource& resource : resources.separate_images)
    {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);
        ShaderReflection::ShaderResource shader_resource;
        shader_resource.name = spirvmodule.get_name(resource.id);
        shader_resource.descriptor_set = set;
        shader_resource.binding = binding;
        shader_resource.parameter_type = ShaderReflection::ResourceType::SRV;
        shader_resource.shader_type = (uint32_t)shader_type;
        shader_reflection.shader_resources.push_back(std::move(shader_resource));
    }

    for (spirv_cross::Resource& resource : resources.separate_samplers)
    {
        uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
        uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);

        ShaderReflection::ShaderResource shader_resource;
        shader_resource.name = spirvmodule.get_name(resource.id);
        shader_resource.descriptor_set = set;
        shader_resource.binding = binding;
        shader_resource.parameter_type = ShaderReflection::ResourceType::Sampler;
        shader_resource.shader_type = (uint32_t)shader_type;
        shader_reflection.shader_resources.push_back(std::move(shader_resource));
    }

    if (shader_type == ShaderType::VertexShader)
    {
        uint32_t offset = 0;
        uint32_t vertex_attrib_index = 0;
        uint32_t total_size = 0;
        for (spirv_cross::Resource& resource : resources.stage_inputs)
        {
            uint32_t set = spirvmodule.get_decoration(resource.id, spv::DecorationDescriptorSet);
            uint32_t binding = spirvmodule.get_decoration(resource.id, spv::DecorationBinding);
            uint32_t location = spirvmodule.get_decoration(resource.id, spv::DecorationLocation);
            uint32_t offset_in_hlsl = spirvmodule.get_decoration(resource.id, spv::DecorationOffset);
            spirv_cross::SPIRType spirType = spirvmodule.get_type(resource.type_id);

            VkFormat format = VK_FORMAT_UNDEFINED;

            if (spirType.basetype == spirv_cross::SPIRType::Float) {
                if (spirType.vecsize == 1)
                    format = VK_FORMAT_R32_SFLOAT;
                else if (spirType.vecsize == 2)
                    format = VK_FORMAT_R32G32_SFLOAT;
                else if (spirType.vecsize == 3)
                    format = VK_FORMAT_R32G32B32_SFLOAT;
                else if (spirType.vecsize == 4)
                    format = VK_FORMAT_R32G32B32A32_SFLOAT;
            } else if (spirType.basetype == spirv_cross::SPIRType::Int) {
                if (spirType.vecsize == 1)
                    format = VK_FORMAT_R32_SINT;
                else if (spirType.vecsize == 2)
                    format = VK_FORMAT_R32G32_SINT;
                else if (spirType.vecsize == 3)
                    format = VK_FORMAT_R32G32B32_SINT;
                else if (spirType.vecsize == 4)
                    format = VK_FORMAT_R32G32B32A32_SINT;
            } else if (spirType.basetype == spirv_cross::SPIRType::UInt) {
                if (spirType.vecsize == 1)
                    format = VK_FORMAT_R32_UINT;
                else if (spirType.vecsize == 2)
                    format = VK_FORMAT_R32G32_UINT;
                else if (spirType.vecsize == 3)
                    format = VK_FORMAT_R32G32B32_UINT;
                else if (spirType.vecsize == 4)
                    format = VK_FORMAT_R32G32B32A32_UINT;
            }

            uint32_t size = spirType.vecsize * 4;

            if (vertex_attrib_index > 0 && offset_in_hlsl == 0) {
                offset += total_size;
            } else {
                offset = offset_in_hlsl;
            }

            ShaderReflection::ShaderResource shader_resource;
            shader_resource.name = spirvmodule.get_name(resource.id);
            shader_resource.descriptor_set = set;
            shader_resource.register_ = binding;
            shader_resource.location = location;
            shader_resource.offset = offset;
            shader_resource.shader_type = (uint32_t)shader_type;
            shader_resource.parameter_type = ShaderReflection::ResourceType::InputAttachment;
            shader_resource.format = format;
            std::string semantic;
            if (spirvmodule.has_decoration(resource.id, spv::DecorationHlslSemanticGOOGLE)) {
                semantic = spirvmodule.get_decoration_string(resource.id, spv::DecorationHlslSemanticGOOGLE);
            } else {
                if (shader_resource.name.find("in.var.") == 0) {
                    semantic = shader_resource.name.substr(strlen("in.var."));
                } else {
                    semantic = shader_resource.name;
                }
            }
            shader_resource.vertex_attribute_type = VertexAttributeType::from_string(semantic.c_str());

            shader_reflection.input_layouts.push_back(std::move(shader_resource));

            vertex_attrib_index++;
            total_size += size;
        }
    }
}

ShaderVariableType DXCCompiler::get_shader_variable_type(uint32_t vec_size, uint32_t column_size, const spirv_cross::SPIRType &type) {
    if (type.basetype == spirv_cross::SPIRType::Float) {
        if (vec_size == 1 && column_size == 1)
            return ShaderVariableType::FLOAT;
        else if (vec_size == 2 && column_size == 1)
            return ShaderVariableType::FLOAT2;
        else if (vec_size == 3 && column_size == 1)
            return ShaderVariableType::FLOAT3;
        else if (vec_size == 4 && column_size == 1)
            return ShaderVariableType::FLOAT4;
        else if (vec_size == 4 && column_size == 4)
            return ShaderVariableType::FLOAT4X4;
    } else if (type.basetype == spirv_cross::SPIRType::Int || type.basetype == spirv_cross::SPIRType::UInt) {
        if (vec_size == 1 && column_size == 1)
            return ShaderVariableType::INT;
        else if (vec_size == 2 && column_size == 1)
            return ShaderVariableType::INT2;
        else if (vec_size == 3 && column_size == 1)
            return ShaderVariableType::INT3;
        else if (vec_size == 4 && column_size == 1)
            return ShaderVariableType::INT4;
    }
    return ShaderVariableType::FLOAT;
}

}// namespace ocarina