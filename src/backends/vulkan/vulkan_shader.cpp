//
// Created by Zero on 06/08/2022.
//

#include "vulkan_shader.h"
#include "util.h"
#include "vulkan_device.h"
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include "dxcapi.h"

#include <wrl/client.h>
using namespace Microsoft::WRL;
#endif

#include "dxc_compiler.h"

namespace ocarina {

VulkanShader::VulkanShader(VulkanDevice *device, std::span<uint32_t> shaderCode, const std::string_view &entryPoint, VkShaderStageFlagBits stage) : 
    entry_(entryPoint), device_(device), stage_(stage) {
    VkShaderModuleCreateInfo moduleCreateInfo{};
    moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleCreateInfo.codeSize = shaderCode.size() * sizeof(uint32_t);
    moduleCreateInfo.pCode = (uint32_t *)shaderCode.data();
    vkCreateShaderModule(device->logicalDevice(), &moduleCreateInfo, nullptr, &shader_module_);
}

VulkanShader::~VulkanShader() {
    vkDestroyShaderModule(device_->logicalDevice(), shader_module_, nullptr);
}

VulkanShader* VulkanShader::create(Device::Impl* device,
                                   ShaderType shader_type, 
    std::span<uint32_t> shaderCode,
    const std::string_view& entryPoint)
{
    return ocarina::new_with_allocator<VulkanShader>(static_cast<VulkanDevice *>(device), shaderCode, entryPoint, get_vulkan_shader_stage(shader_type));
}

VulkanShader *VulkanShader::create_from_HLSL(Device::Impl *device, ShaderType shader_type, const std::string &filename, const std::string &entry_point) {
    std::ifstream is(filename.data());
    VulkanShader *vulkan_shader = nullptr;
    if (is.is_open()) {
        is.seekg(0, std::ios::end);
        size_t size = is.tellg();
        is.seekg(0, std::ios::beg);
        char *shaderCode = new char[size + 1];
        memset(shaderCode, 0, size + 1);
        is.read(shaderCode, size);

        is.close();

        assert(size > 0);

        VkShaderStageFlagBits stage = VulkanShader::convert_vulkan_shader_stage(shader_type);
        std::string flattern_hlsl;
        std::string full_directory = get_file_directory(filename);
        std::vector<std::string> include_paths;
        
        
        if (DXCCompiler::preprocess(shaderCode, size, filename, stage, include_paths, flattern_hlsl))
        {
            std::vector<uint32_t> spriv_code;
            std::string errors;
        }

        CompileInput compile_input{
            .hlsl = shaderCode,
            .entry = entry_point,
            .full_file_path = filename,
            .shader_type = shader_type,
            .output_pdbs = false,
        };
        delete[] shaderCode;
        CompileResult compile_result;
        bool compiled = DXCCompiler::compile_hlsl_spriv(compile_input, compile_result);

        ShaderReflection reflection;
        if (compiled)
        {
            DXCCompiler::run_spriv_reflection(compile_result.spriv_codes, compile_input.shader_type, reflection);

            vulkan_shader = create(device, shader_type, compile_result.spriv_codes, entry_point);
            vulkan_shader->get_shader_variables(reflection);
            if (shader_type == ShaderType::VertexShader)
                vulkan_shader->get_vertex_attributes(reflection);
        }

    } 

    return vulkan_shader;
}

void VulkanShader::get_shader_variables(const ShaderReflection &reflection) {

    VulkanShaderVariableBinding variable;
    variable.shader_stage = stage_;
    for (auto& shader_resource : reflection.shader_resources)
    {
        strcpy(variable.name, shader_resource.name.c_str());
        variable.binding = shader_resource.binding;
        variable.descriptor_set = shader_resource.descriptor_set;
        variable.size = shader_resource.size;
        variable.count = 1;
        
        if (shader_resource.parameter_type == ShaderReflection::ResourceType::ConstantBuffer)
        {
            variable.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::SRV)
        {
            variable.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::UAV) {
            variable.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        } else if (shader_resource.parameter_type == ShaderReflection::ResourceType::Sampler) {
            variable.type = VK_DESCRIPTOR_TYPE_SAMPLER;
        }

        variables_.push_back(variable);
    }

    for (auto& ubo : reflection.uniform_buffers)
    {
        VulkanShaderVariableBinding variable;
        strcpy(variable.name, ubo.name.c_str());
        variable.binding = ubo.binding;
        variable.descriptor_set = ubo.descriptor_set;
        variable.size = ubo.size;
        variable.count = 1;// UBO is always 1
        variable.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        variable.shader_stage = stage_;
        variable.shader_variables_ = std::move(ubo.shader_variables);
        variables_.push_back(variable);
    }

    for (auto& push_constant : reflection.push_constant_buffers)
    {
        push_constant_size_ += push_constant.size;
    }
}

void VulkanShader::get_vertex_attributes(const ShaderReflection& reflection)
{
    VertexAttribute attrib;
    for (auto shader_resource : reflection.input_layouts) {
        if (shader_resource.parameter_type == ShaderReflection::ResourceType::InputAttachment) {
            attrib.binding = shader_resource.register_;
            attrib.location = shader_resource.location;
            attrib.offset = shader_resource.offset;
            attrib.format = shader_resource.format;
            attrib.type = (uint8_t)shader_resource.vertex_attribute_type;

            vertex_attributes_.push_back(attrib);
        } 
    }
}

bool VulkanShader::HLSLToSPRIV(std::span<char> hlsl, VkShaderStageFlagBits stage, const std::string_view &entryPoint, bool outputSymbols, 
    std::vector<uint32_t> &outSpriv, std::string &errorLog) {
    ComPtr<IDxcUtils> dxc_utils = {};
    ComPtr<IDxcCompiler3> dxc_compiler = {};
    
    DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(dxc_utils.ReleaseAndGetAddressOf()));
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(dxc_compiler.ReleaseAndGetAddressOf()));

    std::vector<LPCWSTR> args;
    args.push_back(DXC_ARG_PACK_MATRIX_COLUMN_MAJOR);
    args.push_back(L"-HV");
    args.push_back(L"2021");
    args.push_back(L"-T");
    if (stage == VK_SHADER_STAGE_VERTEX_BIT)
    {
        args.push_back(L"vs_6_0");
    }
    else if (stage == VK_SHADER_STAGE_FRAGMENT_BIT)
    {
        args.push_back(L"ps_6_0");
    }
    else if (stage == VK_SHADER_STAGE_COMPUTE_BIT)
    {
        args.push_back(L"cs_6_0");
    }

    std::wstring wEntry(entryPoint.begin(), entryPoint.end());
    args.push_back(L"-E");
    args.push_back(wEntry.c_str());

    if (outputSymbols)
    {
        args.push_back(L"-Zi");
    }

    args.push_back(L"-spirv");
    args.push_back(L"-fspv-target-env=vulkan1.1");

    DxcBuffer src_buffer = {
        hlsl.data(),
        hlsl.size(),
        0};

    IDxcIncludeHandler* dxcIncludeHandler = nullptr;

    ComPtr<IDxcResult> operationResult;
    HRESULT hr = dxc_compiler->Compile(&src_buffer, args.data(), args.size(), dxcIncludeHandler, IID_PPV_ARGS(&operationResult));

    ComPtr<IDxcBlob> shader_obj;
    operationResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader_obj), nullptr);

    ComPtr<IDxcBlobUtf8> errors = nullptr;
    operationResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);

    if (errors != nullptr && errors->GetStringLength() > 0)
    {
        errorLog = errors->GetStringPointer();
        return false;
    }

    outSpriv.resize(shader_obj->GetBufferSize() / sizeof(uint32_t));

    for (size_t i = 0; i < outSpriv.size(); ++i)
    {
        uint32_t spvCode = static_cast<uint32_t *>(shader_obj->GetBufferPointer())[i];
        outSpriv[i] = spvCode;
    }

    return true;
}

