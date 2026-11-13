#include "ResourceAccess.h"
#include <algorithm>

namespace {
constexpr VkAccessFlags2 shaderAccess = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
VkAccessFlags2 applyDeclaration(VkAccessFlags2 access, VkAccessFlags2 declared)
{
    return declared ? (access & ~shaderAccess) | declared : access;
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
    conservative = false;
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

ResolvedResourceUse& ResourceAccessList::buffer(ResourceManager::BufferHardwareWrap& value, VkPipelineStageFlags2 stages,
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
        return *found;
    }
    if (indexed) bufferIndices.emplace(value.bufferHandle, uses.size());
    ResolvedResourceUse use;
    use.buffer = value.bufferHandle;
    use.stages = stages;
    use.access = access;
    uses.push_back(use);
    return uses.back();
}

ResolvedResourceUse& ResourceAccessList::image(ResourceManager::ImageHardwareWrap& value, VkPipelineStageFlags2 stages,
                                              VkAccessFlags2 access, VkImageLayout layout, VkImageSubresourceRange range)
{
    indexLargeList();
    const ResolvedResourceUse use{{}, &value, range};
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
    }
    auto& previous = uses[index];
    if (layout != VK_IMAGE_LAYOUT_UNDEFINED)
    {
        if (previous.layout != VK_IMAGE_LAYOUT_UNDEFINED && previous.layout != layout)
            throw std::invalid_argument("An invocation uses one image range with incompatible layouts");
        previous.image = &value;
        previous.layout = layout;
    }
    previous.stages |= stages;
    previous.access |= access;
    return previous;
}

void ResourceBarrierPlanner::invalidate()
{
    buffers.clear(); images.clear(); boundary = true;
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

void ResourceBarrierPlanner::plan(const ResourceAccessList& accesses, ResourceBarriers& result)
{
    result.clear();
    if (accesses.uses.empty() && !accesses.conservative) return;
    if (boundary || accesses.conservative)
    {
        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        barrier.srcAccessMask = barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        result.memoryBarriers.push_back(barrier);
        buffers.clear(); images.clear();
        boundary = false;
    }
    // Merge overlapping view aliases before querying history. A single command
    // cannot require two different layouts for the same image subresource.
    struct ImageUse {
        ResourceManager::ImageHardwareWrap* image;
        VkPipelineStageFlags2 stages;
        VkAccessFlags2 access;
        VkImageLayout layout;
        VkPipelineStageFlags2 declaredStages;
        VkAccessFlags2 declaredAccess;
    };
    std::map<ImageKey, ImageUse> imageUses;
    for (const auto& use : accesses.uses)
    {
        if (!use.image)
        {
            const auto stages = use.stages | use.declaredStages;
            const auto access = applyDeclaration(use.access, use.declaredAccess);
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
        for (uint32_t layer = use.range.baseArrayLayer; layer < use.range.baseArrayLayer + use.range.layerCount; ++layer)
            for (uint32_t mip = use.range.baseMipLevel; mip < use.range.baseMipLevel + use.range.levelCount; ++mip)
            {
                ImageKey key{use.image->imageHandle, use.range.aspectMask, mip, layer};
                auto [entry, inserted] = imageUses.try_emplace(key, ImageUse{use.image, use.stages, use.access, use.layout,
                                                                           use.declaredStages, use.declaredAccess});
                if (!inserted)
                {
                    if (entry->second.layout != VK_IMAGE_LAYOUT_UNDEFINED && use.layout != VK_IMAGE_LAYOUT_UNDEFINED &&
                        entry->second.layout != use.layout)
                        throw std::invalid_argument("Overlapping image views require incompatible invocation layouts");
                    if (use.layout != VK_IMAGE_LAYOUT_UNDEFINED)
                    {
                        entry->second.image = use.image;
                        entry->second.layout = use.layout;
                    }
                    entry->second.stages |= use.stages;
                    entry->second.access |= use.access;
                    entry->second.declaredStages |= use.declaredStages;
                    entry->second.declaredAccess |= use.declaredAccess;
                }
            }
    }
    for (auto& [key, use] : imageUses)
    {
        // Actual roles determine the layout. Declarations refine shader access
        // only on overlapping subresources; unknown portions remain conservative.
        if (use.layout == VK_IMAGE_LAYOUT_UNDEFINED)
        {
            use.layout = VK_IMAGE_LAYOUT_GENERAL;
            use.stages = use.declaredStages;
            use.access = use.declaredAccess;
        }
        else if (use.access & shaderAccess)
            use.access = applyDeclaration(use.access, use.declaredAccess);
        const auto [handle, aspect, mip, layer] = key;
        VkPipelineStageFlags2 sourceStages{};
        VkAccessFlags2 sourceAccess{};
        const bool hazard = dependency(images[key], use.stages, use.access, sourceStages, sourceAccess);
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
    if (accesses.conservative) invalidate();
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
