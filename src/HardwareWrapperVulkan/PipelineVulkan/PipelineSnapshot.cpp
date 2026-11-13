#include "PipelineSnapshot.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

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

uintptr_t resourceId(const PipelineResource& resource)
{
    return std::visit([](const auto& value) -> uintptr_t {
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, HardwareBuffer>)
            return value.getBufferID();
        else return value.getImageID();
    }, resource);
}

uint32_t descriptorTable(const PipelineBinding& binding)
{
    if (binding.sampled) return 0;
    const auto& resource = binding.resource;
    if (std::holds_alternative<HardwareBuffer>(resource)) return 1;
    return (legacyImageFromId(std::get<HardwareImage>(resource).getImageID())->imageUsage & VK_IMAGE_USAGE_STORAGE_BIT) ? 2 : 0;
}

int32_t descriptorIndex(const PipelineBinding& binding)
{
    return std::visit([&](const auto& value) -> int32_t {
        if (!value) throw std::invalid_argument("A pipeline binding has no resource");
        if constexpr (std::is_same_v<std::decay_t<decltype(value)>, HardwareBuffer>)
            return legacyBufferFromId(value.getBufferID())->bindlessIndex;
        else {
            const auto* image = legacyImageFromId(value.getImageID());
            return binding.sampled && (image->imageUsage & VK_IMAGE_USAGE_STORAGE_BIT)
                       ? image->sampledIndex : image->bindlessIndex;
        }
    }, binding.resource);
}
}

void PipelineBindings::set(PipelineBinding next)
{
    for (const auto& binding : values)
        if (binding.offset == next.offset && binding.size == next.size && binding.uniform == next.uniform &&
            binding.fixedSlot == next.fixedSlot && binding.sampled == next.sampled &&
            binding.resource.index() == next.resource.index() && resourceId(binding.resource) == resourceId(next.resource)) return;
    if (next.fixedSlot == UINT32_MAX)
        erase(next.offset, next.size, next.uniform);
    else
        std::erase_if(values, [&](const PipelineBinding& binding) {
            return binding.fixedSlot == next.fixedSlot && descriptorTable(binding) == descriptorTable(next);
        });
    values.push_back(std::move(next));
    cached.reset();
}

void PipelineBindings::writeValue(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, int32_t bindType,
                                  uint64_t offset, const void* data, size_t size)
{
    const bool uniform = bindType == BindType::uniformBufferMembers;
    if ((uniform || bindType == BindType::pushConstantMembers) &&
        writeBytes(uniform ? uniformBytes : pushConstants, offset, data, size))
        erase(offset, size, uniform);
}

void PipelineBindings::writeResource(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, int32_t bindType,
                                     uint64_t offset, uint32_t typeSize, PipelineResource resource,
                                     std::optional<uint32_t> fixedSlot)
{
    const uint32_t handle[] = {pendingDescriptorIndex, 0};
    const bool sampled = bindType == BindType::sampledImages;
    const uint32_t size = typeSize == 0 || typeSize >= sizeof(handle) ? sizeof(handle) : sizeof(handle[0]);
    const auto write = [&](std::span<uint8_t> target, bool uniform) {
        if (!writeBytes(target, offset, handle, size)) return false;
        set({offset, size, uniform, std::move(resource), UINT32_MAX, sampled});
        return true;
    };

    switch (bindType)
    {
    case BindType::pushConstantMembers:  write(pushConstants, false); return;
    case BindType::uniformBufferMembers: write(uniformBytes, true); return;
    case BindType::rawBuffer:
    case BindType::storageBuffer:
        if (!std::holds_alternative<HardwareBuffer>(resource)) return;
        break;
    case BindType::sampledImages:
    case BindType::texture:
    case BindType::sampler:
    case BindType::storageTexture:
        if (!std::holds_alternative<HardwareImage>(resource)) return;
        break;
    default: return;
    }

    if (write(pushConstants, false) || write(uniformBytes, true)) return;
    if (fixedSlot) set({offset, size, false, std::move(resource), *fixedSlot, sampled});
}

