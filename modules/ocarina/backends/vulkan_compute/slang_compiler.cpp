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

constexpr unsigned slang_source_cache_version = 4u;

void normalize_ray_query_extensions(vector<uint32_t> &spirv) noexcept {
    // Slang also declares SPV_KHR_ray_tracing when constructing an acceleration
    // structure from its address. SPV_KHR_ray_query provides that instruction
    // itself; a query-only module must not require VK_KHR_ray_tracing_pipeline.
    constexpr uint32_t op_extension = 10u;
    constexpr uint32_t op_capability = 17u;
    constexpr uint32_t ray_query_khr = 4472u;
    constexpr uint32_t ray_tracing_khr = 4479u;
    bool has_query = false, has_pipeline = false;
    for (size_t offset = 5; offset < spirv.size();) {
        const uint32_t count = spirv[offset] >> 16u;
        const uint32_t op = spirv[offset] & 0xffffu;
        OC_ERROR_IF(count == 0 || count > spirv.size() - offset, "Malformed Slang SPIR-V instruction");
        if (op == op_capability && count == 2) {
            has_query |= spirv[offset + 1] == ray_query_khr;
            has_pipeline |= spirv[offset + 1] == ray_tracing_khr;
        }
        offset += count;
    }
    if (!has_query || has_pipeline) return;
    for (size_t offset = 5; offset < spirv.size();) {
        const uint32_t count = spirv[offset] >> 16u;
        const uint32_t op = spirv[offset] & 0xffffu;
        if (op == op_extension && count > 1) {
            string_view name(reinterpret_cast<const char *>(spirv.data() + offset + 1), (count - 1u) * 4u);
            if (name.substr(0, name.find('\0')) == "SPV_KHR_ray_tracing") {
                spirv.erase(spirv.begin() + offset, spirv.begin() + offset + count);
                continue;
            }
        }
        offset += count;
    }
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
    string builtin_dir = (device_->context()->runtime_directory() / "slang").string();
    const char *search_paths[] = {builtin_dir.c_str()};

    slang::SessionDesc session_desc{};
    session_desc.targets                 = &target;
    session_desc.targetCount             = 1;
    session_desc.searchPaths             = search_paths;
    session_desc.searchPathCount         = 1;
    session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;

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
    std::lock_guard lock(compile_mutex_);
    ensure_session();

    string source = emit_slang_source(function);
    if (source.empty()) {
        OC_ERROR("Slang: empty source for function '{}'", function.func_name());
        return {};
    }

    // Include builtins and compiler version, so replacing a runtime DLL or a
    // device header cannot silently reuse SPIR-V from a different compilation.
    auto runtime_dir = device_->context()->runtime_directory();
    auto source_hash = hash64(source,
        RHIContext::read_file(runtime_dir / "slang/slang_device_math.slang"),
        RHIContext::read_file(runtime_dir / "slang/slang_device_resource.slang"),
        string_view(global_session_->getBuildTagString()), slang_source_cache_version);
    const string cache_key = ocarina::format("{}_{}_{}_{}_{}", function.func_name(),
        source_hash, workgroup_size.x, workgroup_size.y, workgroup_size.z);
    const string cache_path = ocarina::format("{}/slang_cache/{}.spv",
                                               device_->context()->cache_directory().string(), cache_key);

    // Try loading from disk cache
    if (std::filesystem::exists(cache_path)) {
        std::ifstream f(cache_path, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            size_t byte_size = static_cast<size_t>(f.tellg());
            if (byte_size >= 20 && byte_size % 4 == 0) {
                f.seekg(0);
                vector<uint32_t> spirv(byte_size / 4);
                f.read(reinterpret_cast<char *>(spirv.data()), (std::streamsize)byte_size);
                if (f.good() && spirv[0] == 0x07230203u) return spirv;
            }
        }
    }

    // Patch numthreads attribute for compute kernels
    if (function.is_kernel()) {
        string nt = ocarina::format("[numthreads({}, {}, {})]",
                                    workgroup_size.x, workgroup_size.y, workgroup_size.z);
        auto pos = source.find("[numthreads(1, 1, 1)]");
        if (pos != string::npos)
            source.replace(pos, std::strlen("[numthreads(1, 1, 1)]"), nt);
    }

    // Add builtin includes
    const string preamble = ocarina::format(
        "#define OC_WORKGROUP_X {}\n#define OC_WORKGROUP_Y {}\n#define OC_WORKGROUP_Z {}\n",
        workgroup_size.x, workgroup_size.y, workgroup_size.z) +
        "#include \"slang_device_resource.slang\"\n#include \"slang_device_math.slang\"\n";
    source = preamble + source;
    device_->context()->write_global_cache(function.func_name() + ".slang", source);

    // Compile via Slang ISession
    Slang::ComPtr<slang::IBlob> diag;
    Slang::ComPtr<slang::IModule> module;
    {
        module = session_->loadModuleFromSourceString(
            cache_key.c_str(), nullptr, source.c_str(), diag.writeRef());
        if (diag && diag->getBufferSize() > 0) {
            OC_WARNING("Slang diagnostics:\n{}",
                       reinterpret_cast<const char *>(diag->getBufferPointer()));
        }
        if (!module) {
            OC_ERROR("Slang: compile failed for '{}'", function.func_name());
            return {};
        }
    }

    // Find the entry point (always named "main" in our codegen)
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    {
        SlangResult r = module->findEntryPointByName("main", entry_point.writeRef());
        if (SLANG_FAILED(r)) {
            OC_ERROR("Slang: no entry point 'main' in '{}'", function.func_name());
            return {};
        }
    }

    // Link and extract SPIR-V
    slang::IComponentType *components[] = {module.get(), entry_point.get()};
    Slang::ComPtr<slang::IComponentType> linked;
    {
        SlangResult r = session_->createCompositeComponentType(
            components, 2, linked.writeRef(), diag.writeRef());
        if (SLANG_FAILED(r)) {
            OC_ERROR("Slang: link failed for '{}'", function.func_name());
            return {};
        }
    }

    Slang::ComPtr<slang::IBlob> spirv_blob;
    {
        SlangResult r = linked->getEntryPointCode(0, 0, spirv_blob.writeRef(), diag.writeRef());
        if (SLANG_FAILED(r)) {
            OC_ERROR("Slang: SPIR-V extraction failed for '{}'", function.func_name());
            return {};
        }
    }

    size_t byte_size = spirv_blob->getBufferSize();
    const uint32_t *data = reinterpret_cast<const uint32_t *>(spirv_blob->getBufferPointer());
    vector<uint32_t> spirv(data, data + byte_size / 4);
    normalize_ray_query_extensions(spirv);

    // Write to disk cache
    std::filesystem::create_directories(
        std::filesystem::path(cache_path).parent_path());
    std::ofstream out(cache_path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(spirv.data()),
              (std::streamsize)(spirv.size() * 4));

    return spirv;
}

}// namespace ocarina
