#pragma once

#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceAccess.h"
#include <optional>
#include <span>
#include <array>

using PipelineResource = std::variant<HardwareBuffer, HardwareImage>;

// Parameter snapshots carry this marker until commit resolves the binding.
// A cleared draw parameter remains zero and must not be patched on replay.
inline constexpr uint32_t pendingDescriptorIndex = UINT32_MAX;

// CPU binding state retains resources and parameter locations independently of
// descriptor allocation and the caller's dependency declaration.
struct PipelineBinding
{
    uint64_t offset{};
    uint32_t size{};
    bool uniform{};
    PipelineResource resource;
    uint32_t fixedSlot{UINT32_MAX};
    bool sampled{};
};

struct BindingSnapshot
{
    std::vector<PipelineBinding> values;
    VkDevice device{VK_NULL_HANDLE};
    std::array<VkDescriptorPool, 3> pools{};
    std::array<VkDescriptorSet, 3> sets{};
    bool prepared{false};
    BindingSnapshot() = default;
    BindingSnapshot(const BindingSnapshot&) = delete;
    ~BindingSnapshot();
    // Automatic descriptors are shared; fixed-slot bindings get private tables.
    void prepareDescriptors();
    void writeParameters(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes) const;
};

struct PipelineBindings
{
    std::vector<PipelineBinding> values;
    std::shared_ptr<BindingSnapshot> cached;
    void writeValue(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, int32_t bindType,
                    uint64_t offset, const void* data, size_t size);
    void writeResource(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, int32_t bindType,
                       uint64_t offset, uint32_t typeSize, PipelineResource resource,
                       std::optional<uint32_t> fixedSlot = std::nullopt);
    void erase(uint64_t offset, size_t size, bool uniform);
    void set(PipelineBinding binding);
    std::shared_ptr<BindingSnapshot> snapshot();
};

// Each invocation owns its upload and descriptor set until its submission's
// timeline point completes. The layout is compatible with the pipeline's set 3.
struct UniformSnapshot
{
    HardwareBuffer buffer;
    std::vector<uint8_t> uploadedBytes;
    VkDevice device{VK_NULL_HANDLE};
    VkDescriptorPool pool{VK_NULL_HANDLE};
    VkDescriptorSet set{VK_NULL_HANDLE};

    UniformSnapshot() = default;
    UniformSnapshot(const UniformSnapshot&) = delete;
    UniformSnapshot& operator=(const UniformSnapshot&) = delete;
    ~UniformSnapshot();
    void prepare(std::span<const uint8_t> bytes, VkDescriptorSetLayout layout);
};

// A slot is writable only when no queued/in-flight invocation retains it.
struct UniformSnapshotPool {
    std::vector<std::shared_ptr<UniformSnapshot>> slots;
    size_t next{0};
    std::shared_ptr<UniformSnapshot> acquire(std::span<const uint8_t> bytes, VkDescriptorSetLayout layout);
};

VkDescriptorSetLayout createUniformSnapshotLayout(VkDevice device);
std::vector<uint8_t> copyParameterBytes(const uint8_t* data, size_t size);
void resolveShaderBindings(ResourceAccessList& result, std::span<const PipelineBinding> bindings,
                           VkPipelineStageFlags2 stages);
void resolveDeclaredUses(ResourceAccessList& result, std::span<const ResourceUse> declarations,
                         VkPipelineStageFlags2 stages);
