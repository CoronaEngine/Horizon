//
// Created during CUDA→Slang+Vulkan migration.
// Replaces cuda_compiler.cpp — Slang session + SPIR-V cache.
//

#include "slang_compiler.h"
#include "vk_compute_device.h"
#include "ast_to_slang_source.h"
#include "ir_to_slang_source.h"
#include "generator/ast_to_ir.h"
#include "rhi/context.h"
#include "core/util/util.h"
#include "dsl/dsl.h"

// Slang public API
#include <slang.h>
#include <slang-com-ptr.h>

namespace ocarina {

namespace {

constexpr unsigned slang_source_cache_version = 1u;

[[nodiscard]] unsigned shader_codegen_path_version(ShaderCodegenPath path) noexcept {
    switch (path) {
        case ShaderCodegenPath::EAstToSource: return 1u;
        case ShaderCodegenPath::EAstToIR:    return 2u;
    }
    return 0u;
}

[[nodiscard]] string emit_slang_source(const Function &function) noexcept {
    switch (Env::shader_codegen_path()) {
        case ShaderCodegenPath::EAstToSource: {
            AstToSlangSource emitter{Env::code_obfuscation()};
            emitter.emit(function);
            return emitter.scratch().c_str();
        }
        case ShaderCodegenPath::EAstToIR: {
            AstToIR lowering;
            IRModule module = lowering.lower(function);
            IRToSlangSource emitter{Env::code_obfuscation()};
            emitter.emit(module);
            return emitter.scratch().c_str();
        }
    }
    return {};
}

}// namespace

// ── SlangShaderCompiler ───────────────────────────────────────────────────────

SlangShaderCompiler::SlangShaderCompiler(VulkanComputeDevice *device) noexcept
    : device_(device) {}

SlangShaderCompiler::~SlangShaderCompiler() noexcept {
    if (session_) {
        session_->release();
        session_ = nullptr;
    }
    if (global_session_) {
        global_session_->release();
        global_session_ = nullptr;
    }
}

// ── ensure_session ────────────────────────────────────────────────────────────

void SlangShaderCompiler::ensure_session() const noexcept {
    if (session_) return;

    // Create a global session using the modern C++ API.
    Slang::ComPtr<slang::IGlobalSession> global_com;
    SlangResult r = slang::createGlobalSession(global_com.writeRef());
    OC_ERROR_IF(SLANG_FAILED(r) || !global_com, "Slang: failed to create global session");

    // Target: SPIR-V 1.5 for Vulkan 1.2
    slang::TargetDesc target{};
    target.format  = SLANG_SPIRV;
    target.profile = global_com->findProfile("spirv_1_5");

    // Search paths for builtin .slang files copied next to the backend DLL.
    string builtin_dir = Env::builtin_path() + "/slang";
    const char *search_paths[] = {builtin_dir.c_str()};

    slang::SessionDesc session_desc{};
    session_desc.targets                 = &target;
    session_desc.targetCount             = 1;
    session_desc.searchPaths             = search_paths;
    session_desc.searchPathCount         = 1;
    session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    Slang::ComPtr<slang::ISession> session_com;
    r = global_com->createSession(session_desc, session_com.writeRef());
    OC_ERROR_IF(SLANG_FAILED(r) || !session_com, "Slang: createSession failed");

    // Keep both objects alive for the compiler's lifetime.
    global_com->addRef();
    global_session_ = global_com.get();

    session_com->addRef();
    session_ = session_com.get();
}

// ── compile ───────────────────────────────────────────────────────────────────

vector<uint32_t> SlangShaderCompiler::compile(const Function &function,
                                               uint3 workgroup_size) const noexcept {
    ensure_session();

    string source = emit_slang_source(function);
    if (source.empty()) {
        OC_ERROR("Slang: empty source for function '{}'", function.name());
        return {};
    }

    // Cache key: hash of source + workgroup size
    const string cache_key = ocarina::format("{}_{}_{}_{}_{}", function.name(),
                                              source.size(), workgroup_size.x,
                                              workgroup_size.y, workgroup_size.z);
    const string cache_path = ocarina::format("{}/slang_cache/{}.spv",
                                               Env::cache_path(), cache_key);

    // Try loading from disk cache
    if (std::filesystem::exists(cache_path)) {
        std::ifstream f(cache_path, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            size_t byte_size = static_cast<size_t>(f.tellg());
            if (byte_size % 4 == 0) {
                f.seekg(0);
                vector<uint32_t> spirv(byte_size / 4);
                f.read(reinterpret_cast<char *>(spirv.data()), (std::streamsize)byte_size);
                if (f.good()) return spirv;
            }
        }
    }

    // Patch numthreads attribute for compute kernels
    if (function.is_general_kernel()) {
        string nt = ocarina::format("[numthreads({}, {}, {})]",
                                    workgroup_size.x, workgroup_size.y, workgroup_size.z);
        auto pos = source.find("[numthreads(1, 1, 1)]");
        if (pos != string::npos)
            source.replace(pos, std::strlen("[numthreads(1, 1, 1)]"), nt);
    }

    // Add builtin includes
    const string preamble = "#include \"slang_device_resource.slang\"\n"
                            "#include \"slang_device_math.slang\"\n";
    source = preamble + source;

    // Compile via Slang ISession
    Slang::ComPtr<slang::IBlob> diag;
    Slang::ComPtr<slang::IModule> module;
    {
        SlangResult r = session_->loadModuleFromSourceString(
            function.name().c_str(), nullptr,
            source.c_str(), module.writeRef(), diag.writeRef());
        if (diag && diag->getBufferSize() > 0) {
            OC_WARNING("Slang diagnostics:\n{}",
                       reinterpret_cast<const char *>(diag->getBufferPointer()));
        }
        if (SLANG_FAILED(r)) {
            OC_ERROR("Slang: compile failed for '{}'", function.name());
            return {};
        }
    }

    // Find the entry point (always named "main" in our codegen)
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    {
        SlangResult r = module->findEntryPointByName("main", entry_point.writeRef());
        if (SLANG_FAILED(r)) {
            OC_ERROR("Slang: no entry point 'main' in '{}'", function.name());
            return {};
        }
    }

    // Link and extract SPIR-V
    slang::IComponentType *components[] = {module.get(), entry_point.get()};
    Slang::ComPtr<slang::IComponentType> linked;
    {
        SlangResult r = isession->createCompositeComponentType(
            components, 2, linked.writeRef(), diag.writeRef());
        if (SLANG_FAILED(r)) {
            OC_ERROR("Slang: link failed for '{}'", function.name());
            return {};
        }
    }

    Slang::ComPtr<slang::IBlob> spirv_blob;
    {
        SlangResult r = linked->getEntryPointCode(0, 0, spirv_blob.writeRef(), diag.writeRef());
        if (SLANG_FAILED(r)) {
            OC_ERROR("Slang: SPIR-V extraction failed for '{}'", function.name());
            return {};
        }
    }

    size_t byte_size = spirv_blob->getBufferSize();
    const uint32_t *data = reinterpret_cast<const uint32_t *>(spirv_blob->getBufferPointer());
    vector<uint32_t> spirv(data, data + byte_size / 4);

    // Write to disk cache
    std::filesystem::create_directories(
        std::filesystem::path(cache_path).parent_path());
    std::ofstream out(cache_path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(spirv.data()),
              (std::streamsize)(spirv.size() * 4));

    return spirv;
}

}// namespace ocarina
