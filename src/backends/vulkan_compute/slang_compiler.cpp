//
//

#include "slang_compiler.h"
#include <chrono>
#include <atomic>
#include <random>
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

constexpr unsigned slang_source_cache_version = 13u;

constexpr uint32_t float_width_mask(uint32_t width) noexcept {
    return width == 16u ? 1u : width == 32u ? 2u : width == 64u ? 4u : 0u;
}

void set_float_control_mode(vector<uint32_t> &spirv, uint32_t capability,
                            uint32_t mode, uint32_t widths) noexcept {
    // Capabilities precede entry points; execution modes precede debug/types.
    constexpr uint32_t op_capability = 17u, op_entry_point = 15u;
    constexpr uint32_t op_execution_mode = 16u, op_execution_mode_id = 331u;
    vector<uint32_t> entry_points;
    vector<uint64_t> preserved_entries;
    size_t capability_end = 5u, execution_modes_end = 5u;
    bool has_capability = false;
    for (size_t offset = 5u; offset < spirv.size();) {
        const uint32_t count = spirv[offset] >> 16u, op = spirv[offset] & 0xffffu;
        OC_ERROR_IF(count == 0u || count > spirv.size() - offset, "Malformed Slang SPIR-V instruction");
        if (op == op_capability) {
            capability_end = offset + count;
            has_capability |= count == 2u && spirv[offset + 1u] == capability;
        } else if (op == op_entry_point) {
            OC_ERROR_IF(count < 4u, "Malformed Slang SPIR-V entry point");
            entry_points.push_back(spirv[offset + 2u]);
            execution_modes_end = offset + count;
        } else if (op == op_execution_mode || op == op_execution_mode_id) {
            execution_modes_end = offset + count;
            if (op == op_execution_mode && count == 4u) {
                uint32_t existing_mode = spirv[offset + 2u], width = spirv[offset + 3u];
                if (existing_mode == mode)
                    preserved_entries.push_back((uint64_t(spirv[offset + 1u]) << 32u) | width);
                OC_ERROR_IF(mode == 4462u && existing_mode == 4463u && (widths & float_width_mask(width)),
                            "Fused operations require RTE but the shader requests RTZ");
            }
        }
        offset += count;
    }
    vector<uint32_t> modes;
    for (uint32_t entry : entry_points) for (uint32_t width : {16u, 32u, 64u}) {
        uint64_t key = (uint64_t(entry) << 32u) | width;
        if ((widths & float_width_mask(width)) &&
            std::find(preserved_entries.begin(), preserved_entries.end(), key) == preserved_entries.end())
            modes.insert(modes.end(), {(4u << 16u) | op_execution_mode, entry, mode, width});
    }
    if (modes.empty()) return;
    spirv.insert(spirv.begin() + execution_modes_end, modes.begin(), modes.end());
    if (!has_capability)
        spirv.insert(spirv.begin() + capability_end, {(2u << 16u) | op_capability, capability});
}

void require_half_fused_rounding(vector<uint32_t> &spirv, uint32_t supported_widths,
                                 uint32_t independence, bool preserve_float16) noexcept {
    constexpr uint32_t op_type_float = 22u;
    uint32_t used_widths = 0u;
    for (size_t offset = 5u; offset < spirv.size();) {
        uint32_t count = spirv[offset] >> 16u, op = spirv[offset] & 0xffffu;
        OC_ERROR_IF(count == 0u || count > spirv.size() - offset, "Malformed Slang SPIR-V instruction");
        if (op == op_type_float && count >= 3u) used_widths |= float_width_mask(spirv[offset + 2u]);
        offset += count;
    }
    // Half TwoSum requires round-to-nearest-even in float32.
    const uint32_t required_widths = independence == VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE ?
                                         used_widths : float_width_mask(32u);
    OC_ERROR_IF((required_widths & supported_widths) != required_widths,
                "Half fused operations require RTE for every dependent floating-point width used by the shader");
    set_float_control_mode(spirv, 4467u, 4462u, required_widths);
    // Half adapters transfer their input/output bits through real float16 values.
    // RTE alone does not preserve the sign of zero at those typed boundaries.
    if (used_widths & 1u) {
        OC_ERROR_IF(!preserve_float16, "Half fused operations require float16 signed-zero/NaN/Inf preservation");
        set_float_control_mode(spirv, 4466u, 4461u, 1u);
    }
}

