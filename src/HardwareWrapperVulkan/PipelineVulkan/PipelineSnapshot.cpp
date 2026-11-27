#include "PipelineSnapshot.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

bool sameResource(const PipelineResource& a, const PipelineResource& b)
{
    if (a.index() != b.index()) return false;
    if (const auto* buffer = std::get_if<HardwareBuffer>(&a))
        return ResourceManager::get(*buffer) == ResourceManager::get(std::get<HardwareBuffer>(b));
    return ResourceManager::get(std::get<HardwareImage>(a)) == ResourceManager::get(std::get<HardwareImage>(b));
}

void PipelineBindings::erase(uint64_t offset, size_t size, bool uniform)
{
    const auto count = std::erase_if(values, [&](const PipelineBinding& binding) {
        return binding.fixedSlot == UINT32_MAX && binding.uniform == uniform &&
               binding.offset < offset + size && offset < binding.offset + binding.size;
    });
    if (count) cached.reset();
}

namespace {
using BindType = EmbeddedShader::ShaderCodeModule::ShaderResources::BindType;

inline bool writeBytes(std::span<uint8_t> target, uint64_t offset, const void* data, size_t size)
{
    if (!target.data() || !data || !size || offset > target.size() || size > target.size() - offset)
        return false;
    std::memcpy(target.data() + offset, data, size);
    return true;
}

int32_t descriptorIndex(const PipelineBinding& binding)
{
    return std::visit([&](const auto& value) -> int32_t {
        if (!value) throw std::invalid_argument("A pipeline binding has no resource");
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, HardwareBuffer>)
            return ResourceManager::get(value)->bindlessIndex;
        else {
            const auto* image = ResourceManager::get(value);
            return binding.table == 0 && (image->imageUsage & VK_IMAGE_USAGE_STORAGE_BIT)
                       ? image->sampledIndex : image->bindlessIndex;
        }
    }, binding.resource);
}
}

void PipelineBindings::set(PipelineBinding next)
{
    for (const auto& binding : values)
        if (binding.offset == next.offset && binding.size == next.size && binding.uniform == next.uniform &&
            binding.fixedSlot == next.fixedSlot && binding.table == next.table &&
            binding.access == next.access && binding.stages == next.stages &&
            sameResource(binding.resource, next.resource)) return;
    if (next.fixedSlot == UINT32_MAX)
        erase(next.offset, next.size, next.uniform);
    else
        std::erase_if(values, [&](const PipelineBinding& binding) {
            return binding.fixedSlot == next.fixedSlot && binding.table == next.table;
        });
    values.push_back(std::move(next));
    cached.reset();
}

void PipelineBindings::writeValue(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, int32_t bindType,
                                  uint64_t offset, const void* data, size_t size)
{
    const bool uniform = bindType == BindType::uniformBufferMembers;
    if (!uniform && bindType != BindType::pushConstantMembers)
        throw std::invalid_argument("A descriptor field requires a resource object");
    if (!writeBytes(uniform ? uniformBytes : pushConstants, offset, data, size))
        throw std::invalid_argument("Shader parameter write exceeds its parameter block");
    erase(offset, size, uniform);
}

void PipelineBindings::writeResource(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, int32_t bindType,
                                     uint64_t offset, uint32_t typeSize, const HardwareDescriptor& descriptor,
                                     VkPipelineStageFlags2 stages, std::optional<uint32_t> fixedSlot)
{
    using Access = EmbeddedShader::Ast::AccessPermissions;
    if (!std::visit([](const auto& handle) { return bool(handle); }, descriptor.resource_))
        throw std::invalid_argument("Cannot bind a descriptor without a resource");
    PipelineBinding value;
    value.resource = descriptor.resource_;
    value.table = std::holds_alternative<HardwareBuffer>(value.resource) ? 1u : descriptor.sampled_ ? 0u : 2u;
    value.stages = stages;
    if ((descriptor.access_ & Access::ReadOnly) != Access::None) value.access |= VK_ACCESS_2_SHADER_READ_BIT;
    if ((descriptor.access_ & Access::WriteOnly) != Access::None) value.access |= VK_ACCESS_2_SHADER_WRITE_BIT;
    if (!value.access || !stages) throw std::invalid_argument("Shader descriptor has no access or stage");

    if (bindType == BindType::uniformBufferMembers || bindType == BindType::pushConstantMembers) {
        const uint32_t marker[] = {pendingDescriptorIndex, 0};
        if (typeSize != sizeof(uint32_t) && typeSize != sizeof(marker))
            throw std::invalid_argument("A shader descriptor index must occupy four or eight bytes");
        value.uniform = bindType == BindType::uniformBufferMembers;
        value.offset = offset;
        value.size = typeSize;
        if (!writeBytes(value.uniform ? uniformBytes : pushConstants, offset, marker, typeSize))
            throw std::invalid_argument("Shader descriptor exceeds its parameter block");
    } else {
        uint32_t expectedTable;
        switch (bindType) {
        case BindType::storageBuffer: case BindType::rawBuffer: expectedTable = 1; break;
        case BindType::storageTexture: expectedTable = 2; break;
        case BindType::sampledImages: case BindType::texture: case BindType::sampler: expectedTable = 0; break;
        default: throw std::invalid_argument("Shader field cannot bind a descriptor");
        }
        if (value.table != expectedTable || !fixedSlot)
            throw std::invalid_argument("Descriptor does not match the shader field");
        value.fixedSlot = *fixedSlot;
    }
    set(std::move(value));
}

