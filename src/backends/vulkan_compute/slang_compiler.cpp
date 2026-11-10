//
//

#include "slang_compiler.h"
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include "core/util/switch_profile.h"
#include "vk_compute_device.h"
#include "ast_to_slang_source.h"
#include "rhi/context.h"
#include "core/util/util.h"
#include "dsl/dsl.h"

// Slang public API
#include <slang.h>
#include <slang-com-ptr.h>

namespace ocarina {

namespace {

constexpr unsigned slang_source_cache_version = 9u;

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

#ifdef __APPLE__
// MoltenVK translates SPIR-V through SPIRV-Cross, which folds an OpLoad from a
// PhysicalStorageBuffer pointer into the expression at its use site. That
// re-evaluates the load after any intervening store to an aliasing address and
// breaks shaders that read and later write the same buffer (for example
// `points[i].xz() = points[i].zx()` or a read/barrier/write sequence). SPIR-V
// SSA semantics require the load to happen at its original program point, so
// mark only the loads whose value is observed after a later physical store;
// other loads keep full MSL optimization freedom.
void mark_physical_storage_loads_volatile(vector<uint32_t> &spirv) noexcept {
    constexpr uint32_t op_type_pointer = 32u;
    constexpr uint32_t op_function_parameter = 55u;
    constexpr uint32_t op_function_call = 57u;
    constexpr uint32_t op_variable = 59u;
    constexpr uint32_t op_load = 61u;
    constexpr uint32_t op_store = 62u;
    constexpr uint32_t op_access_chain = 65u;
    constexpr uint32_t op_in_bounds_access_chain = 66u;
    constexpr uint32_t op_ptr_access_chain = 67u;
    constexpr uint32_t op_in_bounds_ptr_access_chain = 70u;
    constexpr uint32_t op_copy_object = 83u;
    constexpr uint32_t op_convert_u_to_ptr = 120u;
    constexpr uint32_t op_bitcast = 124u;
    constexpr uint32_t op_select = 169u;
    constexpr uint32_t op_phi = 245u;
    constexpr uint32_t storage_class_physical_storage_buffer = 5349u;
    constexpr uint32_t memory_access_volatile = 0x1u;

    if (spirv.size() < 5u || spirv[0] != 0x07230203u) return;

    std::unordered_set<uint32_t> physical_pointer_types;
    std::unordered_map<uint32_t, uint32_t> id_types;
    for (size_t offset = 5u; offset < spirv.size();) {
        const uint32_t count = spirv[offset] >> 16u;
        const uint32_t op = spirv[offset] & 0xffffu;
        if (count == 0u || count > spirv.size() - offset) return;
        if (op == op_type_pointer && count >= 4u &&
            spirv[offset + 2u] == storage_class_physical_storage_buffer)
            physical_pointer_types.insert(spirv[offset + 1u]);
        switch (op) {
            case op_function_parameter:
            case op_function_call:
            case op_variable:
            case op_load:
            case op_access_chain:
            case op_in_bounds_access_chain:
            case op_ptr_access_chain:
            case op_in_bounds_ptr_access_chain:
            case op_copy_object:
            case op_convert_u_to_ptr:
            case op_bitcast:
            case op_select:
            case op_phi:
                if (count >= 3u) id_types[spirv[offset + 2u]] = spirv[offset + 1u];
                break;
            default:
                break;
        }
        offset += count;
    }

    if (physical_pointer_types.empty()) return;

    struct PhysicalLoad {
        size_t offset;
        size_t last_use;
    };
    vector<PhysicalLoad> loads;
    vector<size_t> stores;
    std::unordered_map<uint32_t, size_t> load_by_result;
    for (size_t offset = 5u; offset < spirv.size();) {
        const uint32_t count = spirv[offset] >> 16u;
        const uint32_t op = spirv[offset] & 0xffffu;
        if (count == 0u || count > spirv.size() - offset) return;
        if (op == op_load && count >= 5u) {
            const auto pointer_type = id_types.find(spirv[offset + 3u]);
            if (pointer_type != id_types.end() &&
                physical_pointer_types.count(pointer_type->second) != 0u) {
                load_by_result[spirv[offset + 2u]] = loads.size();
                loads.push_back({offset, offset});
            }
        } else if (op == op_store && count >= 3u) {
            const auto pointer_type = id_types.find(spirv[offset + 1u]);
            if (pointer_type != id_types.end() &&
                physical_pointer_types.count(pointer_type->second) != 0u)
                stores.push_back(offset);
        }
        offset += count;
    }
    if (loads.empty() || stores.empty()) return;

    // Record the last instruction that consumes each physical load's value.
    for (size_t offset = 5u; offset < spirv.size();) {
        const uint32_t count = spirv[offset] >> 16u;
        const uint32_t op = spirv[offset] & 0xffffu;
        if (count == 0u || count > spirv.size() - offset) return;
        for (uint32_t word = 1u; word < count; ++word) {
            const auto load = load_by_result.find(spirv[offset + word]);
            if (load != load_by_result.end() && offset > loads[load->second].last_use)
                loads[load->second].last_use = offset;
        }
        offset += count;
    }

    for (const PhysicalLoad &load : loads) {
        const bool reordered = std::any_of(stores.begin(), stores.end(), [&](size_t store) {
            return store > load.offset && store < load.last_use;
        });
        if (reordered) spirv[load.offset + 4u] |= memory_access_volatile;
    }
}
#endif

[[nodiscard]] string emit_slang_source(const Function &function) noexcept {
    AstToSlangSource emitter{Env::code_obfuscation()};
    emitter.emit(function);
    return emitter.scratch().c_str();
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

    // Slang's texture intrinsics carry these standard-library requirements even
    // when they only emit ordinary image operations. Declaring them avoids an
    // implicit profile upgrade; SPIR-V still declares only capabilities in use.
    // The backend requires ballot subgroups and enables ray queries separately.
    const char *capabilities[] = {
        "SPV_KHR_non_semantic_info", "SPV_GOOGLE_user_type",
        "spvDerivativeControl", "spvImageQuery", "spvImageGatherExtended",
        "spvSparseResidency", "spvMinLod", "spvFragmentFullyCoveredEXT",
        "spvGroupNonUniform", "spvGroupNonUniformBallot",
        "spvRayQueryKHR"};
    slang::CompilerOptionEntry target_options[std::size(capabilities)]{};
    const uint32_t capability_count = uint32_t(std::size(capabilities)) - (device_->has_rt() ? 0u : 1u);
    for (uint32_t i = 0; i < capability_count; ++i) {
        auto capability = global_com->findCapability(capabilities[i]);
        OC_ERROR_IF(capability == SLANG_CAPABILITY_UNKNOWN,
                    "Slang: required target capability not found: ", capabilities[i]);
        target_options[i].name = slang::CompilerOptionName::Capability;
        target_options[i].value.intValue0 = capability;
    }
    target.compilerOptionEntries = target_options;
    target.compilerOptionEntryCount = capability_count;

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
    switch_profile::Scope profile{"SlangShaderCompiler::compile", "slang"};
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
                if (f.good() && spirv[0] == 0x07230203u) {
                    OC_INFO_FORMAT("shader SPIR-V cache hit: {}.spv", cache_key);
                    return spirv;
                }
            }
        }
    }

    OC_INFO_FORMAT("shader SPIR-V cache miss: {}.spv", cache_key);
    const auto compile_start = std::chrono::steady_clock::now();

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
            OC_WARNING_FORMAT("Slang diagnostics:\n{}",
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
#ifdef __APPLE__
    mark_physical_storage_loads_volatile(spirv);
#endif
    const double compile_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - compile_start).count();
    OC_INFO_FORMAT("task Slang compile {}.slang is take {} ms", cache_key, compile_ms);

    // Write to disk cache
    std::filesystem::create_directories(
        std::filesystem::path(cache_path).parent_path());
    std::ofstream out(cache_path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(spirv.data()),
              (std::streamsize)(spirv.size() * 4));

    return spirv;
}

}// namespace ocarina
