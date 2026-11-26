#pragma once

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "Horizon.h"
#include "Compiler/ShaderCodeCompiler.h"
#include "HardwareWrapperVulkan/HardwareVulkan/DeviceManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "PipelineSnapshot.h"

namespace horizon::detail {
struct RasterizerPipelineVulkan
{
  public:
    RasterizerPipelineVulkan();
    ~RasterizerPipelineVulkan();

    RasterizerPipelineVulkan(EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                             EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                             uint32_t multiviewCount);

    RasterizerPipelineVulkan(const EmbeddedShader::ShaderCodeModule &vertexSpirV,
                             const EmbeddedShader::ShaderCodeModule &fragmentSpirV,
                             uint32_t multiviewCount);

    void setDepthImage(HardwareImage &depthImage)
    {
        this->depthImage = depthImage;
        explicitDepthImage = static_cast<bool>(depthImage);
    }

    void setRasterState(RasterState state) { rasterState = state; }
    std::shared_ptr<RasterizerPipelineVulkan> variant(EmbeddedShader::ShaderCodeModule vertex,
                                                      EmbeddedShader::ShaderCodeModule fragment) const;

    [[nodiscard]] HardwareImage &getDepthImage()
    {
        return depthImage;
    }

    // Direct-access methods: bypass string lookup, use pre-resolved metadata
    void setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location = 0);

    struct Root {
        VkDeviceAddress packets, geometries, parameters;
        uint32_t groupBase, drawBase;
    };
    static_assert(sizeof(RasterIndirectPacket) == 32);
    static_assert(offsetof(RasterIndirectPacket, geometryIndex) == sizeof(VkDrawIndirectCommand));
    static_assert(offsetof(RasterIndirectPacket, firstInstance) == offsetof(VkDrawIndirectCommand, firstInstance));
    static_assert(sizeof(Root) == 32 && offsetof(Root, drawBase) == 28);

    struct GraphicsState
    {
        VkDevice device{};
        VkRenderPass renderPass{};
        VkPipeline pipeline{};
        VkPipelineLayout layout{};
        std::vector<VkFormat> colorFormats;
        std::vector<VkImageLayout> finalLayouts;
        VkFormat depthFormat{};
        RasterState state;
        RasterPass pass;
        ~GraphicsState();
    };
    struct FramebufferState
    {
        VkDevice device{};
        VkFramebuffer framebuffer{};
        std::shared_ptr<GraphicsState> graphics;
        std::vector<HardwareImage> targets;
        HardwareImage depth;
        ktm::uvec2 size{};
        ~FramebufferState();
    };
  private:
    friend class ::RasterCommand;
    std::vector<std::shared_ptr<HardwareBuffer>> uploadPool;
    std::mutex uploadMutex;
    void initializeStageBindings(uint32_t viewCount);
    void createRenderPass(GraphicsState& state);
    void createGraphicsPipeline(GraphicsState& state);

    int multiviewCount{1};
    bool mergeWithoutUses{};

    std::vector<std::shared_ptr<GraphicsState>> graphicsStates;
    std::shared_ptr<FramebufferState> lastFramebuffer;

    RasterState rasterState;
    bool explicitDepthImage{false};

    HardwareImage depthImage;
    std::vector<HardwareImage> renderTargets;

    EmbeddedShader::ShaderCodeModule vertShaderCode;
    EmbeddedShader::ShaderCodeModule fragShaderCode;

    // Per-pipeline UBO support
    std::unique_ptr<uint8_t[]> tempUBO;
    uint32_t uboSize{0};
    VkDescriptorSetLayout uboDescriptorSetLayout{VK_NULL_HANDLE};
    PipelineBindings bindings;
    UniformSnapshotPool uniformPool;

    std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> vertexStageInputs;
    std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> vertexStageOutputs;
    std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> fragmentStageInputs;
    std::vector<EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo> fragmentStageOutputs;
};
} // namespace horizon::detail

using horizon::detail::RasterizerPipelineVulkan;

struct RasterCommand::Impl final : CommandRecordVulkan
{
    struct GeometryEntry {
        VkDeviceAddress vertices, indices;
        uint32_t strideWords, indexBytes, vertexCount, reserved{};
    };
    static_assert(sizeof(GeometryEntry) == 32);
    struct IndirectGroup {
        HardwareBuffer vertices, indices, packets, parameters;
        std::shared_ptr<HardwareBuffer> geometryUpload;
        VkRect2D scissor;
        uint32_t count;
        RasterizerPipelineVulkan::Root root;
    };

    std::shared_ptr<RasterizerPipelineVulkan> pipeline;
    ktm::uvec2 imageSize{};
    RasterState state;
    RasterPass pass;
    bool startsPass{};
    HardwareImage depthImage;
    std::vector<HardwareImage> renderTargets;
    std::vector<VkClearValue> clearValues;
    std::vector<IndirectGroup> groups;
    uint32_t drawCount{};
    std::vector<ResourceUse> resourceUses;
    std::vector<uint8_t> uniformBytes;
    std::shared_ptr<BindingSnapshot> bindings;
    std::shared_ptr<UniformSnapshot> uniform;
    std::shared_ptr<RasterizerPipelineVulkan::FramebufferState> framebuffer;

    Impl(std::shared_ptr<RasterizerPipelineVulkan> backend, std::optional<ktm::uvec2> extent, std::optional<RasterPass> pass,
         const std::vector<ResourceUse>& uses);
    bool sameAttachments(const Impl& next) const;
    bool canMerge(const Impl& next) const;
    void appendIndirect(const HardwareBuffer& vertices, const HardwareBuffer& indices,
                        const HardwareBuffer& packets, uint32_t count, const HardwareBuffer& parameters,
                        std::optional<ScissorRect> scissor);
    void prepareFramebuffer();
    ExecutorType getExecutorType() override { return ExecutorType::Graphics; }
    void resolveResourceUses(std::span<const ResourceUse> declarations, ResourceAccessList& result) override;
    void commitCommand(HardwareExecutorVulkan& executor) override;
};