void PipelineBindings::bindAuto(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes,
                                const EmbeddedShader::AutoBindEntry& entry)
{
    if (entry.boundValueRef) {
        writeValue(pushConstants, uniformBytes, entry.bindType, entry.byteOffset, entry.boundValueRef, entry.boundValueSize);
        return;
    }
    if (!entry.boundResourceRef) return;
    void* resource = *entry.boundResourceRef;
    if (!resource) throw std::invalid_argument("An active EDSL shader resource has no binding");
    auto handle = entry.boundResourceIsBuffer ? PipelineResource(*static_cast<HardwareBuffer*>(resource))
                                              : PipelineResource(*static_cast<HardwareImage*>(resource));
    VkPipelineStageFlags2 stages{};
    if (entry.stages & 1) stages |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    if (entry.stages & 2) stages |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    if (entry.stages & 4) stages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    writeResource(pushConstants, uniformBytes, entry.bindType, entry.byteOffset, entry.typeSize,
                  HardwareDescriptor(std::move(handle), entry.access, entry.sampled), stages);
}

void PipelineBindings::addLayout(const EmbeddedShader::ShaderCodeModule::ShaderResources& resources)
{
    for (const auto& info : resources.bindInfoPool) {
        switch (info.bindType) {
        case BindType::sampledImages: case BindType::texture: case BindType::sampler:
        case BindType::storageBuffer: case BindType::rawBuffer: case BindType::storageTexture: break;
        default: continue;
        }
        if (!info.binding && info.set < tableLimits.size() && info.elementCount &&
            info.elementCount < tableLimits[info.set])
            tableLimits[info.set] = static_cast<uint32_t>(info.elementCount);
    }
}

std::shared_ptr<BindingSnapshot> PipelineBindings::snapshot()
{
    if (!cached) {
        auto next = std::make_shared<BindingSnapshot>();
        next->values = values;
        next->tableLimits = tableLimits;
        cached = std::move(next);
    }
    return cached;
}

BindingSnapshot::~BindingSnapshot()
{
    for (auto pool : pools) if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
}

