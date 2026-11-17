#include <vulkan/vulkan.h>
#include <slang.h>
#include <slang-com-ptr.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
void check(VkResult result, const char *operation) {
    require(result == VK_SUCCESS, std::string(operation) + " returned " + std::to_string(result));
}
uint64_t aligned(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) / alignment * alignment;
}
std::string quoted(const std::string &value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 32u) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
                else out << char(c);
        }
    }
    return out.str() + '"';
}
const char *boolean(bool value) { return value ? "true" : "false"; }

// Only pipeline TraceRay calls are used. No ray-query types or instructions.
constexpr const char *shader_source = R"SLANG(
struct InputRay { float4 origin_min; float4 direction_max; uint4 mask; };
struct Output { uint4 ids; float4 geometry; };
struct Payload { uint instance; uint primitive; float distance; float2 bary; uint hit; };
[[vk::binding(0, 0)]] RaytracingAccelerationStructure scene;
[[vk::binding(1, 0)]] StructuredBuffer<InputRay> inputs;
[[vk::binding(2, 0)]] RWStructuredBuffer<Output> outputs;

[shader("raygeneration")]
void raygenMain() {
    uint i = DispatchRaysIndex().x;
    InputRay input = inputs[i];
    RayDesc ray;
    ray.Origin = input.origin_min.xyz;
    ray.TMin = input.origin_min.w;
    ray.Direction = input.direction_max.xyz;
    ray.TMax = input.direction_max.w;
    Payload p;
    p.instance = 0xffffffffu; p.primitive = 0xffffffffu;
    p.distance = -1.0f; p.bary = float2(0.0f); p.hit = 0u;
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE, input.mask.x, 0, 0, 0, ray, p);
    Output result;
    result.ids = uint4(p.instance, p.primitive, p.hit, 0u);
    result.geometry = float4(p.distance, p.bary, 0.0f);

    // A hit keeps this payload value; the miss shader clears it. Skipping the
    // closest-hit shader independently exercises the occlusion fast path.
    p.hit = 1u;
    TraceRay(scene, RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                    RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
             input.mask.x, 0, 0, 0, ray, p);
    result.ids.w = p.hit;
    outputs[i] = result;
}
[shader("closesthit")]
void closestMain(inout Payload p, BuiltInTriangleIntersectionAttributes attributes) {
    p.instance = InstanceID(); p.primitive = PrimitiveIndex();
    p.distance = RayTCurrent(); p.bary = attributes.barycentrics; p.hit = 1u;
}
[shader("miss")]
void missMain(inout Payload p) {
    p.instance = 0xffffffffu; p.primitive = 0xffffffffu;
    p.distance = -1.0f; p.bary = float2(0.0f); p.hit = 0u;
}
)SLANG";

struct InputRay { float origin_min[4]; float direction_max[4]; uint32_t mask[4]; };
struct Output { uint32_t ids[4]; float geometry[4]; };
static_assert(sizeof(InputRay) == 48 && sizeof(Output) == 32);
struct Expected { uint32_t instance, primitive, hit; float distance; };
constexpr std::array<Expected, 8> expected{{
    {0, 1, 1, 2.f}, {1, 1, 1, 2.f}, {~0u, ~0u, 0, -1.f}, {~0u, ~0u, 0, -1.f},
    {0, 0, 1, 4.f}, {0, 0, 1, 1.f}, {0, 0, 1, 1.f}, {~0u, ~0u, 0, -1.f}}};

struct Program { std::vector<uint32_t> words; std::string entry; };
struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; VkDeviceAddress address{}; void *mapped{}; };
struct Acceleration { VkAccelerationStructureKHR handle{}; VkDeviceAddress address{}; };

