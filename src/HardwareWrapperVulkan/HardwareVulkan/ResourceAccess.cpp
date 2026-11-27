#include "ResourceAccess.h"
#include <algorithm>
#include <cstddef>
#include <memory_resource>

namespace {
// GENERAL is valid for both color output and shader access. An overlapping
// attachment and another concrete role still require feedback-loop support.
bool incompatibleImageUses(VkImageLayout a, VkAccessFlags2 accessA, VkImageLayout b, VkAccessFlags2 accessB)
{
    constexpr auto colorAccess = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    return a != b || bool(accessA & colorAccess) != bool(accessB & colorAccess);
}

constexpr VkAccessFlags2 writes = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
}

VkImageSubresourceRange imageViewRange(const ResourceManager::ImageHardwareWrap& image)
{
    return {image.aspectMask, image.baseMipLevel, image.mipLevels, image.baseArrayLayer, image.arrayLayers};
}

VkImageLayout imageSubresourceLayout(const ResourceManager::ImageHardwareWrap& image, uint32_t mip, uint32_t layer)
{
    if (image.allocationOwner && !image.allocationOwner->layouts.empty())
        return image.allocationOwner->layouts.at(size_t(layer) * image.allocationOwner->mipLevels + mip);
    return image.imageLayout;
}

void setImageSubresourceLayout(ResourceManager::ImageHardwareWrap& image, const VkImageSubresourceRange& range, VkImageLayout layout)
{
    image.imageLayout = layout; // Compatibility for legacy operations on this wrapper.
    if (!image.allocationOwner || image.allocationOwner->layouts.empty()) return;
    for (uint32_t layer = range.baseArrayLayer; layer < range.baseArrayLayer + range.layerCount; ++layer)
        for (uint32_t mip = range.baseMipLevel; mip < range.baseMipLevel + range.levelCount; ++mip)
            image.allocationOwner->layouts.at(size_t(layer) * image.allocationOwner->mipLevels + mip) = layout;
}

ResourceAccessList::RangeKey ResourceAccessList::key(const ResolvedResourceUse& use)
{
    return {use.image->imageHandle, use.range.aspectMask, use.range.baseMipLevel,
            use.range.levelCount, use.range.baseArrayLayer, use.range.layerCount};
}

void ResourceAccessList::clear()
{
    uses.clear();
    bufferIndices.clear();
    imageIndices.clear();
    indexed = false;
}

void ResourceAccessList::indexLargeList()
{
    if (indexed || uses.size() < 8) return;
    for (size_t i = 0; i < uses.size(); ++i)
        if (uses[i].image) imageIndices.emplace(key(uses[i]), i);
        else bufferIndices.emplace(uses[i].buffer, i);
    indexed = true;
}

void ResourceAccessList::buffer(ResourceManager::BufferHardwareWrap& value, VkPipelineStageFlags2 stages,
                                VkAccessFlags2 access)
{
    indexLargeList();
    ResolvedResourceUse* found = nullptr;
    if (indexed)
    {
        const auto entry = bufferIndices.find(value.bufferHandle);
        if (entry != bufferIndices.end()) found = &uses[entry->second];
    }
    else
        for (auto& use : uses)
            if (!use.image && use.buffer == value.bufferHandle) { found = &use; break; }
    if (found)
    {
        found->stages |= stages;
        found->access |= access;
        return;
    }
    if (indexed) bufferIndices.emplace(value.bufferHandle, uses.size());
    ResolvedResourceUse use;
    use.buffer = value.bufferHandle;
    use.hostMapped = value.bufferAllocInfo.pMappedData != nullptr;
    use.stages = stages;
    use.access = access;
    uses.push_back(use);
}

void ResourceAccessList::image(ResourceManager::ImageHardwareWrap& value, VkPipelineStageFlags2 stages,
                               VkAccessFlags2 access, VkImageLayout layout, VkImageSubresourceRange range)
{
    indexLargeList();
    const ResolvedResourceUse use{{}, &value, range, stages, access, layout};
    const auto rangeKey = key(use);
    size_t index = uses.size();
    if (indexed)
    {
        const auto found = imageIndices.find(rangeKey);
        if (found != imageIndices.end()) index = found->second;
    }
    else
        for (size_t i = 0; i < uses.size(); ++i)
            if (uses[i].image && key(uses[i]) == rangeKey) { index = i; break; }
    if (index == uses.size())
    {
        if (indexed) imageIndices.emplace(rangeKey, index);
        uses.push_back(use);
        return;
    }
    auto& previous = uses[index];
    if (incompatibleImageUses(previous.layout, previous.access, layout, access))
        throw std::invalid_argument("An invocation uses one image range with incompatible layouts");
    previous.stages |= stages;
    previous.access |= access;
}

