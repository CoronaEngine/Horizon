//
// Created by Zero on 10/08/2022.
//

#include "cuda_compiler.h"
#include "core/util/switch_profile.h"
#include "cuda_device.h"
#include "ast/function.h"
#include "ast_to_cuda_source.h"
#include "ir_to_cuda_source.h"
#include "generator/ast_to_ir.h"
#include "rhi/context.h"
#include "core/util/util.h"
#include "dsl/dsl.h"

#include <io.h>

namespace ocarina {

namespace {
constexpr unsigned ast_to_cuda_source_cache_version = 1u;

[[nodiscard]] unsigned shader_codegen_path_version(ShaderCodegenPath path) noexcept {
    switch (path) {
        case ShaderCodegenPath::EAstToSource:
            return 1u;
        case ShaderCodegenPath::EAstToIR:
            return 2u;
    }
    return 0u;
}

[[nodiscard]] string emit_cuda_source(const Function &function) noexcept {
    switch_profile::Scope profile{"emit_cuda_source", "codegen"};
    switch (Env::shader_codegen_path()) {
        case ShaderCodegenPath::EAstToSource: {
            AstToCudaSource emitter{Env::code_obfuscation()};
            emitter.emit(function);
            return emitter.scratch().c_str();
        }
        case ShaderCodegenPath::EAstToIR: {
            AstToIR lowering;
            IRModule module = lowering.lower(function);
            IRToCudaSource emitter{Env::code_obfuscation()};
            emitter.emit(module);
            return emitter.scratch().c_str();
        }
    }
    return {};
}
}// namespace

std::string get_cuda_path() {
    char cudaPath[1024];
    DWORD size = GetEnvironmentVariableA("CUDA_PATH", cudaPath, sizeof(cudaPath));
    if (size > 0 && size < sizeof(cudaPath)) {
        return std::string(cudaPath);
    }

    std::string defaultPath = "C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA";
    if (_access(defaultPath.c_str(), 0) == 0) {
        return defaultPath;
    }
    return "";
}

CUDACompiler::CUDACompiler(CUDADevice *device)
    : device_(device) {}

ocarina::string CUDACompiler::compile(const Function &function, int sm) const noexcept {
    switch_profile::Scope profile{"CUDACompiler::compile", "compiler_setup"};
    // These strings own the storage referenced by NVRTC's pointer arrays.
    // Keep them alive after the options phase ends, through the compile call.
    string header_path, nvrtc_option, real_option, compute_sm, rt_option, const_option;
    std::vector<const char *> header_names;
    std::vector<string> header_sources;
    std::vector<const char *> header_sources_ptr;
    ocarina::vector<const char *> compile_option;
    ocarina::list<string> includes;
    int ptx_sm = sm;
    {
        switch_profile::Scope options_profile{"CUDACompiler::compile.headers_options", "compiler_setup"};
        fs::path cuda_path = get_cuda_path();
        cuda_path = cuda_path / "include";

        header_path = "-I" + cuda_path.string();
        int ver_major = 0;
        int ver_minor = 0;
        OC_NVRTC_CHECK(nvrtcVersion(&ver_major, &ver_minor));
        int nvrtc_version = ver_major * 10000 + ver_minor * 100;
        const auto storage_policy = function.storage_policy();
        const int use_float32 = storage_policy.policy == PrecisionPolicy::force_f16 ? 0 : 1;
        nvrtc_option = fmt::format("-DLC_NVRTC_VERSION={}", nvrtc_version);
        real_option = fmt::format("-DOCARINA_CUDA_USE_FLOAT32={}", use_float32);
        header_names = {"cuda_device_std.h", "cuda_device_scalar.h", "cuda_device_vector.h",
                        "cuda_device_matrix.h", "cuda_device_builtin.h", "cuda_tensor_builtin.h", "cuda_vector_func.h",
                        "cuda_device_math.h", "cuda_matrix_func.h", "cuda_device_resource.h"};

        for (auto fn : header_names) {
            string source = RHIContext::read_file(string("cuda/") + fn);
            header_sources_ptr.push_back(source.c_str());
            header_sources.push_back(ocarina::move(source));
        }

        // NVRTC will embed the chosen architecture into the PTX (e.g. .target sm_120).
        // OptiX's PTX frontend can lag behind the newest GPU targets; if we hand it PTX
        // with an unsupported .target, optixModuleCreate() fails with "Invalid PTX input".
        //
        // PTX is ABI-compatible by the driver for the real GPU anyway, so we can OptiX modules
        // It's safer to cap the PTX target to a widely supported SM.
        if (function.is_raytracing()) {
            // keep this conservative(for compatibility) given OptiX releases.
            ptx_sm = std::min(ptx_sm, 89);
        }
        compute_sm = ocarina::format("-arch=compute_{}", ptx_sm);
        rt_option = fmt::format("-DLC_OPTIX_VERSION={}", OPTIX_VERSION);
        const_option = fmt::format("-Dlc_constant={}", nvrtc_version <= 110200 ? "const" : "constexpr");
        compile_option = {
            "--std=c++17",
            compute_sm.c_str(),
            const_option.c_str(),
            rt_option.c_str(),
            nvrtc_option.c_str(),
            real_option.c_str(),
            "-default-device",
            "--use_fast_math",
            "-restrict",
            header_path.c_str(),
            //#ifndef NDEBUG
            "-lineinfo",
            //#endif
            "-extra-device-vectorization",
            "-dw",
            "-w"};
        if (function.is_raytracing()) {
            static string inc_path = (fs::current_path() / "cuda" / "optix").string();
            includes.push_back(ocarina::format("-I {}", inc_path));
            compile_option.push_back(includes.back().c_str());
            header_names.push_back("optix_device_header.h");
            string source = RHIContext::read_file(string("cuda/optix_device_header.h"));
            header_sources_ptr.push_back(source.c_str());
            header_sources.push_back(ocarina::move(source));
        }
        for (const auto &header_name : header_names) {
            includes.push_back(ocarina::format("-include={}", header_name));
            compile_option.push_back(includes.back().c_str());
        }
    }
    string fn;
    {
        switch_profile::Scope hash_profile{"CUDACompiler::compile.cache_key", "cache_key"};
        uint64_t ext_hash = hash64(hash64_list(compile_option),
                                  hash64_list(header_sources_ptr),
                                  ast_to_cuda_source_cache_version,
                                  shader_codegen_path_version(Env::shader_codegen_path()));
        fn = function.func_name(ext_hash, function.description());
    }

    auto compile = [&](const string &cu, const string &fn, int sm) -> string {
        OC_INFO_FORMAT("compiling shader with NVRTC: {}", fn);
        TIMER_TAG(compile, "NVRTC compile " + fn);
        switch_profile::Scope nvrtc_profile{"CUDACompiler::compile.nvrtc", "nvrtc_setup"};
        nvrtcProgram program{};
        OC_NVRTC_CHECK(nvrtcCreateProgram(&program, cu.c_str(), fn.c_str(),
                                          header_names.size(), header_sources_ptr.data(),
                                          header_names.data()));
        nvrtcResult compile_res;
        {
            switch_profile::Scope compile_profile{"nvrtcCompileProgram", "nvrtc"};
            compile_res = nvrtcCompileProgram(program, compile_option.size(), compile_option.data());
        }
        size_t log_size = 0;
        OC_NVRTC_CHECK(nvrtcGetProgramLogSize(program, &log_size));
        string log;
        log.resize(log_size);
        if (log_size > 1) {
            OC_NVRTC_CHECK(nvrtcGetProgramLog(program, log.data()));
        }
        if (compile_res != NVRTC_SUCCESS) {
            cout << log << endl;
            std::abort();
        }
        size_t ptx_size = 0;
        ocarina::string ptx;
        OC_NVRTC_CHECK(nvrtcGetPTXSize(program, &ptx_size));
        ptx.resize(ptx_size);
        OC_NVRTC_CHECK(nvrtcGetPTX(program, ptx.data()));
        OC_NVRTC_CHECK(nvrtcDestroyProgram(&program));
        return ptx;
    };

    ocarina::string ptx_fn = fn + ".ptx";
    string cu_fn = fn + ".cu";
    ocarina::string ptx;
    RHIContext *context = device_->context();
    if (!context->is_exist_cache(ptx_fn)) {
        OC_INFO_FORMAT("shader PTX cache miss: {}", ptx_fn);
        if (!context->is_exist_cache(cu_fn)) {
            const ocarina::string cu = emit_cuda_source(function);
            context->write_global_cache(cu_fn, cu);
            ptx = compile(cu, cu_fn, ptx_sm);
            context->write_global_cache(ptx_fn, ptx);
        } else {
            const ocarina::string &cu = context->read_global_cache(cu_fn);
            ptx = compile(cu, cu_fn, sm);
            context->write_global_cache(ptx_fn, ptx);
        }
    } else {
        OC_INFO_FORMAT("shader PTX cache hit: {}", ptx_fn);
        ptx = context->read_global_cache(ptx_fn);
    }
    return ptx;
}

}// namespace ocarina