class Probe {
    std::filesystem::path report_path_;
    std::string phase_{"initializing"}, status_{"failed"}, error_;
    VkInstance instance_{};
    VkPhysicalDevice physical_{};
    VkDevice device_{};
    VkQueue queue_{};
    uint32_t queue_family_{};
    VkCommandPool command_pool_{};
    VkPhysicalDeviceProperties properties_{};
    VkPhysicalDeviceMemoryProperties memory_{};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_properties_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceAccelerationStructurePropertiesKHR as_properties_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
    bool extension_as_{}, extension_pipeline_{}, extension_deferred_{}, extension_query_{};
    bool feature_as_{}, feature_pipeline_{}, feature_address_{};
    bool submitted_{}, completed_{}, sentinels_{};
    uint32_t trace_instructions_{}, closest_verified_{}, occlusion_verified_{};
    std::vector<VkExtensionProperties> extensions_;
    std::vector<Buffer> buffers_;
    std::vector<Acceleration> structures_;
    std::vector<VkShaderModule> modules_;
    VkDescriptorSetLayout descriptor_layout_{};
    VkDescriptorPool descriptor_pool_{};
    VkPipelineLayout pipeline_layout_{};
    VkPipeline pipeline_{};
    std::array<Output, 8> actual_{};

    PFN_vkCreateAccelerationStructureKHR create_as_{};
    PFN_vkDestroyAccelerationStructureKHR destroy_as_{};
    PFN_vkGetAccelerationStructureBuildSizesKHR build_sizes_{};
    PFN_vkCmdBuildAccelerationStructuresKHR cmd_build_{};
    PFN_vkGetAccelerationStructureDeviceAddressKHR as_address_{};
    PFN_vkCreateRayTracingPipelinesKHR create_pipeline_{};
    PFN_vkGetRayTracingShaderGroupHandlesKHR group_handles_{};
    PFN_vkCmdTraceRaysKHR cmd_trace_{};

    void phase(const char *value) {
        phase_ = value;
        std::cout << "TRACE_RAY_PHASE " << phase_ << std::endl;
        write_report();
    }
    bool extension(const char *name) const {
        return std::any_of(extensions_.begin(), extensions_.end(), [&](auto &e) { return std::strcmp(e.extensionName, name) == 0; });
    }

    std::array<Program, 3> compile_shaders() {
        phase("compile-native-trace-ray-shaders");
        Slang::ComPtr<slang::IGlobalSession> global;
        require(SLANG_SUCCEEDED(slang::createGlobalSession(global.writeRef())), "Slang global session failed");
        slang::CompilerOptionEntry capability{};
        capability.name = slang::CompilerOptionName::Capability;
        capability.value.kind = slang::CompilerOptionValueKind::Int;
        capability.value.intValue0 = global->findCapability("spvRayTracingKHR");
        require(capability.value.intValue0 != SLANG_CAPABILITY_UNKNOWN, "Slang has no ray-tracing pipeline capability");
        slang::TargetDesc target{};
        target.format = SLANG_SPIRV;
        target.profile = global->findProfile("spirv_1_5");
        target.floatingPointMode = SLANG_FLOATING_POINT_MODE_PRECISE;
        target.compilerOptionEntries = &capability;
        target.compilerOptionEntryCount = 1;
        slang::SessionDesc description{};
        description.targets = &target; description.targetCount = 1;
        Slang::ComPtr<slang::ISession> session;
        require(SLANG_SUCCEEDED(global->createSession(description, session.writeRef())), "Slang session failed");
        Slang::ComPtr<slang::IBlob> diagnostic;
        Slang::ComPtr<slang::IModule> module;
        module = session->loadModuleFromSourceString("trace_ray_probe", nullptr, shader_source, diagnostic.writeRef());
        if (diagnostic) std::cerr << static_cast<const char *>(diagnostic->getBufferPointer());
        require(module != nullptr, "Slang shader module failed");
        std::array<Program, 3> programs;
        const char *names[]{"raygenMain", "missMain", "closestMain"};
        for (size_t i = 0; i < programs.size(); ++i) {
            Slang::ComPtr<slang::IEntryPoint> entry;
            require(SLANG_SUCCEEDED(module->findEntryPointByName(names[i], entry.writeRef())), "missing Slang entry point");
            slang::IComponentType *components[]{module.get(), entry.get()};
            Slang::ComPtr<slang::IComponentType> linked;
            require(SLANG_SUCCEEDED(session->createCompositeComponentType(components, 2, linked.writeRef(), diagnostic.writeRef())),
                    "Slang entry-point linking failed");
            Slang::ComPtr<slang::IBlob> code;
            const auto result = linked->getEntryPointCode(0, 0, code.writeRef(), diagnostic.writeRef());
            if (diagnostic) std::cerr << static_cast<const char *>(diagnostic->getBufferPointer());
            require(SLANG_SUCCEEDED(result) && code && code->getBufferSize() % 4u == 0u, "Slang SPIR-V generation failed");
            auto &program = programs[i];
            const auto words = static_cast<const uint32_t *>(code->getBufferPointer());
            program.words.assign(words, words + code->getBufferSize() / 4u);
            for (size_t offset = 5; offset < program.words.size();) {
                const uint32_t count = program.words[offset] >> 16u, op = program.words[offset] & 0xffffu;
                require(count && offset + count <= program.words.size(), "malformed SPIR-V");
                if (op == 15u) {
                    const char *first = reinterpret_cast<const char *>(program.words.data() + offset + 3u);
                    const char *last = reinterpret_cast<const char *>(program.words.data() + offset + count);
                    program.entry.assign(first, std::find(first, last, '\0'));
                }
                if (op == 17u && count == 2u) require(program.words[offset + 1u] != 4472u, "unexpected RayQuery capability");
                if (op == 4445u) ++trace_instructions_; // OpTraceRayKHR
                offset += count;
            }
            require(!program.entry.empty(), "SPIR-V entry-point name missing");
            std::ofstream output(report_path_.parent_path() / (std::string(names[i]) + ".spv"), std::ios::binary);
            output.write(reinterpret_cast<const char *>(program.words.data()), std::streamsize(code->getBufferSize()));
        }
        require(trace_instructions_ == 2u, "expected two pipeline TraceRay instructions");
        return programs;
    }