void decorate_glsl_fma(vector<uint32_t> &spirv) noexcept {
    // Preserve NoContraction for calls that still lower to GLSL.std.450 Fma.
    constexpr uint32_t op_ext_inst_import = 11u, op_ext_inst = 12u, op_decorate = 71u;
    constexpr uint32_t glsl_fma = 50u, no_contraction = 42u;
    uint32_t glsl_set = 0u;
    size_t annotation_end = 0u;
    vector<uint32_t> fused_results, decorated_results;
    for (size_t offset = 5u; offset < spirv.size();) {
        const uint32_t count = spirv[offset] >> 16u, op = spirv[offset] & 0xffffu;
        OC_ERROR_IF(count == 0u || count > spirv.size() - offset, "Malformed Slang SPIR-V instruction");
        if (op == op_ext_inst_import && count >= 3u) {
            string_view name(reinterpret_cast<const char *>(spirv.data() + offset + 2u), (count - 2u) * 4u);
            if (name.substr(0u, name.find('\0')) == "GLSL.std.450") glsl_set = spirv[offset + 1u];
        } else if (op == op_decorate && count == 3u && spirv[offset + 2u] == no_contraction)
            decorated_results.push_back(spirv[offset + 1u]);
        else if (op == op_ext_inst && count >= 5u && glsl_set != 0u &&
                 spirv[offset + 3u] == glsl_set && spirv[offset + 4u] == glsl_fma)
            fused_results.push_back(spirv[offset + 2u]);
        // Types follow the complete annotations section in the logical layout.
        if (annotation_end == 0u && op >= 19u && op <= 39u) annotation_end = offset;
        offset += count;
    }
    vector<uint32_t> decorations;
    for (uint32_t result : fused_results) {
        if (std::find(decorated_results.begin(), decorated_results.end(), result) == decorated_results.end())
            decorations.insert(decorations.end(), {(3u << 16u) | op_decorate, result, no_contraction});
    }
    if (decorations.empty()) return;
    OC_ERROR_IF(annotation_end == 0u, "Slang SPIR-V has no type declarations");
    spirv.insert(spirv.begin() + annotation_end, decorations.begin(), decorations.end());
}

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

struct Stage {
    VkShaderStageFlagBits stage;
    const char *label;
    const char *entry;
};
constexpr Stage compute_stages[] = {{VK_SHADER_STAGE_COMPUTE_BIT, "compute", "main"}};
constexpr Stage native_stages[] = {
    {VK_SHADER_STAGE_RAYGEN_BIT_KHR, "raygen", "main"},
    {VK_SHADER_STAGE_MISS_BIT_KHR, "miss", "ocMiss"},
    {VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, "closesthit", "ocClosestHit"},
};

const char *mode_label(ShaderExecutionMode mode) noexcept {
    switch (mode) {
        case ShaderExecutionMode::Compute: return "compute";
        case ShaderExecutionMode::ComputeRayQuery: return "query";
        case ShaderExecutionMode::RayTracingPipeline: return "pipeline";
    }
    OC_ERROR("Unknown Slang execution mode");
    return "";
}

vector<uint32_t> read_spirv(const fs::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const auto size = file.tellg();
    if (size < 20 || size % 4 != 0) return {};
    vector<uint32_t> words(size_t(size) / sizeof(uint32_t));
    file.seekg(0);
    file.read(reinterpret_cast<char *>(words.data()), size);
    if (!file || words.front() != 0x07230203u) return {};
    return words;
}

// A cache reader must only see complete files, even with multiple compiler
// instances. Missing modules in an interrupted native build can be regenerated.
void write_cache_file(const fs::path &path, const void *data, size_t size) {
    static const auto process_nonce = std::random_device{}();
    static std::atomic<uint64_t> serial{};
    const auto temp = path.string() + ocarina::format(".{}.{}.tmp", process_nonce, serial.fetch_add(1));
    std::ofstream file(temp, std::ios::binary);
    file.write(static_cast<const char *>(data), std::streamsize(size));
    file.close();
    std::error_code error;
    if (file) fs::rename(temp, path, error);
    if (!file || error) {
        OC_WARNING_FORMAT("Could not publish Slang cache artifact {}", path.string());
        fs::remove(temp, error);
    }
}