std::shared_ptr<BindingSnapshot> PipelineBindings::snapshot()
{
    if (!cached)
    {
        cached = std::make_shared<BindingSnapshot>();
        cached->values = values;
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
    for (auto& binding : values)
        if (descriptorIndex(binding) < 0) {
            if (binding.sampled) (void)std::get<HardwareImage>(binding.resource).storeSampledDescriptor();
            else std::visit([](auto& value) { (void)value.storeDescriptor(); }, binding.resource);
        }
    auto& manager = getHardwareContext().getMainDevice()->resourceManager;
    device = getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice();
    constexpr VkDescriptorType types[] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    for (uint32_t tableIndex = 0; tableIndex < 3; ++tableIndex)
    {
        if (sets[tableIndex]) continue;
        const bool used = std::any_of(values.begin(), values.end(), [&](const auto& binding) {
            return binding.fixedSlot != UINT32_MAX && descriptorTable(binding) == tableIndex;
        });
        if (!used) continue;
        const auto& table = manager.bindlessDescriptors[tableIndex];
        for (const auto& binding : values)
            if (binding.fixedSlot != UINT32_MAX && descriptorTable(binding) == tableIndex && binding.fixedSlot >= table.capacity)
                throw std::out_of_range("Fixed descriptor slot exceeds table capacity");
        VkDescriptorPoolSize size{types[tableIndex], table.capacity};
        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &size;
        coronaHardwareCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pools[tableIndex]));
        VkDescriptorSetVariableDescriptorCountAllocateInfo count{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO};
        count.descriptorSetCount = 1;
        count.pDescriptorCounts = &table.capacity;
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.pNext = &count;
        allocate.descriptorPool = pools[tableIndex];
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &table.descriptorSetLayout;
        coronaHardwareCheck(vkAllocateDescriptorSets(device, &allocate, &sets[tableIndex]));
        VkCopyDescriptorSet copy{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
        copy.srcSet = table.descriptorSet;
        copy.dstSet = sets[tableIndex];
        copy.descriptorCount = table.capacity;
        vkUpdateDescriptorSets(device, 0, nullptr, 1, &copy);
        for (const auto& binding : values)
        {
            if (binding.fixedSlot == UINT32_MAX || descriptorTable(binding) != tableIndex) continue;
            if (binding.fixedSlot >= table.capacity) throw std::out_of_range("Fixed descriptor slot exceeds table capacity");
            const int32_t index = descriptorIndex(binding);
            if (index < 0) throw std::runtime_error("Fixed binding has no resource descriptor");
            VkCopyDescriptorSet slot{VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET};
            slot.srcSet = table.descriptorSet;
            slot.srcArrayElement = static_cast<uint32_t>(index);
            slot.dstSet = sets[tableIndex];
            slot.dstArrayElement = binding.fixedSlot;
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
        const int32_t index = descriptorIndex(binding);
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
    if (!buffer) buffer = HardwareBuffer(static_cast<uint32_t>(bytes.size()), BufferUsage::UniformBuffer);
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
    VkDescriptorBufferInfo info{legacyBufferFromId(buffer.getBufferID())->bufferHandle, 0, bytes.size()};
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

namespace {
VkAccessFlags2 shaderAccess(ResourceAccess access)
{
    return (access != ResourceAccess::Write ? VK_ACCESS_2_SHADER_READ_BIT : 0) |
           (access != ResourceAccess::Read ? VK_ACCESS_2_SHADER_WRITE_BIT : 0);
}

bool addShaderUse(ResourceAccessList& result, const PipelineResource& resource,
                  VkPipelineStageFlags2 stages, VkAccessFlags2 access, bool declaration = false)
{
    ResolvedResourceUse* use;
    if (const auto* buffer = std::get_if<HardwareBuffer>(&resource))
    {
        if (!*buffer) return false;
        use = &result.buffer(*legacyBufferFromId(buffer->getBufferID()), declaration ? 0 : stages, declaration ? 0 : access);
    }
    else if (const auto& image = std::get<HardwareImage>(resource); image)
    {
        auto* native = legacyImageFromId(image.getImageID());
        if (!(native->imageUsage & VK_IMAGE_USAGE_STORAGE_BIT)) access = VK_ACCESS_2_SHADER_READ_BIT;
        use = &result.image(*native, declaration ? 0 : stages, declaration ? 0 : access,
                            declaration ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL, imageViewRange(*native));
    }
    else return false;
    if (declaration)
    {
        use->declaredStages |= stages;
        use->declaredAccess |= access;
    }
    return true;
}
}

void resolveShaderBindings(ResourceAccessList& result, std::span<const PipelineBinding> bindings,
                            VkPipelineStageFlags2 stages)
{
    for (const auto& binding : bindings)
        addShaderUse(result, binding.resource, stages, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
}

void resolveDeclaredUses(ResourceAccessList& result, std::span<const ResourceUse> declarations,
                         VkPipelineStageFlags2 stages)
{
    result.conservative = declarations.empty();
    for (const auto& declared : declarations)
        if (!addShaderUse(result, declared.resource, stages, shaderAccess(declared.access), true))
            result.conservative = true;
}