    void initialize() {
        phase("query-physical-device-capabilities");
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.pApplicationName = "Horizon standalone traceRay probe";
        application.apiVersion = VK_API_VERSION_1_2;
        uint32_t extension_count = 0;
        check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr), "enumerate instance extensions");
        std::vector<VkExtensionProperties> available(extension_count);
        check(vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, available.data()), "enumerate instance extensions");
        const bool portability = std::any_of(available.begin(), available.end(), [](const auto &e) {
            return std::strcmp(e.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
        });
        const char *instance_extensions[]{VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &application;
        if (portability) {
            info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = instance_extensions;
        }
        check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
        uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "vkEnumeratePhysicalDevices");
        require(count != 0, "no Vulkan physical device");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "vkEnumeratePhysicalDevices");
        physical_ = devices.front();
        vkGetPhysicalDeviceProperties(physical_, &properties_);
        vkGetPhysicalDeviceMemoryProperties(physical_, &memory_);
        check(vkEnumerateDeviceExtensionProperties(physical_, nullptr, &count, nullptr), "enumerate device extensions");
        extensions_.resize(count);
        check(vkEnumerateDeviceExtensionProperties(physical_, nullptr, &count, extensions_.data()), "enumerate device extensions");
        extension_as_ = extension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        extension_pipeline_ = extension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        extension_deferred_ = extension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        extension_query_ = extension(VK_KHR_RAY_QUERY_EXTENSION_NAME); // Diagnostic only, never required/enabled.
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR pipeline_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &features12; features12.pNext = &as_features; as_features.pNext = &pipeline_features;
        vkGetPhysicalDeviceFeatures2(physical_, &features);
        feature_as_ = as_features.accelerationStructure;
        feature_pipeline_ = pipeline_features.rayTracingPipeline;
        feature_address_ = features12.bufferDeviceAddress;
        VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        properties.pNext = &ray_properties_; ray_properties_.pNext = &as_properties_;
        vkGetPhysicalDeviceProperties2(physical_, &properties);
        std::cout << "TRACE_RAY_DEVICE " << properties_.deviceName
                  << " accelerationStructure=" << feature_as_ << " rayTracingPipeline=" << feature_pipeline_
                  << " bufferDeviceAddress=" << feature_address_ << " rayQueryExtension=" << extension_query_ << std::endl;
        if (!(extension_as_ && extension_pipeline_ && extension_deferred_ && feature_as_ && feature_pipeline_ && feature_address_)) {
            status_ = "unsupported";
            throw std::runtime_error("physical device cannot execute native traceRay: required extension/feature unavailable");
        }
        require(ray_properties_.maxRayRecursionDepth >= 1u, "device reports no ray recursion depth");
        vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_, &count, queues.data());
        queue_family_ = count;
        for (uint32_t i = 0; i < count; ++i)
            if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) { queue_family_ = i; break; }
        require(queue_family_ != count, "no compute-capable queue");
        float priority = 1.f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = queue_family_; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        std::vector<const char *> requested{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
        if (extension("VK_KHR_portability_subset")) requested.push_back("VK_KHR_portability_subset");
        VkPhysicalDeviceRayTracingPipelineFeaturesKHR enable_pipeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
        enable_pipeline.rayTracingPipeline = true;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR enable_as{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
        enable_as.accelerationStructure = true; enable_as.pNext = &enable_pipeline;
        VkPhysicalDeviceVulkan12Features enable12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        enable12.bufferDeviceAddress = true; enable12.pNext = &enable_as;
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        device.pNext = &enable12; device.queueCreateInfoCount = 1; device.pQueueCreateInfos = &queue;
        device.enabledExtensionCount = uint32_t(requested.size()); device.ppEnabledExtensionNames = requested.data();
        check(vkCreateDevice(physical_, &device, nullptr, &device_), "vkCreateDevice (no ray-query extension)");
        vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
#define LOAD(member, name) member = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device_, #name)); require(member != nullptr, "missing " #name)
        LOAD(create_as_, vkCreateAccelerationStructureKHR);
        LOAD(destroy_as_, vkDestroyAccelerationStructureKHR);
        LOAD(build_sizes_, vkGetAccelerationStructureBuildSizesKHR);
        LOAD(cmd_build_, vkCmdBuildAccelerationStructuresKHR);
        LOAD(as_address_, vkGetAccelerationStructureDeviceAddressKHR);
        LOAD(create_pipeline_, vkCreateRayTracingPipelinesKHR);
        LOAD(group_handles_, vkGetRayTracingShaderGroupHandlesKHR);
        LOAD(cmd_trace_, vkCmdTraceRaysKHR);
#undef LOAD
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = queue_family_;
        check(vkCreateCommandPool(device_, &pool, nullptr, &command_pool_), "vkCreateCommandPool");
    }

    Buffer buffer(VkDeviceSize size, VkBufferUsageFlags usage) {
        buffers_.push_back({});
        auto &out = buffers_.back();
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size; info.usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        check(vkCreateBuffer(device_, &info, nullptr, &out.buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device_, out.buffer, &requirements);
        uint32_t memory_type = memory_.memoryTypeCount;
        const auto wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < memory_.memoryTypeCount; ++i)
            if ((requirements.memoryTypeBits & (1u << i)) && (memory_.memoryTypes[i].propertyFlags & wanted) == wanted) {
                memory_type = i; break;
            }
        require(memory_type != memory_.memoryTypeCount, "no host-coherent buffer memory");
        VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.pNext = &flags; allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = memory_type;
        check(vkAllocateMemory(device_, &allocation, nullptr, &out.memory), "vkAllocateMemory");
        check(vkBindBufferMemory(device_, out.buffer, out.memory, 0), "vkBindBufferMemory");
        check(vkMapMemory(device_, out.memory, 0, VK_WHOLE_SIZE, 0, &out.mapped), "vkMapMemory");
        VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
        address.buffer = out.buffer; out.address = vkGetBufferDeviceAddress(device_, &address);
        require(out.address != 0, "null buffer device address");
        return out;
    }

    VkCommandBuffer begin() {
        VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        allocation.commandPool = command_pool_; allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount = 1;
        VkCommandBuffer command;
        check(vkAllocateCommandBuffers(device_, &allocation, &command), "vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &info), "vkBeginCommandBuffer");
        return command;
    }
    void finish(VkCommandBuffer command, bool trace = false) {
        check(vkEndCommandBuffer(command), "vkEndCommandBuffer");
        VkSubmitInfo submission{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submission.commandBufferCount = 1; submission.pCommandBuffers = &command;
        check(vkQueueSubmit(queue_, 1, &submission, VK_NULL_HANDLE), "vkQueueSubmit");
        if (trace) { submitted_ = true; write_report(); }
        check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
        if (trace) completed_ = true;
        vkFreeCommandBuffers(device_, command_pool_, 1, &command);
    }

    Acceleration build(VkAccelerationStructureTypeKHR type, VkAccelerationStructureGeometryKHR geometry, uint32_t count) {
        VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        info.type = type; info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        info.geometryCount = 1; info.pGeometries = &geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        build_sizes_(device_, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &count, &sizes);
        auto storage = buffer(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
        VkAccelerationStructureCreateInfoKHR creation{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        creation.buffer = storage.buffer; creation.size = sizes.accelerationStructureSize; creation.type = type;
        Acceleration result;
        check(create_as_(device_, &creation, nullptr, &result.handle), "vkCreateAccelerationStructureKHR");
        structures_.push_back(result);
        VkAccelerationStructureDeviceAddressInfoKHR address{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        address.accelerationStructure = result.handle;
        result.address = as_address_(device_, &address);
        const uint64_t alignment = as_properties_.minAccelerationStructureScratchOffsetAlignment;
        require(alignment != 0 && result.address != 0, "invalid acceleration-structure address/alignment");
        auto scratch = buffer(sizes.buildScratchSize + alignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        info.dstAccelerationStructure = result.handle;
        info.scratchData.deviceAddress = aligned(scratch.address, alignment);
        VkAccelerationStructureBuildRangeInfoKHR range{count, 0, 0, 0};
        const auto *ranges = &range;
        auto command = begin();
        cmd_build_(command, 1, &info, &ranges);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 1, &barrier, 0, nullptr, 0, nullptr);
        finish(command);
        return result;
    }

    Acceleration build_scene() {
        phase("build-two-triangle-two-instance-scene");
        // The farther primitive is listed first, so primitive selection must
        // actually find the nearest intersection rather than the first one.
        const float vertices[]{-1,-1,4, 1,-1,4, 0,1,4, -1,-1,2, 1,-1,2, 0,1,2};
        const uint32_t indices[]{0,1,2,3,4,5};
        auto vb = buffer(sizeof(vertices), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
        auto ib = buffer(sizeof(indices), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
        std::memcpy(vb.mapped, vertices, sizeof(vertices)); std::memcpy(ib.mapped, indices, sizeof(indices));
        VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR; geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto &triangles = geometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT; triangles.vertexData.deviceAddress = vb.address;
        triangles.vertexStride = 3u * sizeof(float); triangles.maxVertex = 5;
        triangles.indexType = VK_INDEX_TYPE_UINT32; triangles.indexData.deviceAddress = ib.address;
        const auto blas = build(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, 2);
        std::array<VkAccelerationStructureInstanceKHR, 2> instances{};
        for (uint32_t i = 0; i < 2; ++i) {
            auto &instance = instances[i];
            instance.transform.matrix[0][0] = instance.transform.matrix[1][1] = instance.transform.matrix[2][2] = 1.f;
            instance.transform.matrix[0][3] = float(i * 3u);
            instance.instanceCustomIndex = i; instance.mask = 1u;
            instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
            instance.accelerationStructureReference = blas.address;
        }
        auto instance_buffer = buffer(sizeof(instances), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR);
        std::memcpy(instance_buffer.mapped, instances.data(), sizeof(instances));
        geometry = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
        geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geometry.geometry.instances.data.deviceAddress = instance_buffer.address;
        return build(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geometry, 2);
    }

    void execute(const std::array<Program, 3> &programs, Acceleration scene) {
        phase("create-ray-tracing-pipeline");
        const VkShaderStageFlags stages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR;
        const VkDescriptorSetLayoutBinding bindings[]{
            {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, stages, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, stages, nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptor{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        descriptor.bindingCount = 3; descriptor.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device_, &descriptor, nullptr, &descriptor_layout_), "vkCreateDescriptorSetLayout");
        VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout.setLayoutCount = 1; layout.pSetLayouts = &descriptor_layout_;
        check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipeline_layout_), "vkCreatePipelineLayout");
        std::array<VkPipelineShaderStageCreateInfo, 3> shader_stages{};
        const VkShaderStageFlagBits kinds[]{VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
        for (uint32_t i = 0; i < 3; ++i) {
            VkShaderModuleCreateInfo module{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            module.codeSize = programs[i].words.size() * 4u; module.pCode = programs[i].words.data();
            VkShaderModule handle;
            check(vkCreateShaderModule(device_, &module, nullptr, &handle), "vkCreateShaderModule");
            modules_.push_back(handle);
            shader_stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
            shader_stages[i].stage = kinds[i]; shader_stages[i].module = handle; shader_stages[i].pName = programs[i].entry.c_str();
        }
        std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> groups{};
        for (auto &group : groups) {
            group = {VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR};
            group.generalShader = group.closestHitShader = group.anyHitShader = group.intersectionShader = VK_SHADER_UNUSED_KHR;
        }
        groups[0].type = groups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        groups[0].generalShader = 0; groups[1].generalShader = 1;
        groups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR; groups[2].closestHitShader = 2;
        VkRayTracingPipelineCreateInfoKHR creation{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
        creation.stageCount = 3; creation.pStages = shader_stages.data(); creation.groupCount = 3; creation.pGroups = groups.data();
        creation.maxPipelineRayRecursionDepth = 1; creation.layout = pipeline_layout_;
        check(create_pipeline_(device_, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &creation, nullptr, &pipeline_), "vkCreateRayTracingPipelinesKHR");

        const uint64_t handle_size = ray_properties_.shaderGroupHandleSize;
        require(handle_size && ray_properties_.shaderGroupHandleAlignment && ray_properties_.shaderGroupBaseAlignment,
                "invalid shader binding table alignment properties");
        const uint64_t stride = aligned(handle_size, ray_properties_.shaderGroupHandleAlignment);
        const uint64_t pitch = aligned(stride, ray_properties_.shaderGroupBaseAlignment);
        require(stride <= ray_properties_.maxShaderGroupStride, "SBT stride exceeds device limit");
        std::vector<unsigned char> handles(size_t(3u * handle_size));
        check(group_handles_(device_, pipeline_, 0, 3, handles.size(), handles.data()), "vkGetRayTracingShaderGroupHandlesKHR");
        auto sbt = buffer(3u * pitch + ray_properties_.shaderGroupBaseAlignment, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR);
        const auto start = aligned(sbt.address, ray_properties_.shaderGroupBaseAlignment);
        auto mapped = static_cast<unsigned char *>(sbt.mapped) + (start - sbt.address);
        for (uint32_t i = 0; i < 3; ++i) std::memcpy(mapped + i * pitch, handles.data() + i * handle_size, size_t(handle_size));
        VkStridedDeviceAddressRegionKHR raygen{start, stride, stride}, miss{start + pitch, stride, stride},
            hit{start + 2u * pitch, stride, stride}, callable{};

        std::array<InputRay, 8> rays{};
        for (auto &ray : rays) {
            ray.origin_min[3] = .001f; ray.direction_max[2] = 1.f;
            ray.direction_max[3] = 10.f; ray.mask[0] = 1;
        }
        rays[1].origin_min[0] = 3.f; rays[2].origin_min[0] = 10.f;
        rays[3].direction_max[3] = 1.f; rays[4].origin_min[3] = 2.5f;
        rays[5].origin_min[2] = 3.f;
        rays[6].origin_min[2] = 5.f; rays[6].direction_max[2] = -1.f;
        rays[7].mask[0] = 2;
        auto input = buffer(sizeof(rays), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        auto output = buffer(sizeof(Output) * 10u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        std::memcpy(input.mapped, rays.data(), sizeof(rays)); std::memset(output.mapped, 0xcd, sizeof(Output) * 10u);
        const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 1; pool.poolSizeCount = 2; pool.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptor_pool_), "vkCreateDescriptorPool");
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = descriptor_pool_; allocate.descriptorSetCount = 1; allocate.pSetLayouts = &descriptor_layout_;
        VkDescriptorSet set;
        check(vkAllocateDescriptorSets(device_, &allocate, &set), "vkAllocateDescriptorSets");
        VkWriteDescriptorSetAccelerationStructureKHR as_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR};
        as_write.accelerationStructureCount = 1; as_write.pAccelerationStructures = &scene.handle;
        VkDescriptorBufferInfo input_info{input.buffer, 0, sizeof(rays)}, output_info{output.buffer, sizeof(Output), sizeof(actual_)};
        std::array<VkWriteDescriptorSet, 3> writes{};
        for (uint32_t i = 0; i < 3; ++i) {
            writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; writes[i].dstSet = set; writes[i].dstBinding = i; writes[i].descriptorCount = 1;
            writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        writes[0].pNext = &as_write; writes[1].pBufferInfo = &input_info; writes[2].pBufferInfo = &output_info;
        vkUpdateDescriptorSets(device_, 3, writes.data(), 0, nullptr);
        phase("submit-vkCmdTraceRaysKHR");
        auto command = begin();
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_layout_, 0, 1, &set, 0, nullptr);
        cmd_trace_(command, &raygen, &miss, &hit, &callable, 8, 1, 1);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, VK_PIPELINE_STAGE_HOST_BIT,
                             0, 1, &host, 0, nullptr, 0, nullptr);
        finish(command, true);
        phase("verify-gpu-readback");
        const auto data = static_cast<const unsigned char *>(output.mapped);
        sentinels_ = true;
        for (size_t i = 0; i < sizeof(Output); ++i)
            sentinels_ &= data[i] == 0xcd && data[9u * sizeof(Output) + i] == 0xcd;
        std::memcpy(actual_.data(), data + sizeof(Output), sizeof(actual_));
        for (size_t i = 0; i < actual_.size(); ++i) {
            auto &a = actual_[i]; auto &e = expected[i];
            const float u = e.hit ? .25f : 0.f, v = e.hit ? .5f : 0.f;
            const bool closest = a.ids[0] == e.instance && a.ids[1] == e.primitive && a.ids[2] == e.hit &&
                                 std::abs(a.geometry[0] - e.distance) < 1e-4f && std::abs(a.geometry[1] - u) < 1e-4f &&
                                 std::abs(a.geometry[2] - v) < 1e-4f;
            closest_verified_ += closest;
            occlusion_verified_ += a.ids[3] == e.hit;
            std::cout << "TRACE_RAY_READBACK ray=" << i << " instance=" << a.ids[0] << " primitive=" << a.ids[1]
                      << " distance=" << a.geometry[0] << " occluded=" << a.ids[3] << " closest_match=" << closest << std::endl;
        }
        require(sentinels_ && closest_verified_ == 8u && occlusion_verified_ == 8u, "traceRay closest-hit/occlusion readback mismatch");
        status_ = "passed"; phase("verified");
    }

public:
    explicit Probe(std::filesystem::path report) : report_path_(std::move(report)) {}
    ~Probe() {
        if (device_) {
            vkDeviceWaitIdle(device_);
            if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
            if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
            if (descriptor_pool_) vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
            if (descriptor_layout_) vkDestroyDescriptorSetLayout(device_, descriptor_layout_, nullptr);
            for (auto module : modules_) vkDestroyShaderModule(device_, module, nullptr);
            for (auto object : structures_) destroy_as_(device_, object.handle, nullptr);
            for (auto b : buffers_) {
                if (b.mapped) vkUnmapMemory(device_, b.memory);
                if (b.buffer) vkDestroyBuffer(device_, b.buffer, nullptr);
                if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
            }
            if (command_pool_) vkDestroyCommandPool(device_, command_pool_, nullptr);
            vkDestroyDevice(device_, nullptr);
        }
        if (instance_) vkDestroyInstance(instance_, nullptr);
    }
    void write_report() const {
        std::ofstream out(report_path_);
        out << "{\n  \"status\": " << quoted(status_) << ",\n  \"phase\": " << quoted(phase_)
            << ",\n  \"error\": " << quoted(error_) << ",\n  \"device\": " << quoted(properties_.deviceName)
            << ",\n  \"vendor_id\": " << properties_.vendorID << ",\n  \"device_id\": " << properties_.deviceID
            << ",\n  \"driver_version\": " << properties_.driverVersion << ",\n  \"ray_query_enabled\": false,\n  \"extensions\": {\"acceleration_structure\": "
            << boolean(extension_as_) << ", \"ray_tracing_pipeline\": " << boolean(extension_pipeline_)
            << ", \"deferred_host_operations\": " << boolean(extension_deferred_) << ", \"ray_query_advertised\": " << boolean(extension_query_) << "},\n"
            << "  \"features\": {\"accelerationStructure\": " << boolean(feature_as_) << ", \"rayTracingPipeline\": "
            << boolean(feature_pipeline_) << ", \"bufferDeviceAddress\": " << boolean(feature_address_) << "},\n"
            << "  \"max_ray_recursion_depth\": " << ray_properties_.maxRayRecursionDepth
            << ",\n  \"trace_ray_instructions\": " << trace_instructions_ << ",\n  \"ray_count\": 8,\n  \"submitted\": "
            << boolean(submitted_) << ",\n  \"completed\": " << boolean(completed_) << ",\n  \"sentinels_intact\": " << boolean(sentinels_)
            << ",\n  \"cases\": [\n    {\"name\": \"closest-hit\", \"status\": "
            << quoted(completed_ ? (closest_verified_ == 8 ? "passed" : "failed") : "not-run")
            << ", \"verified_rays\": " << closest_verified_ << "},\n    {\"name\": \"occlusion\", \"status\": "
            << quoted(completed_ ? (occlusion_verified_ == 8 ? "passed" : "failed") : "not-run")
            << ", \"verified_rays\": " << occlusion_verified_ << "}\n  ],\n  \"readback\": [";
        if (completed_) for (size_t i = 0; i < actual_.size(); ++i) {
            if (i) out << ',';
            auto &a = actual_[i]; auto &e = expected[i];
            out << "\n    {\"ray\": " << i << ", \"instance\": " << a.ids[0] << ", \"primitive\": " << a.ids[1]
                << ", \"hit\": " << a.ids[2] << ", \"occluded\": " << a.ids[3] << ", \"distance\": ";
            if (std::isfinite(a.geometry[0])) out << a.geometry[0]; else out << "null";
            out << ", \"expected_instance\": " << e.instance << ", \"expected_primitive\": " << e.primitive
                << ", \"expected_hit\": " << e.hit << ", \"expected_distance\": " << e.distance << '}';
        }
        out << "\n  ]\n}\n";
    }
    int run() {
        try {
            const auto programs = compile_shaders();
            initialize();
            const auto scene = build_scene();
            execute(programs, scene);
            return 0;
        } catch (const std::exception &error) {
            error_ = error.what(); write_report();
            std::cerr << "TRACE_RAY_RESULT status=" << status_ << " phase=" << phase_ << " error=" << error_ << std::endl;
            return status_ == "unsupported" ? 2 : 1;
        }
    }
};
}

int main(int argc, char **argv) {
    const auto output = std::filesystem::absolute(argc == 2 ? argv[1] : "trace-ray-result.json");
    std::filesystem::create_directories(output.parent_path());
    Probe probe(output);
    return probe.run();
}
