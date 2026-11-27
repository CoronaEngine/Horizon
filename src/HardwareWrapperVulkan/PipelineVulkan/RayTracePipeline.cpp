#include "PipelineSnapshot.h"

#include <array>
#include <cstring>

namespace {
using Ray = RayTracePipeline<>;
using Module = EmbeddedShader::ShaderCodeModule;
using Buffer = ResourceManager::BufferHardwareWrap;
constexpr auto buildStage = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
constexpr auto traceStage = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR;
constexpr auto asRead = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
constexpr auto asWrite = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
constexpr auto buildInput = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;

const DeviceManager::FeaturesUtils& rayDevice()
{
    const auto& caps = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils();
    if (!caps.rayTrace) throw std::runtime_error("Device does not support ray tracing pipelines");
    return caps;
}

VkDeviceAddress address(const Buffer& buffer)
{
    VkBufferDeviceAddressInfo info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    info.buffer = buffer.bufferHandle;
    return vkGetBufferDeviceAddress(buffer.device->getLogicalDevice(), &info);
}

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

// Private GPU allocations have the same access tracking and release path as ordinary buffers.
struct RayBuffer {
    Buffer value{};
    RayBuffer() = default;
    RayBuffer(const RayBuffer&) = delete;
    RayBuffer& operator=(const RayBuffer&) = delete;
    ~RayBuffer() { if (value.bufferHandle) value.resourceManager->destroyBuffer(value); }

    void allocate(VkDeviceSize bytes, VkBufferUsageFlags usage, bool mapped = false)
    {
        if (bytes > UINT32_MAX) throw std::length_error("Ray tracing allocation exceeds the buffer size limit");
        value = getHardwareContext().getMainDevice()->resourceManager.createBuffer(
            1, static_cast<uint32_t>(bytes), usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, mapped, false, true);
    }
};
}

template<class... Stages>
struct RayTracePipeline<Stages...>::AccelerationStructure::Impl {
    VkAccelerationStructureKHR handle{};
    VkDeviceAddress address{};
    VkAccelerationStructureTypeKHR type{};
    RayBuffer storage;
    std::vector<AccelerationStructure> geometries;

    ~Impl() {
        if (handle) {
            auto& device = *storage.value.device;
            device.getFeaturesUtils().destroyAS(device.getLogicalDevice(), handle, nullptr);
        }
    }
};

struct HardwareExecutor::Command::Impl::BuildAS : HardwareExecutor::Command::Impl, CommandRecordVulkan {
    std::shared_ptr<Ray::AccelerationStructure::Impl> target = std::make_shared<Ray::AccelerationStructure::Impl>();
    HardwareBuffer vertices, indices;
    RayBuffer instances, scratch;
    VkAccelerationStructureGeometryKHR geometry{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR};
    VkAccelerationStructureBuildGeometryInfoKHR info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
    VkAccelerationStructureBuildRangeInfoKHR range{};

    Entry queueEntry(std::shared_ptr<HardwareExecutor::Command::Impl> owner) override { return RecordedCommand(*this, std::move(owner)); }
    ExecutorType getExecutorType() override { return ExecutorType::Compute; }

