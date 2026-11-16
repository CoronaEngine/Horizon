//
//

#pragma once

#include "core/stl.h"
#include "ast/function.h"
#include <mutex>

// Forward declarations only — avoids pulling slang.h into every TU.
namespace slang { struct IGlobalSession; struct ISession; }

namespace ocarina {

class VulkanComputeDevice;

class SlangShaderCompiler {
private:
    VulkanComputeDevice     *device_;
    mutable std::mutex compile_mutex_;
    // global_session_ owns the Slang context; session_ is a child session scoped
    // to a single target profile.  Both are ref-counted COM-like objects; we hold
    // raw pointers and manage AddRef/Release explicitly in the .cpp.
    mutable slang::IGlobalSession *global_session_{nullptr};
    mutable slang::ISession       *session_{nullptr};
    mutable bool preserve_float32_{false};
    mutable bool preserve_float16_{false};
    mutable uint32_t rte_widths_{0u};
    mutable uint32_t rounding_independence_{0u};

    void ensure_session() const noexcept;

public:
    explicit SlangShaderCompiler(VulkanComputeDevice *device) noexcept;
    ~SlangShaderCompiler() noexcept;

    // Compile an EDSL Function to SPIR-V.  workgroup_size is the desired
    // numthreads for all kernels, including those using inline ray queries.
    // Returns empty vector on failure (error already logged).
    [[nodiscard]] vector<uint32_t> compile(const Function &function,
                                           uint3 workgroup_size) const noexcept;
};

}// namespace ocarina
