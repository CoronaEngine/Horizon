#pragma once

#include <memory>
#include <map>
#include <vector>

#include "Horizon_old.h"
#include "Compiler/ShaderCodeCompiler.h"
#include "HardwareWrapperVulkan/HardwareVulkan/DeviceManager.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceManager.h"
#include "PipelineSnapshot.h"

struct RasterizerPipelineVulkan : std::enable_shared_from_this<RasterizerPipelineVulkan>
{
  public:
    RasterizerPipelineVulkan();
    ~RasterizerPipelineVulkan();

    RasterizerPipelineVulkan(std::string vertexShaderCode,
                             std::string fragmentShaderCode,
                             uint32_t multiviewCount = 1,
                             EmbeddedShader::ShaderLanguage vertexShaderLanguage = EmbeddedShader::ShaderLanguage::GLSL,
                             EmbeddedShader::ShaderLanguage fragmentShaderLanguage = EmbeddedShader::ShaderLanguage::GLSL,
                             const std::source_location &sourceLocation = std::source_location::current());

    // 从已编译的 ShaderCodeCompiler 构造
    RasterizerPipelineVulkan(EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                             EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                             uint32_t multiviewCount = 1,
                             const std::source_location &sourceLocation = std::source_location::current());

    // 从 SPIR-V + Slang 反射构造
    RasterizerPipelineVulkan(const EmbeddedShader::ShaderCodeModule &vertexSpirV,
                             const EmbeddedShader::ShaderCodeModule &fragmentSpirV,
                             uint32_t multiviewCount = 1,
                             const std::source_location &sourceLocation = std::source_location::current());

    void setDepthImage(HardwareImage &depthImage)
    {
        this->depthImage = depthImage;
        explicitDepthImage = static_cast<bool>(depthImage);
    }

    void setDepthEnabled(bool enabled) { rasterState.depthTest = rasterState.depthWrite = enabled; }
    void setRasterState(RasterState state) { rasterState = state; }
    std::shared_ptr<RasterizerPipelineVulkan> variant(EmbeddedShader::ShaderCodeCompiler& vertex,
                                                      EmbeddedShader::ShaderCodeCompiler& fragment) const;

    [[nodiscard]] HardwareImage &getDepthImage()
    {
        return depthImage;
    }

    // Direct-access methods: bypass string lookup, use pre-resolved metadata
    void setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location = 0);

    using Packet = RasterIndirectPacket;
    struct GeometryEntry {
        VkDeviceAddress vertices, indices;
        uint32_t strideWords, indexBytes, vertexCount, reserved{};
    };
    struct Root {
        VkDeviceAddress packets, geometries, parameters;
        uint32_t groupBase, padding{};
    };
    static_assert(sizeof(Packet) == 32 && offsetof(Packet, geometryIndex) == sizeof(VkDrawIndirectCommand));
    static_assert(offsetof(Packet, firstInstance) == offsetof(VkDrawIndirectCommand, firstInstance));
    static_assert(sizeof(GeometryEntry) == 32 && sizeof(Root) == 32);
    struct Run { VkRect2D scissor; uint32_t first, count; };

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
    struct Batch : CommandRecordVulkan
    {
        std::shared_ptr<RasterizerPipelineVulkan> pipeline;
        ktm::uvec2 imageSize{};
        RasterState state;
        RasterPass pass;
        HardwareImage depthImage;
        std::vector<HardwareImage> renderTargets;
        std::vector<VkClearValue> clearValues;
        std::vector<Packet> packets;
        std::vector<RasterGeometry> geometries;
        std::map<std::pair<uintptr_t, uintptr_t>, uint32_t> geometryIndices;
        std::vector<uint8_t> parameters;
        std::vector<Run> runs;
        std::shared_ptr<HardwareBuffer> upload;
        HardwareBuffer externalPackets;
        void appendIndirect(const RasterGeometry&, const HardwareBuffer&, uint32_t, const void*, size_t);
        Root root{};
        void append(const RasterGeometry&, const RasterDraw&, const void*, size_t);
        void prepareUpload();
        std::vector<uint8_t> uniformBytes;
        std::shared_ptr<BindingSnapshot> bindings;
        std::shared_ptr<UniformSnapshot> uniform;
        std::shared_ptr<FramebufferState> framebuffer;
        ExecutorType getExecutorType() override { return ExecutorType::Graphics; }
        void resolveResourceUses(std::span<const ResourceUse> declarations, ResourceAccessList& result) override;
        void commitCommand(HardwareExecutorVulkan& executor) override;
    };
    std::shared_ptr<Batch> capture(uint16_t width, uint16_t height, RasterPass pass);

  private:
    std::vector<std::shared_ptr<HardwareBuffer>> uploadPool;
    void initializeStageBindings(uint32_t viewCount);
    void createRenderPass(GraphicsState& state);
    void createGraphicsPipeline(GraphicsState& state);
    void prepareDepth(Batch& batch);
    void prepareFramebuffer(Batch& batch);
    void commitCommand(HardwareExecutorVulkan& executor, Batch& batch);

    int multiviewCount{1};

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
