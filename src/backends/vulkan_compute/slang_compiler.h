#pragma once

#include "ast/function.h"
#include "vk_shader_program.h"
#include <array>
#include <mutex>

namespace slang { struct IGlobalSession; struct ISession; }

namespace ocarina {

class VulkanComputeDevice;

struct ShaderCompilationContext {
    ShaderCompileOptions options{};
    ShaderExecutionMode execution_mode{ShaderExecutionMode::Compute};
    string description;
};

class SlangShaderCompiler {
private:
    VulkanComputeDevice *device_;
    mutable std::mutex compile_mutex_;
    mutable slang::IGlobalSession *global_session_{};
    mutable std::array<slang::ISession *, 3> sessions_{};
    mutable bool preserve_float32_{};
    mutable bool preserve_float16_{};
    mutable uint32_t rte_widths_{};
    mutable uint32_t rounding_independence_{};

    slang::ISession *session_for(ShaderExecutionMode mode) const noexcept;
    slang::ISession *create_session(ShaderExecutionMode mode) const noexcept;

public:
    explicit SlangShaderCompiler(VulkanComputeDevice *device) noexcept;
    ~SlangShaderCompiler() noexcept;

    [[nodiscard]] VkShaderProgram compile_program(const Function &function, uint3 workgroup_size,
                                                  const ShaderCompilationContext &context) const noexcept;
};

}// namespace ocarina