void report_diagnostics(slang::IBlob *diagnostics) {
    if (diagnostics && diagnostics->getBufferSize())
        OC_WARNING_FORMAT("Slang diagnostics:\n{}", static_cast<const char *>(diagnostics->getBufferPointer()));
}

vector<uint32_t> compile_entry(slang::ISession *session, slang::IModule *module, const Stage &stage) {
    Slang::ComPtr<slang::IEntryPoint> entry;
    auto result = module->findEntryPointByName(stage.entry, entry.writeRef());
    OC_ERROR_IF(SLANG_FAILED(result) || !entry, "Slang entry point missing: ", stage.entry);
    slang::IComponentType *components[] = {module, entry.get()};
    Slang::ComPtr<slang::IComponentType> linked;
    Slang::ComPtr<slang::IBlob> diagnostics, blob;
    result = session->createCompositeComponentType(components, 2, linked.writeRef(), diagnostics.writeRef());
    report_diagnostics(diagnostics.get());
    OC_ERROR_IF(SLANG_FAILED(result), "Slang link failed for ", stage.entry);
    result = linked->getEntryPointCode(0, 0, blob.writeRef(), diagnostics.writeRef());
    report_diagnostics(diagnostics.get());
    OC_ERROR_IF(SLANG_FAILED(result) || !blob, "Slang SPIR-V generation failed for ", stage.entry);
    const auto *words = static_cast<const uint32_t *>(blob->getBufferPointer());
    return {words, words + blob->getBufferSize() / sizeof(uint32_t)};
}

}// namespace

SlangShaderCompiler::SlangShaderCompiler(VulkanComputeDevice *device) noexcept
    : device_(device) {}

SlangShaderCompiler::~SlangShaderCompiler() noexcept {
    for (auto *session : sessions_) if (session) session->release();
    if (global_session_) global_session_->release();
}

slang::ISession *SlangShaderCompiler::session_for(ShaderExecutionMode mode) const noexcept {
    auto &session = sessions_[static_cast<size_t>(mode)];
    if (!session) session = create_session(mode);
    return session;
}