void BindingSnapshot::prepareDescriptors()
{
    if (prepared) return;
    // An automatic slot belongs to its resource wrapper until retirement;
    // fixed bindings cannot overwrite it. Reuse it across binding snapshots.
    auto& manager = getHardwareContext().getMainDevice()->resourceManager;
    for (auto& binding : values) {
        if (descriptorIndex(binding) >= 0) continue;
        if (auto buffer = std::get_if<HardwareBuffer>(&binding.resource))
            (void)manager.storeDescriptor(*ResourceManager::get(*buffer));
        else {
            auto& image = *ResourceManager::get(std::get<HardwareImage>(binding.resource));
            if (binding.table == 0) manager.storeSampledDescriptor(image);
            else (void)manager.storeDescriptor(image);
        }
    }
    device = getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice();
    constexpr VkDescriptorType types[] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    for (uint32_t tableIndex = 0; tableIndex < 3; ++tableIndex)
    {
        if (sets[tableIndex]) continue;
        const bool used = std::any_of(values.begin(), values.end(), [&](const auto& binding) {
            return binding.table == tableIndex && (binding.fixedSlot != UINT32_MAX || tableLimits[tableIndex] != UINT32_MAX);
        });
        if (!used) continue;
        const auto& table = manager.bindlessDescriptors[tableIndex];
        // Private tables hold only descriptors dereferenced by this invocation.
        // Reserve fixed slots first so a managed parameter can never alias a different resource.
        const uint32_t managedLimit = std::min(table.capacity, tableLimits[tableIndex]);
        std::vector<int32_t> slots(managedLimit, -1);
        for (const auto& binding : values) {
            if (binding.table != tableIndex || binding.fixedSlot == UINT32_MAX) continue;
            if (binding.fixedSlot >= table.capacity) throw std::out_of_range("Fixed descriptor slot exceeds table capacity");
            // A named field may refer to a longer alias of this table in another shader stage.
            if (binding.fixedSlot >= slots.size()) slots.resize(binding.fixedSlot + 1, -1);
            slots[binding.fixedSlot] = descriptorIndex(binding);
        }
        auto managed = std::span(slots).first(managedLimit);
        for (auto& binding : values) {
            if (binding.table != tableIndex || binding.fixedSlot != UINT32_MAX) continue;
            const int32_t index = descriptorIndex(binding);
            auto position = std::ranges::find(managed, index);
            if (position == managed.end()) position = std::ranges::find(managed, -1);
            if (position == managed.end()) throw std::length_error("Private descriptor table is full");
            *position = index;
            binding.privateSlot = static_cast<uint32_t>(position - managed.begin());
        }
        const auto capacity = static_cast<uint32_t>(slots.size());
        VkDescriptorPoolSize size{types[tableIndex], capacity};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        VkDescriptorPool pool{};
        coronaHardwareCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
        VkDescriptorSetVariableDescriptorCountAllocateInfo count{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
        count.descriptorSetCount = 1;
        count.pDescriptorCounts = &capacity;
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.pNext = &count;
        allocate.descriptorPool = pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &table.descriptorSetLayout;
        VkDescriptorSet set{};
        const VkResult result = vkAllocateDescriptorSets(device, &allocate, &set);
        if (result != VK_SUCCESS) {
            vkDestroyDescriptorPool(device, pool, nullptr);
            coronaHardwareCheck(result);
        }
        pools[tableIndex] = pool;
        sets[tableIndex] = set;
        for (uint32_t destination = 0; destination < slots.size(); ++destination)
        {
            if (slots[destination] < 0) continue;
            VkCopyDescriptorSet slot{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
            slot.srcSet = table.descriptorSet;
            slot.srcArrayElement = static_cast<uint32_t>(slots[destination]);
            slot.dstSet = sets[tableIndex];
            slot.dstArrayElement = destination;
            slot.descriptorCount = 1;
            vkUpdateDescriptorSets(device, 0, nullptr, 1, &slot);
        }
    }
    prepared = true;
}

void BindingSnapshot::writeParameters(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes) const
{
    for (const auto& binding : values)
    {
        if (binding.fixedSlot != UINT32_MAX) continue;
        const auto target = binding.uniform ? uniformBytes : pushConstants;
        if (binding.offset > target.size() || binding.size > target.size() - binding.offset) continue;
        uint32_t stored{};
        std::memcpy(&stored, target.data() + binding.offset, sizeof(stored));
        if (stored != pendingDescriptorIndex) continue;
        const int32_t index = binding.privateSlot == UINT32_MAX ? descriptorIndex(binding) : static_cast<int32_t>(binding.privateSlot);
        if (index < 0) throw std::runtime_error("Pipeline descriptors have not been prepared");
        std::memcpy(target.data() + binding.offset, &index, sizeof(index));
    }
}

std::vector<uint8_t> copyParameterBytes(const uint8_t* data, size_t size)
{
    return data && size ? std::vector<uint8_t>(data, data + size) : std::vector<uint8_t>{};
}

VkDescriptorSetLayout createUniformSnapshotLayout(VkDevice device)
{
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_ALL;
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = 1;
    info.pBindings = &binding;
    VkDescriptorSetLayout layout{};
    coronaHardwareCheck(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));
    return layout;
}

UniformSnapshot::~UniformSnapshot()
{
    if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
}

void UniformSnapshot::prepare(std::span<const uint8_t> bytes, VkDescriptorSetLayout layout)
{
    if (bytes.empty()) return;
    if (!buffer) buffer = HardwareBuffer(static_cast<uint32_t>(bytes.size()), HardwareBuffer::Usage::UniformBuffer);
    if (uploadedBytes.size() != bytes.size() || !std::equal(bytes.begin(), bytes.end(), uploadedBytes.begin()))
    {
        if (!buffer.copyFromData(bytes.data(), bytes.size()))
            throw std::runtime_error("Cannot upload invocation uniform data");
        uploadedBytes.assign(bytes.begin(), bytes.end());
    }
    if (set) return;
    device = getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice();
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &size;
    coronaHardwareCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
    VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &layout;
    coronaHardwareCheck(vkAllocateDescriptorSets(device, &allocation, &set));
    VkDescriptorBufferInfo info{ResourceManager::get(buffer)->bufferHandle, 0, bytes.size()};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

std::shared_ptr<UniformSnapshot> UniformSnapshotPool::acquire(std::span<const uint8_t> bytes, VkDescriptorSetLayout layout)
{
    if (bytes.empty()) return {};
    if (!slots.empty()) {
        // Immutable bytes and descriptor sets can serve multiple in-flight invocations.
        const auto& latest = slots[next ? next - 1 : slots.size() - 1];
        if (std::ranges::equal(bytes, latest->uploadedBytes)) return latest;
    }
    for (size_t visited = 0; visited < slots.size(); ++visited)
    {
        const size_t index = (next + visited) % slots.size();
        if (slots[index].use_count() == 1)
        {
            slots[index]->prepare(bytes, layout);
            next = (index + 1) % slots.size();
            return slots[index];
        }
    }
    auto slot = std::make_shared<UniformSnapshot>();
    slot->prepare(bytes, layout);
    slots.push_back(slot);
    next = 0;
    return slot;
}

void BindingSnapshot::resolveResourceUses(ResourceAccessList& result) const
{
    for (const auto& binding : values) {
        if (!binding.access) continue;
        if (const auto* buffer = std::get_if<HardwareBuffer>(&binding.resource))
            result.buffer(*ResourceManager::get(*buffer), binding.stages, binding.access);
        else {
            auto* image = ResourceManager::get(std::get<HardwareImage>(binding.resource));
            result.image(*image, binding.stages, binding.access, VK_IMAGE_LAYOUT_GENERAL, imageViewRange(*image));
        }
    }
}
