#pragma once

#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceAccess.h"
#include <optional>
#include <span>
#include <array>
#include <variant>

using PipelineResource = std::variant<HardwareBuffer, HardwareImage>;
bool sameResource(const PipelineResource& a, const PipelineResource& b);

// Parameter snapshots carry this marker until commit resolves the binding.
// A cleared draw parameter remains zero and must not be patched on replay.
inline constexpr uint32_t pendingDescriptorIndex = UINT32_MAX;

// A captured binding owns the resource and its reflected access in this shader variant.
struct PipelineBinding
{
    explicit PipelineBinding(const HardwareExecutor::Descriptor& descriptor);
    PipelineBinding(PipelineResource resource, EmbeddedShader::Ast::AccessPermissions access, bool sampled = false);
    void applyFieldAccess(uint32_t set, uint32_t binding, EmbeddedShader::Ast::AccessPermissions declared, bool aliased);

    uint64_t offset{};
    uint32_t size{};
    bool uniform{};
    PipelineResource resource;
    uint32_t fixedSlot{UINT32_MAX};
    uint32_t table{};
    uint32_t privateSlot{UINT32_MAX};
    VkPipelineStageFlags2 stages{};
    VkAccessFlags2 access{};

private:
    explicit PipelineBinding(HardwareExecutor::Descriptor::Impl descriptor);
};

struct BindingSnapshot
{
    std::vector<PipelineBinding> values;
    std::array<uint32_t, 3> tableLimits{UINT32_MAX, UINT32_MAX, UINT32_MAX};
    VkDevice device{VK_NULL_HANDLE};
    std::array<VkDescriptorPool, 3> pools{};
    std::array<VkDescriptorSet, 3> sets{};
    bool prepared{false};
    BindingSnapshot() = default;
    BindingSnapshot(const BindingSnapshot&) = delete;
    ~BindingSnapshot();
    // Fixed slots and bounded shader arrays use private descriptor tables.
    void resolveResourceUses(ResourceAccessList& result) const;
    void prepareDescriptors();
    void writeParameters(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes) const;
};

struct PipelineBindings
{
    std::vector<PipelineBinding> values;
    std::array<uint32_t, 3> tableLimits{UINT32_MAX, UINT32_MAX, UINT32_MAX};
    std::shared_ptr<BindingSnapshot> cached;
    void addLayout(const EmbeddedShader::ShaderCodeModule::ShaderResources& resources);
    void bindAuto(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, const EmbeddedShader::AutoBindEntry& entry);
    void writeValue(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, int32_t bindType,
                    uint64_t offset, const void* data, size_t size);
    void writeResource(std::span<uint8_t> pushConstants, std::span<uint8_t> uniformBytes, int32_t bindType,
                       uint64_t offset, uint32_t typeSize, PipelineBinding value, VkPipelineStageFlags2 stages,
                       std::optional<uint32_t> fixedSlot = std::nullopt);
    void erase(uint64_t offset, size_t size, bool uniform);
    void set(PipelineBinding binding);
    std::shared_ptr<BindingSnapshot> snapshot();
};

namespace EmbeddedShader {
template<class PipelineType>
BoundField<PipelineType> BoundField<PipelineType>::operator[](uint32_t element) const
{
    if (descriptorCount_ == UINT64_MAX) throw std::invalid_argument("Only descriptor fields support indexing");
    if (element == UINT32_MAX || (descriptorCount_ && element >= descriptorCount_))
        throw std::out_of_range("Shader descriptor array index is out of range");
    auto field = *this;
    field.element_ = element;
    return field;
}

template<class PipelineType>
void BoundField<PipelineType>::writeValue(const void* data, size_t size)
{
    auto& state = PipelineType::Impl::checked(pipeline_->impl_);
    state.backend->setPushConstantDirect(byteOffset, data, size, bindType);
}

template<class PipelineType>
void BoundField<PipelineType>::writeResource(const ::HardwareBuffer& buffer)
{
    using Bind = ShaderCodeModule::ShaderResources;
    if (bindType != Bind::storageBuffer && bindType != Bind::rawBuffer)
        throw std::invalid_argument("Assign a buffer descriptor to a shader index field");
    const auto access = access_ != Ast::AccessPermissions::None ? access_
        : bindType == Bind::rawBuffer ? Ast::AccessPermissions::ReadOnly : Ast::AccessPermissions::ReadAndWrite;
    writeBinding(PipelineBinding(buffer, access));
}

template<class PipelineType>
void BoundField<PipelineType>::writeResource(const ::HardwareImage& image)
{
    using Bind = ShaderCodeModule::ShaderResources;
    if constexpr (std::is_same_v<PipelineType, ::RasterPipeline<>>) {
        if (bindType == Bind::stageOutputs) {
            PipelineType::Impl::checked(pipeline_->impl_).backend->setRenderTarget(location, image);
            return;
        }
    }
    if (bindType == Bind::storageTexture) {
        const auto access = access_ == Ast::AccessPermissions::None ? Ast::AccessPermissions::ReadAndWrite : access_;
        writeBinding(PipelineBinding(image, access));
    } else if (bindType == Bind::texture || bindType == Bind::sampledImages || bindType == Bind::sampler)
        writeBinding(PipelineBinding(image, Ast::AccessPermissions::ReadOnly, true));
    else throw std::invalid_argument("Assign an image descriptor to a shader index field");
}

template<class PipelineType>
void BoundField<PipelineType>::writeResource(const Descriptor& descriptor)
{
    writeBinding(PipelineBinding(descriptor));
}

template<class PipelineType>
void BoundField<PipelineType>::writeBinding(auto value)
{
    if (descriptorCount_ != UINT64_MAX) value.applyFieldAccess(set, binding, access_, aliased_);
    auto& state = PipelineType::Impl::checked(pipeline_->impl_);
    state.backend->setResourceDirect(byteOffset, typeSize, std::move(value), bindType, element_);
}

// Only the pipeline implementation files have the complete Impl needed for these writes.
extern template struct BoundField<::ComputePipeline<>>;
extern template struct BoundField<::RasterPipeline<>>;
} // namespace EmbeddedShader

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
