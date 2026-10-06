//
// Created during CUDA→Slang+Vulkan migration.
// Replaces CUDACompiler: EDSL Function → SPIR-V bytecode via the Slang API.
//

#pragma once

#include "core/stl.h"
#include "ast/function.h"

// Forward declarations: avoid pulling full slang.h into every TU.
struct SlangSession;
struct SlangCompileRequest;

namespace ocarina {

class VulkanComputeDevice;

class SlangShaderCompiler {
private:
    VulkanComputeDevice *device_;
    mutable SlangSession *session_{nullptr};

    void ensure_session() const noexcept;

public:
    explicit SlangShaderCompiler(VulkanComputeDevice *device) noexcept;
    ~SlangShaderCompiler() noexcept;

    // Compile an EDSL Function to SPIR-V.  workgroup_size is the desired
    // numthreads for compute kernels; ignored for raytracing kernels.
    // Returns empty vector on failure (error already logged).
    [[nodiscard]] vector<uint32_t> compile(const Function &function,
                                           uint3 workgroup_size) const noexcept;
};

}// namespace ocarina