VulkanShader* VulkanShaderManager::get_or_create_from_HLSL(VulkanDevice *device,
    ShaderType shader_type,
    const std::string& filename,
    const std::set<std::string> &options,
    const std::string& entry_point)
{
    ShaderKey shader_key{shader_type, filename, entry_point, options};

    auto it = vulkan_shaders_.find(shader_key);
    if (it != vulkan_shaders_.end()) {
        VulkanShaderEntry entry = get_shader_entry((handle_ty)it->second->shader_module());
    }

    VulkanShader *shader = VulkanShader::create_from_HLSL(device, shader_type, filename, entry_point);

    if (shader != nullptr)
    {
        shaders_.insert({(handle_ty)shader->shader_module(), shader });
        vulkan_shaders_.insert(std::make_pair(shader_key, shader));
        //VulkanShaderEntry entry{shader->shader_module(), shader->stage(), shader->get_entry_point()};
        vulkan_shader_entries_.insert(std::make_pair((handle_ty)shader->shader_module(), VulkanShaderEntry{shader->shader_module(), shader->stage(), shader->get_entry_point()}));
        return shader;
    }

    return nullptr;
}

VulkanShaderEntry VulkanShaderManager::get_shader_entry(handle_ty shader_handle) const
{
    auto it = vulkan_shader_entries_.find(shader_handle);
    if (it != vulkan_shader_entries_.end())
    {
        return it->second;
    }

    return {};
}

void VulkanShaderManager::clear(VulkanDevice* device)
{
    for (auto iter : vulkan_shaders_)
    {
        ocarina::delete_with_allocator(iter.second);
    }
    vulkan_shaders_.clear();
    vulkan_shader_entries_.clear();
}

}// namespace ocarina