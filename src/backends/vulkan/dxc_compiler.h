//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "rhi/resources/shader.h"
#include <vulkan/vulkan.h>
#include "rhi/graphics_descriptions.h"
#include "spirv_cross.hpp"

namespace ocarina {

    class VulkanDevice;
struct ShaderReflection;

    struct CompileInput
    {
        std::string hlsl;
        std::string entry;
        std::string full_file_path;
        std::vector<std::string> macros;
        std::vector<std::string> include_paths;
        ShaderType shader_type;
        bool output_pdbs;

    };

    struct CompileResult
    {
        std::vector<uint32_t> spriv_codes;
        std::string error;
    };

class DXCCompiler : public concepts::Noncopyable {
public:
    static bool preprocess(const char *hlsl, uint32_t size, const std::string& full_file_path, VkShaderStageFlagBits stage, const std::vector<std::string> &include_paths, std::string &plattern_hlsl);
    static bool compile_hlsl_spriv(const CompileInput& input, CompileResult& result);

    static void run_spriv_reflection(const std::vector<uint32_t> &spriv, ShaderType shader_type, ShaderReflection& shader_reflection);
    DXCCompiler() = default;
    DXCCompiler(const DXCCompiler &) = delete;
    DXCCompiler(DXCCompiler &&) = delete;
    DXCCompiler operator=(const DXCCompiler &) = delete;
    DXCCompiler operator=(DXCCompiler &&) = delete;
    ~DXCCompiler() {}

private:
    static ShaderVariableType get_shader_variable_type(uint32_t vec_size, uint32_t column_size, const spirv_cross::SPIRType& type);
};
}// namespace ocarina