    void allocate(VkAccelerationStructureTypeKHR type, uint32_t count)
    {
        const auto& caps = rayDevice();
        const auto device = getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice();
        target->type = type;
        range.primitiveCount = count;
        info.type = type;
        info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
        info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount = 1;
        info.pGeometries = &geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        caps.buildSizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &count, &sizes);
        target->storage.allocate(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
        VkAccelerationStructureCreateInfoKHR create{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        create.buffer = target->storage.value.bufferHandle;
        create.size = sizes.accelerationStructureSize;
        create.type = type;
        coronaHardwareCheck(caps.createAS(device, &create, nullptr, &target->handle));
        info.dstAccelerationStructure = target->handle;
        VkAccelerationStructureDeviceAddressInfoKHR location{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
        location.accelerationStructure = target->handle;
        target->address = caps.addressAS(device, &location);
        if (sizes.buildScratchSize) {
            const auto alignment = caps.accelerationStructureProperties.minAccelerationStructureScratchOffsetAlignment;
            scratch.allocate(sizes.buildScratchSize + alignment - 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            info.scratchData.deviceAddress = alignUp(address(scratch.value), alignment);
        }
    }

    void resolveResourceUses(ResourceAccessList& uses) override
    {
        uses.buffer(target->storage.value, buildStage, asWrite);
        if (scratch.value.bufferHandle) uses.buffer(scratch.value, buildStage, asRead | asWrite);
        if (vertices) {
            uses.buffer(*ResourceManager::get(vertices), buildStage, VK_ACCESS_2_SHADER_READ_BIT);
            uses.buffer(*ResourceManager::get(indices), buildStage, VK_ACCESS_2_SHADER_READ_BIT);
        } else {
            if (instances.value.bufferHandle) uses.buffer(instances.value, buildStage, VK_ACCESS_2_SHADER_READ_BIT);
            for (const auto& child : target->geometries) uses.buffer(child.impl_->storage.value, buildStage, asRead);
        }
    }

    void commitCommand(HardwareExecutorVulkan& executor) override
    {
        const auto* ranges = &range;
        rayDevice().buildAS(executor.currentRecordQueue->commandBuffer, 1, &info, &ranges);
    }
};

template<class... Stages>
HardwareExecutor::Command RayTracePipeline<Stages...>::AccelerationStructure::build(
    const HardwareBuffer& vertices, const HardwareBuffer& indices, uint32_t positionOffset)
{
    const auto& caps = rayDevice();
    const auto* v = ResourceManager::get(vertices);
    const auto* i = ResourceManager::get(indices);
    if (!v || !i || !v->elementCount || !i->elementCount || i->elementCount % 3 ||
        (i->elementSize != 2 && i->elementSize != 4) || v->elementSize % 4 || positionOffset % 4 ||
        uint64_t(positionOffset) + 12 > v->elementSize || !(v->bufferUsage & buildInput) || !(i->bufferUsage & buildInput))
        throw std::invalid_argument("Triangle builds require float3 positions and triangle-list uint16 or uint32 indices");
    const auto count = i->elementCount / 3;
    if (count > caps.accelerationStructureProperties.maxPrimitiveCount) throw std::length_error("Too many ray tracing triangles");
    auto build = std::make_shared<HardwareExecutor::Command::Impl::BuildAS>();
    build->vertices = vertices;
    build->indices = indices;
    build->geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    build->geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    auto& triangles = build->geometry.geometry.triangles;
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = address(*v) + positionOffset;
    triangles.vertexStride = v->elementSize;
    triangles.maxVertex = v->elementCount - 1;
    triangles.indexType = i->elementSize == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = address(*i);
    build->allocate(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, count);
    impl_ = build->target;
    return HardwareExecutor::Command(std::move(build));
}

template<class... Stages>
HardwareExecutor::Command RayTracePipeline<Stages...>::AccelerationStructure::build(std::span<const Instance> instances)
{
    const auto& caps = rayDevice();
    if (instances.size() > caps.accelerationStructureProperties.maxInstanceCount ||
        instances.size() > UINT32_MAX / sizeof(VkAccelerationStructureInstanceKHR))
        throw std::length_error("Too many ray tracing instances");

    auto build = std::make_shared<HardwareExecutor::Command::Impl::BuildAS>();
    std::vector<VkAccelerationStructureInstanceKHR> native;
    native.reserve(instances.size());
    std::unordered_set<const Impl*> referenced;
    for (const auto& instance : instances) {
        if (!instance.geometry.impl_ || instance.geometry.impl_->type != VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)
            throw std::invalid_argument("Instance geometry must reference a BLAS");
        if (instance.id > 0xffffff) throw std::out_of_range("Instance ID exceeds 24 bits");
        auto& item = native.emplace_back();
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col) item.transform.matrix[row][col] = instance.transform[col][row];
        item.instanceCustomIndex = instance.id;
        item.mask = instance.mask;
        item.accelerationStructureReference = instance.geometry.impl_->address;
        if (referenced.insert(instance.geometry.impl_.get()).second)
            build->target->geometries.push_back(instance.geometry);
    }
    build->geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    auto& data = build->geometry.geometry.instances;
    data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    // Vulkan also requires a valid input address for zero active primitives.
    const size_t bytes = std::max<size_t>(native.size(), 1) * sizeof(VkAccelerationStructureInstanceKHR);
    build->instances.allocate(bytes, buildInput, true);
    if (!native.empty()) build->instances.value.resourceManager->writeMapped(build->instances.value, native.data(), bytes);
    data.data.deviceAddress = address(build->instances.value);
    build->allocate(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, static_cast<uint32_t>(instances.size()));
    impl_ = build->target;
    return HardwareExecutor::Command(std::move(build));
}

namespace {
constexpr VkShaderStageFlags rayStages = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR |
                                         VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
struct RayTracePipelineVulkan {
    VkDevice device{};
    VkDescriptorSetLayout sceneLayout{};
    VkPipelineLayout layout{};
    VkPipeline pipeline{};
    RayBuffer sbt;
    std::array<VkStridedDeviceAddressRegionKHR, 4> regions{};
    bool usesScene{};
    std::vector<uint8_t> push, uniform;
    PipelineBindings bindings;

    ~RayTracePipelineVulkan() {
        if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (sceneLayout) vkDestroyDescriptorSetLayout(device, sceneLayout, nullptr);
    }

    void create(const std::array<const Module*, 3>& modules)
    {
        const auto& caps = rayDevice();
        auto& context = *getHardwareContext().getMainDevice();
        device = context.deviceManager.getLogicalDevice();
        for (const auto* module : modules) {
            const auto& resources = module->shaderResources;
            bindings.addLayout(resources);
            uniform.resize(std::max<size_t>(uniform.size(), resources.uniformBufferSize));
            push.resize(std::max<size_t>(push.size(), resources.pushConstantSize));
            for (const auto& field : resources.bindInfoPool)
                usesScene |= field.bindType == Module::ShaderResources::accelerationStructure;
        }
        if (push.size() > caps.supportedProperties.properties.limits.maxPushConstantsSize)
            throw std::length_error("Ray tracing push constants exceed device limits");
        std::array<VkDescriptorSetLayout, 4> sets{};
        for (size_t i = 0; i < 3; ++i) sets[i] = context.resourceManager.bindlessDescriptors[i].descriptorSetLayout;
        if (usesScene || !uniform.empty()) sets[3] = sceneLayout = createUniformSnapshotLayout(device, !uniform.empty(), usesScene);
        VkPushConstantRange range{rayStages, 0, static_cast<uint32_t>(push.size())};
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = sceneLayout ? 4 : 3;
        pipelineLayout.pSetLayouts = sets.data();
        pipelineLayout.pushConstantRangeCount = !push.empty();
        pipelineLayout.pPushConstantRanges = &range;
        coronaHardwareCheck(vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &layout));

        constexpr VkShaderStageFlagBits flags[]{VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
                                               VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
        std::array<VkPipelineShaderStageCreateInfo, 3> stages{};
        std::array<VkRayTracingShaderGroupCreateInfoKHR, 3> groups{};
        try {
            for (uint32_t i = 0; i < stages.size(); ++i) {
                auto& stage = stages[i];
                stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                stage.stage = flags[i];
                stage.module = context.resourceManager.createShaderModule(std::get<std::vector<uint32_t>>(modules[i]->shaderCode));
                stage.pName = "main";
                auto& group = groups[i];
                group.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
                group.type = i == 2 ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR : VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
                group.generalShader = i == 2 ? VK_SHADER_UNUSED_KHR : i;
                group.closestHitShader = i == 2 ? i : VK_SHADER_UNUSED_KHR;
                group.anyHitShader = group.intersectionShader = VK_SHADER_UNUSED_KHR;
            }
            VkRayTracingPipelineCreateInfoKHR info{VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
            info.stageCount = stages.size();
            info.pStages = stages.data();
            info.groupCount = groups.size();
            info.pGroups = groups.data();
            info.maxPipelineRayRecursionDepth = 1;
            info.layout = layout;
            coronaHardwareCheck(caps.createRayPipelines(device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline));
        } catch (...) {
            for (const auto& stage : stages) if (stage.module) vkDestroyShaderModule(device, stage.module, nullptr);
            throw;
        }
        for (const auto& stage : stages) vkDestroyShaderModule(device, stage.module, nullptr);

        const auto& props = caps.rayTracingPipelineProperties;
        const auto handleSize = props.shaderGroupHandleSize;
        const auto stride = alignUp(handleSize, props.shaderGroupHandleAlignment);
        const auto spacing = alignUp(stride, props.shaderGroupBaseAlignment);
        std::vector<uint8_t> handles(groups.size() * handleSize);
        coronaHardwareCheck(caps.rayGroupHandles(device, pipeline, 0, groups.size(), handles.size(), handles.data()));
        const size_t bytes = props.shaderGroupBaseAlignment - 1 + spacing * (groups.size() - 1) + stride;
        sbt.allocate(bytes, VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, true);
        const auto base = address(sbt.value);
        const auto aligned = alignUp(base, props.shaderGroupBaseAlignment);
        auto* mapped = static_cast<uint8_t*>(sbt.value.bufferAllocInfo.pMappedData);
        for (size_t i = 0; i < groups.size(); ++i) {
            regions[i] = {aligned + i * spacing, stride, stride};
            std::memcpy(mapped + (regions[i].deviceAddress - base), handles.data() + i * handleSize, handleSize);
        }
        context.resourceManager.flushMapped(sbt.value, bytes);
    }

    void setPushConstantDirect(uint64_t offset, const void* data, size_t size, int32_t type)
    {
        bindings.writeValue(push, uniform, type, offset, data, size);
    }
    void setResourceDirect(uint64_t offset, uint32_t size, PipelineBinding value, int32_t type, uint32_t slot)
    {
        bindings.writeResource(push, uniform, type, offset, size, std::move(value), traceStage, slot);
    }
};
}

template<class... Stages>
struct RayTracePipeline<Stages...>::Impl {
    std::shared_ptr<RayTracePipelineVulkan> backend = std::make_shared<RayTracePipelineVulkan>();
    AccelerationStructure scene;
    // Slots in this pool share the scene descriptor and recycle only after GPU completion.
    std::shared_ptr<UniformSnapshotPool> parameters = std::make_shared<UniformSnapshotPool>();

    static Impl& checked(const std::unique_ptr<Impl>& impl)
    {
        if (!impl) throw std::logic_error("Empty ray tracing pipeline");
        return *impl;
    }
};

struct HardwareExecutor::Command::Impl::Trace : HardwareExecutor::Command::Impl, CommandRecordVulkan {
    explicit Trace(Ray::Impl& state, ktm::uvec3 size)
        : backend(state.backend), scene(state.scene), dimensions(size), bindings(backend->bindings.snapshot()),
          push(backend->push), uniform(backend->uniform), pool(state.parameters) {}

    std::shared_ptr<RayTracePipelineVulkan> backend;
    Ray::AccelerationStructure scene;
    ktm::uvec3 dimensions;
    std::shared_ptr<BindingSnapshot> bindings;
    std::vector<uint8_t> push, uniform;
    std::shared_ptr<UniformSnapshotPool> pool;
    std::shared_ptr<UniformSnapshot> parameters;
    Entry queueEntry(std::shared_ptr<HardwareExecutor::Command::Impl> owner) override { return RecordedCommand(*this, std::move(owner)); }
    ExecutorType getExecutorType() override { return ExecutorType::Compute; }
    void resolveResourceUses(ResourceAccessList& uses) override
    {
        bindings->resolveResourceUses(uses);
        if (scene.impl_) {
            uses.buffer(scene.impl_->storage.value, traceStage, asRead);
            for (const auto& child : scene.impl_->geometries) uses.buffer(child.impl_->storage.value, traceStage, asRead);
        }
        uses.buffer(backend->sbt.value, traceStage, VK_ACCESS_2_SHADER_READ_BIT);
    }
    void commitCommand(HardwareExecutorVulkan& executor) override
    {
        const auto cb = executor.currentRecordQueue->commandBuffer;
        bindings->prepareDescriptors();
        bindings->writeParameters(push, uniform);
        if (!parameters)
            parameters = pool->acquire(uniform, backend->sceneLayout, scene.impl_ ? scene.impl_->handle : VK_NULL_HANDLE);
        const auto& tables = getHardwareContext().getMainDevice()->resourceManager.bindlessDescriptors;
        std::array<VkDescriptorSet, 4> sets{};
        for (size_t i = 0; i < 3; ++i) sets[i] = bindings->sets[i] ? bindings->sets[i] : tables[i].descriptorSet;
        sets[3] = parameters ? parameters->set : VK_NULL_HANDLE;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, backend->pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, backend->layout, 0,
                                sets[3] ? 4 : 3, sets.data(), 0, nullptr);
        if (!push.empty()) vkCmdPushConstants(cb, backend->layout, rayStages, 0, static_cast<uint32_t>(push.size()), push.data());
        const auto& regions = backend->regions;
        rayDevice().traceRays(cb, &regions[0], &regions[1], &regions[2], &regions[3], dimensions.x, dimensions.y, dimensions.z);
    }
};

template<class... Stages>
RayTracePipeline<Stages...>::RayTracePipeline(const Module& raygen, const Module& miss, const Module& hit)
    : impl_(std::make_unique<Impl>())
{
    impl_->backend->create({&raygen, &miss, &hit});
}

template<class... Stages>
RayTracePipeline<Stages...>::~RayTracePipeline() = default;
template<class... Stages>
RayTracePipeline<Stages...>::RayTracePipeline(RayTracePipeline&&) noexcept = default;
template<class... Stages>
RayTracePipeline<Stages...>& RayTracePipeline<Stages...>::operator=(RayTracePipeline&&) noexcept = default;

template<class... Stages>
void RayTracePipeline<Stages...>::SceneField::operator=(const AccelerationStructure& scene)
{
    if (!scene.impl_ || scene.impl_->type != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
        throw std::invalid_argument("Ray tracing scene requires a TLAS");
    auto& state = Impl::checked(pipeline_.impl_);
    if (state.scene.impl_ != scene.impl_) {
        state.parameters = std::make_shared<UniformSnapshotPool>();
        state.scene = scene;
    }
}

template<class... Stages>
HardwareExecutor::Command RayTracePipeline<Stages...>::operator()(uint32_t width, uint32_t height, uint32_t depth)
{
    if (!width || !height || !depth) return HardwareExecutor::Command(nullptr);
    auto& state = Impl::checked(impl_);
    if (state.backend->usesScene && !state.scene.impl_) throw std::logic_error("Ray tracing scene is unbound");
    const auto& caps = rayDevice();
    const auto& limits = caps.supportedProperties.properties.limits;
    const std::array dims{width, height, depth};
    for (unsigned i = 0; i < dims.size(); ++i)
        if (dims[i] > uint64_t(limits.maxComputeWorkGroupCount[i]) * limits.maxComputeWorkGroupSize[i])
            throw std::out_of_range("Ray tracing dimension exceeds device limit");
    if (uint64_t(width) * height > caps.rayTracingPipelineProperties.maxRayDispatchInvocationCount / depth)
        throw std::out_of_range("Ray tracing invocation count exceeds device limit");
    return HardwareExecutor::Command(std::make_shared<HardwareExecutor::Command::Impl::Trace>(state, ktm::uvec3(width, height, depth)));
}

template struct RayTracePipeline<>;
template struct EmbeddedShader::BoundField<RayTracePipeline<>>;
