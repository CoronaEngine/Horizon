#pragma once

#include "ResourceManager.h"
#include <map>
#include <tuple>

// Whole-buffer tracking is deliberately conservative. Images are tracked by
// allocation, aspect, mip and layer, so different wrappers cannot hide aliases.
struct ResolvedResourceUse
{
    VkBuffer buffer{};
    ResourceManager::ImageHardwareWrap* image{};
    VkImageSubresourceRange range{};
    VkPipelineStageFlags2 stages{};
    VkAccessFlags2 access{};
    VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
    // Kept separate until overlapping image ranges have been merged.
    VkPipelineStageFlags2 declaredStages{};
    VkAccessFlags2 declaredAccess{};
};

struct ResourceAccessList
{
    bool conservative{false};
    std::vector<ResolvedResourceUse> uses;
    void clear();
    ResolvedResourceUse& buffer(ResourceManager::BufferHardwareWrap& value, VkPipelineStageFlags2 stages, VkAccessFlags2 access);
    ResolvedResourceUse& image(ResourceManager::ImageHardwareWrap& value, VkPipelineStageFlags2 stages, VkAccessFlags2 access,
                               VkImageLayout layout, VkImageSubresourceRange range);
private:
    std::unordered_map<VkBuffer, size_t> bufferIndices;
    using RangeKey = std::tuple<VkImage, VkImageAspectFlags, uint32_t, uint32_t, uint32_t, uint32_t>;
    std::map<RangeKey, size_t> imageIndices;
    bool indexed{false};
    void indexLargeList();
    static RangeKey key(const ResolvedResourceUse& use);
};

struct ResourceBarriers
{
    std::vector<VkMemoryBarrier2> memoryBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    void clear()
    {
        memoryBarriers.clear();
        bufferBarriers.clear();
        imageBarriers.clear();
    }
};

class ResourceBarrierPlanner
{
public:
    void plan(const ResourceAccessList& accesses, ResourceBarriers& result);
    void invalidate(); // An opaque command is a conservative boundary on both sides.

private:
    struct AccessState
    {
        VkPipelineStageFlags2 writerStages{}, readerStages{}, visibleReadStages{};
        VkAccessFlags2 writerAccess{}, readerAccess{};
    };
    std::unordered_map<VkBuffer, AccessState> buffers;
    using ImageKey = std::tuple<VkImage, VkImageAspectFlags, uint32_t, uint32_t>;
    std::map<ImageKey, AccessState> images;
    bool boundary{true};
    static bool dependency(AccessState& state, VkPipelineStageFlags2 stages, VkAccessFlags2 access,
                           VkPipelineStageFlags2& sourceStages, VkAccessFlags2& sourceAccess);
};

VkImageSubresourceRange imageViewRange(const ResourceManager::ImageHardwareWrap& image);
VkImageLayout imageSubresourceLayout(const ResourceManager::ImageHardwareWrap& image, uint32_t mip, uint32_t layer);
void setImageSubresourceLayout(ResourceManager::ImageHardwareWrap& image, const VkImageSubresourceRange& range, VkImageLayout layout);

// Recording may throw before queue submission; do not publish layouts for GPU
// commands that were never submitted.
struct ResourceLayoutJournal {
    bool committed{false};
    std::map<ResourceManager::ImageAllocation*, std::vector<VkImageLayout>> allocations;
    std::map<ResourceManager::ImageHardwareWrap*, VkImageLayout> wrappers;
    void capture(const ResourceAccessList& accesses);
    ~ResourceLayoutJournal();
};
