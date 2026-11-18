#pragma once

#include "core/stl.h"
#include "rhi/shader_compile_options.h"
#include <vulkan/vulkan.h>

namespace ocarina {

struct VkShaderModuleCode {
    VkShaderStageFlagBits stage;
    string entry;
    vector<uint32_t> spirv;
};

struct VkShaderProgram {
    ShaderExecutionMode execution_mode;
    vector<VkShaderModuleCode> modules;
};

}// namespace ocarina