void ResourceBarrierPlanner::recordHostReadBarrier(VkCommandBuffer commandBuffer) const
{
    if (!mappedWriteStages) return;
    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = mappedWriteStages;
    barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.memoryBarrierCount = 1;
    dependency.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

bool ResourceBarrierPlanner::dependency(AccessState& state, VkPipelineStageFlags2 stages, VkAccessFlags2 access,
                                       VkPipelineStageFlags2& sourceStages, VkAccessFlags2& sourceAccess)
{
    if (access & writes)
    {
        sourceStages = state.writerStages | state.readerStages;
        sourceAccess = state.writerAccess | state.readerAccess;
        state = {stages, 0, 0, access, 0};
        return sourceStages != 0;
    }
    const bool visible = (state.visibleReadStages & VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) ||
                         (stages & ~state.visibleReadStages) == 0;
    sourceStages = state.writerStages;
    sourceAccess = state.writerAccess;
    const bool needed = sourceStages && !visible;
    if (needed) state.visibleReadStages |= stages;
    state.readerStages |= stages;
    state.readerAccess |= access;
    return needed;
}

void ResourceBarrierPlanner::plan(const ResourceAccessList& accesses, ResourceBarriers& result, bool finalCommand)
{
    result.clear();
    if (accesses.uses.empty()) return;
    // The first resource access also orders earlier submissions on this queue.
    const bool firstAccess = boundary;
    const bool isolated = firstAccess && finalCommand;
    if (firstAccess)
    {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        if (finalCommand)
        {
            barrier.dstStageMask = 0;
            for (const auto& use : accesses.uses) barrier.dstStageMask |= use.stages;
        }
        result.memoryBarriers.push_back(barrier);
        boundary = false;
    }
    // Merge overlapping view aliases before querying history. A single command
    // cannot require two different layouts for the same image subresource.
    struct ImageUse {
        ResourceManager::ImageHardwareWrap* image;
        VkPipelineStageFlags2 stages;
        VkAccessFlags2 access;
        VkImageLayout layout;
    };
    // The per-command merge usually covers just a few attachments. Larger
    // sets spill to the heap and all nodes expire together after this plan.
    alignas(std::max_align_t) std::byte storage[1024];
    std::pmr::monotonic_buffer_resource scratch(storage, sizeof(storage), std::pmr::new_delete_resource());
    std::pmr::map<ImageKey, ImageUse> imageUses(&scratch);
    for (const auto& use : accesses.uses)
    {
        if (!use.image)
        {
            const auto stages = use.stages;
            const auto access = use.access;
            if (use.hostMapped && (access & writes)) mappedWriteStages |= stages;
            if (isolated) continue; // The boundary barrier covers all prior accesses.
            VkPipelineStageFlags2 sourceStages{};
            VkAccessFlags2 sourceAccess{};
            if (dependency(buffers[use.buffer], stages, access, sourceStages, sourceAccess))
            {
                VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
                barrier.srcStageMask = sourceStages;
                barrier.srcAccessMask = sourceAccess;
                barrier.dstStageMask = stages;
                barrier.dstAccessMask = access | ((access & writes) ? 0 : VK_ACCESS_2_MEMORY_READ_BIT);
                barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.buffer = use.buffer;
                barrier.size = VK_WHOLE_SIZE;
                result.bufferBarriers.push_back(barrier);
            }
            continue;
        }
        // The submission boundary already covers an isolated upload or blit.
        // Larger first reads (such as cubemaps) can also share one history entry.
        const auto access = use.access;
        if (firstAccess && (isolated || !(access & writes)) &&
            ((isolated && accesses.uses.size() <= 2) || use.range.levelCount > 1 || use.range.layerCount > 1) &&
            std::none_of(accesses.uses.begin(), accesses.uses.end(), [&](const auto& other) {
                return &other != &use && other.image && other.image->imageHandle == use.image->imageHandle;
            }))
        {
            const auto layout = use.layout;
            bool unchanged = true;
            for (uint32_t layer = use.range.baseArrayLayer; unchanged && layer < use.range.baseArrayLayer + use.range.layerCount; ++layer)
                for (uint32_t mip = use.range.baseMipLevel; unchanged && mip < use.range.baseMipLevel + use.range.levelCount; ++mip)
                    unchanged = imageSubresourceLayout(*use.image, mip, layer) == layout;
            if (unchanged)
            {
                if (!isolated) {
                    if (!initialImageReads) initialImageReads.emplace();
                    auto& read = (*initialImageReads)[use.image->imageHandle];
                    read.range = use.range;
                    read.state.readerStages = use.stages;
                    read.state.readerAccess = access;
                }
                use.image->imageLayout = layout;
                continue;
            }
        }
        for (uint32_t layer = use.range.baseArrayLayer; layer < use.range.baseArrayLayer + use.range.layerCount; ++layer)
            for (uint32_t mip = use.range.baseMipLevel; mip < use.range.baseMipLevel + use.range.levelCount; ++mip)
            {
                ImageKey key{use.image->imageHandle, use.range.aspectMask, mip, layer};
                auto [entry, inserted] = imageUses.try_emplace(key, ImageUse{use.image, use.stages, use.access, use.layout});
                if (!inserted)
                {
                    if (incompatibleImageUses(entry->second.layout, entry->second.access, use.layout, use.access))
                        throw std::invalid_argument("Overlapping image views require incompatible invocation layouts");
                    entry->second.stages |= use.stages;
                    entry->second.access |= use.access;
                }
            }
    }
    for (auto& [key, use] : imageUses)
    {
        const auto [handle, aspect, mip, layer] = key;
        VkPipelineStageFlags2 sourceStages{};
        VkAccessFlags2 sourceAccess{};
        auto [history, inserted] = images.try_emplace(key);
        if (inserted && initialImageReads) {
            const auto initial = initialImageReads->find(handle);
            if (initial != initialImageReads->end()) {
                const auto& range = initial->second.range;
                if ((range.aspectMask & aspect) && mip - range.baseMipLevel < range.levelCount &&
                    layer - range.baseArrayLayer < range.layerCount)
                    history->second = initial->second.state;
            }
        }
        const bool hazard = dependency(history->second, use.stages, use.access, sourceStages, sourceAccess);
        const auto oldLayout = imageSubresourceLayout(*use.image, mip, layer);
        if (hazard || oldLayout != use.layout)
        {
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = oldLayout != use.layout ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : sourceStages;
            barrier.srcAccessMask = oldLayout != use.layout ? VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT : sourceAccess;
            barrier.dstStageMask = use.stages;
            barrier.dstAccessMask = use.access | ((use.access & writes) ? 0 : VK_ACCESS_2_MEMORY_READ_BIT);
            if (oldLayout != use.layout)
            {
                // Layout transitions are writes themselves. Cover every later
                // stage, including a different read stage with no data hazard.
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
            }
            barrier.oldLayout = oldLayout;
            barrier.newLayout = use.layout;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = handle;
            barrier.subresourceRange = {aspect, mip, 1, layer, 1};
            result.imageBarriers.push_back(barrier);
        }
        setImageSubresourceLayout(*use.image, {aspect, mip, 1, layer, 1}, use.layout);
    }
}

void ResourceLayoutJournal::capture(const ResourceAccessList& accesses)
{
    for (const auto& use : accesses.uses) if (use.image) {
        wrappers.try_emplace(use.image, use.image->imageLayout);
        if (auto* owner = use.image->allocationOwner.get(); owner && !allocations.contains(owner))
            allocations.emplace(owner, owner->layouts);
    }
}
ResourceLayoutJournal::~ResourceLayoutJournal()
{
    if (committed) return;
    for (auto& [owner, layouts] : allocations) owner->layouts.swap(layouts);
    for (const auto& [image, layout] : wrappers) image->imageLayout = layout;
}
