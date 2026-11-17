//
//

#pragma once

#include "core/stl.h"
#include "ast/function.h"
#include <mutex>

// Forward declarations only — avoids pulling slang.h into every TU.
namespace slang { struct IGlobalSession; struct ISession; }

namespace ocarina {

class RHIContext;

// Describes the compilation target, not the machine running the compiler.
// Runtime callers must populate this from the device that will execute SPIR-V.
struct SlangCompileTarget {
    bool warp32{false};
    bool ray_query{false};
    bool preserve_float32{false};
    bool preserve_float16{false};
    uint32_t rte_widths{0u};
    uint32_t rounding_independence{0u};
};

class SlangShaderCompiler {
private:
    RHIContext *context_;
    const SlangCompileTarget target_;
    mutable std::mutex compile_mutex_;
    // global_session_ owns the Slang context; session_ is a child session scoped
    // to a single target profile.  Both are ref-counted COM-like objects; we hold
    // raw pointers and manage AddRef/Release explicitly in the .cpp.
    mutable slang::IGlobalSession *global_session_{nullptr};
    mutable slang::ISession       *session_{nullptr};

    void ensure_session() const noexcept;

public:
    SlangShaderCompiler(RHIContext *context, SlangCompileTarget target) noexcept;
    ~SlangShaderCompiler() noexcept;

    // Compile an EDSL Function to SPIR-V.  workgroup_size is the desired
    // numthreads for all kernels, including those using inline ray queries.
    // Returns empty vector on failure (error already logged).
    [[nodiscard]] vector<uint32_t> compile(const Function &function,
                                           uint3 workgroup_size) const noexcept;
};

}// namespace ocarina