slang::ISession *SlangShaderCompiler::create_session(ShaderExecutionMode mode) const noexcept {
    if (!global_session_) {
        auto result = slang::createGlobalSession(&global_session_);
        OC_ERROR_IF(SLANG_FAILED(result) || !global_session_, "Slang global session creation failed");
    }
    slang::TargetDesc target{};
    target.format = SLANG_SPIRV;
    target.profile = global_session_->findProfile("spirv_1_5");
    target.floatingPointMode = SLANG_FLOATING_POINT_MODE_PRECISE;
    VkPhysicalDeviceFloatControlsProperties float_controls{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &float_controls;
    vkGetPhysicalDeviceProperties2(device_->physical_device(), &properties);
    preserve_float32_ = float_controls.shaderSignedZeroInfNanPreserveFloat32 == VK_TRUE;
    preserve_float16_ = float_controls.shaderSignedZeroInfNanPreserveFloat16 == VK_TRUE;
    rte_widths_ = (float_controls.shaderRoundingModeRTEFloat16 ? 1u : 0u) |
                  (float_controls.shaderRoundingModeRTEFloat32 ? 2u : 0u) |
                  (float_controls.shaderRoundingModeRTEFloat64 ? 4u : 0u);
    rounding_independence_ = float_controls.roundingModeIndependence;
    OC_INFO_FORMAT("Slang FP32 signed-zero/NaN/Inf preservation: {}", preserve_float32_);

    // Slang's texture intrinsics carry these standard-library requirements even
    // when they only emit ordinary image operations. Declaring them avoids an
    // implicit profile upgrade; SPIR-V still declares only capabilities in use.
    // Warp operations validate ballot subgroup support during source emission;
    // ray queries are enabled separately.
    vector<const char *> capabilities{
        "SPV_KHR_non_semantic_info", "SPV_GOOGLE_user_type",
        "spvDerivativeControl", "spvImageQuery", "spvImageGatherExtended",
        "spvSparseResidency", "spvMinLod", "spvFragmentFullyCoveredEXT",
        "spvGroupNonUniform", "spvGroupNonUniformBallot", "spvGroupNonUniformArithmetic"};
    if (mode == ShaderExecutionMode::ComputeRayQuery) capabilities.push_back("spvRayQueryKHR");
    if (mode == ShaderExecutionMode::RayTracingPipeline) capabilities.push_back("spvRayTracingKHR");
    vector<slang::CompilerOptionEntry> options;
    for (const auto *name : capabilities) {
        auto cap = global_session_->findCapability(name);
        OC_ERROR_IF(cap == SLANG_CAPABILITY_UNKNOWN, "Unknown Slang capability: ", name);
        slang::CompilerOptionEntry option{};
        option.name = slang::CompilerOptionName::Capability;
        option.value.intValue0 = cap;
        options.push_back(option);
    }
    slang::CompilerOptionEntry entry_names{};
    entry_names.name = slang::CompilerOptionName::VulkanUseEntryPointName;
    entry_names.value.intValue0 = 1;
    options.push_back(entry_names);
    target.compilerOptionEntries = options.data();
    target.compilerOptionEntryCount = uint32_t(options.size());

    // Search paths for builtin .slang files copied next to the backend DLL.
    string builtin_dir = (device_->context()->runtime_directory() / "slang").string();
    const char *search_paths[] = {builtin_dir.c_str()};

    slang::SessionDesc session_desc{};
    session_desc.targets                 = &target;
    session_desc.targetCount             = 1;
    session_desc.searchPaths             = search_paths;
    session_desc.searchPathCount         = 1;
    session_desc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_ROW_MAJOR;

    slang::ISession *session = nullptr;
    auto result = global_session_->createSession(session_desc, &session);
    OC_ERROR_IF(SLANG_FAILED(result) || !session, "Slang target session creation failed");
    return session;
}

VkShaderProgram SlangShaderCompiler::compile_program(const Function &function, uint3 workgroup_size,
                                                      const ShaderCompilationContext &context) const noexcept {
    switch_profile::Scope profile{"SlangShaderCompiler::compile_program", "slang"};
    std::lock_guard lock(compile_mutex_);
    const auto fma_mode = context.options.float32_fma_mode;
    const bool use_native_float32_fma = fma_mode == Float32FmaMode::Native ||
        (fma_mode == Float32FmaMode::Auto && device_->supports_native_float32_fma());
    const auto mode = context.execution_mode;
    const bool native = mode == ShaderExecutionMode::RayTracingPipeline;
    const bool query = mode == ShaderExecutionMode::ComputeRayQuery;
    const bool cull = native && device_->has_primitive_culling();
    OC_ERROR_IF(query && !device_->has_ray_query(), "Slang ray query unavailable on device");
    OC_ERROR_IF(native && !device_->has_ray_tracing_pipeline(), "Slang native ray tracing pipeline unavailable on device");
    const char *label = mode_label(mode);
    auto *session = session_for(mode);
    auto [body, function_name, requires_half_rte] = AstToSlangSource::generate(
        function, context, Env::code_obfuscation(), device_->supports_warp32());
    OC_ERROR_IF(requires_half_rte && (!preserve_float32_ || !(rte_widths_ & 2u)),
                "Half fused operations require float32 RTE and signed-zero/NaN/Inf preservation on this device");
    OC_ERROR_IF(body.empty(), "Slang source missing for ", function_name);
    const string source = ocarina::format(
        "#define OC_RAY_QUERY {}\n#define OC_RAY_PIPELINE {}\n#define OC_PRIMITIVE_CULLING {}\n"
        "#define OC_WORKGROUP_X {}\n#define OC_WORKGROUP_Y {}\n#define OC_WORKGROUP_Z {}\n",
        int(query), int(native), int(cull), workgroup_size.x, workgroup_size.y, workgroup_size.z) +
        (use_native_float32_fma ? "#define OC_NATIVE_FLOAT32_FMA 1\n" : "") +
        "#include \"slang_device_resource.slang\"\n#include \"slang_device_math.slang\"\n" + body;

    const auto builtin_dir = device_->context()->runtime_directory() / "slang";
    const auto stages = native ? span<const Stage>(native_stages) : span<const Stage>(compute_stages);
    // Version covers target-profile options and the numeric postprocessing ABI.
    auto key = hash64(source, context.description, string_view(label), slang_source_cache_version,
        RHIContext::read_file(builtin_dir / "slang_device_math.slang"),
        RHIContext::read_file(builtin_dir / "slang_device_resource.slang"),
        string_view(global_session_->getBuildTagString()), string_view("spirv_1_5-precise-triangle-payload-v1"),
        preserve_float32_, preserve_float16_, requires_half_rte, rte_widths_, rounding_independence_);
    // The emitted body already includes entry headers. Hash their contents as
    // well, so replacing a literal header at the same path invalidates SPIR-V.
    bool has_literal_header = false;
    function.for_each_header([&](string_view path) {
        has_literal_header = true;
        const fs::path header{path};
        key = hash64(key, RHIContext::read_file(header));
        // In-memory modules also resolve relative includes in the session's
        // builtin search directory. Include both candidates in the cache key.
        if (header.is_relative()) key = hash64(key, RHIContext::read_file(builtin_dir / header));
    });
#ifdef __APPLE__
    key = hash64(key, string_view("moltenvk-volatile-loads"));
#endif
    for (const auto &stage : stages) key = hash64(key, stage.stage, string_view(stage.entry));
    const string program_key = ocarina::format("{}_{}_{:x}", function_name, label, key);
    const auto cache_dir = device_->context()->cache_directory() / "slang_cache";
    fs::create_directories(cache_dir);
    Slang::ComPtr<slang::ISession> literal_session;
    Slang::ComPtr<slang::IModule> module;
    VkShaderProgram program{mode, {}};
    for (const auto &stage : stages) {
        const string name = ocarina::format("{}_{}_{}", program_key, stage.label, stage.entry);
        const auto path = cache_dir / (name + ".spv");
        const auto diagnostic = cache_dir / (name + ".slang");
        if (!fs::exists(diagnostic)) write_cache_file(diagnostic, source.data(), source.size());
        auto spirv = read_spirv(path);
        if (spirv.empty()) {
            OC_INFO_FORMAT("shader SPIR-V cache miss: {}.spv", name);
            const auto start = std::chrono::steady_clock::now();
            if (!module) {
                if (has_literal_header) {
                    // A Slang session remembers included files by path. Start
                    // a fresh session when compiling a literal-backed cache miss.
                    literal_session.attach(create_session(mode));
                    session = literal_session.get();
                }
                Slang::ComPtr<slang::IBlob> diagnostics;
                module = session->loadModuleFromSourceString(program_key.c_str(), nullptr, source.c_str(), diagnostics.writeRef());
                report_diagnostics(diagnostics.get());
                OC_ERROR_IF(!module, "Slang source failed for ", diagnostic.string());
            }
            spirv = compile_entry(session, module.get(), stage);
            if (query) normalize_ray_query_extensions(spirv);
            if (preserve_float32_) set_float_control_mode(spirv, 4466u, 4461u, 2u);
            if (requires_half_rte)
                require_half_fused_rounding(spirv, rte_widths_, rounding_independence_, preserve_float16_);
            decorate_glsl_fma(spirv);
#ifdef __APPLE__
            mark_physical_storage_loads_volatile(spirv);
#endif
            write_cache_file(path, spirv.data(), spirv.size() * sizeof(uint32_t));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            OC_INFO_FORMAT("task Slang compile {}.slang is take {} ms", name, ms);
        } else {
            OC_INFO_FORMAT("shader SPIR-V cache hit: {}.spv", name);
        }
        program.modules.push_back({stage.stage, stage.entry, std::move(spirv)});
    }
    return program;
}

}// namespace ocarina
