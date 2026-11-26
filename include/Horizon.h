#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <ranges>
#include <source_location>
#include <span>
#include <utility>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include "Codegen/ComputePipelineObject.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/RasterizedPipelineObject.h"
#include "Codegen/VariateProxy.h"
#include "Compiler/ShaderCodeCompiler.h"
#include <ktm/ktm.h>

namespace horizon::detail {
struct CommandRecordVulkan;
struct ComputePipelineVulkan;
struct RasterizerPipelineVulkan;
struct CopyCommandImpl;
}

struct HardwareExecutor;
struct HardwareImage;

// Transfers retain both resources. A zero buffer copy size uses the source remainder.
class CopyCommand
{
public:
    CopyCommand() = default;
    explicit operator bool() const { return impl_ != nullptr; }

private:
    friend struct HardwareBuffer;
    friend struct HardwareImage;
    friend struct HardwareExecutor;

    explicit CopyCommand(std::shared_ptr<horizon::detail::CopyCommandImpl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<horizon::detail::CopyCommandImpl> impl_;
};

// Legacy resources and control objects are owner-thread objects.
// Copying/releasing shared handles and modifying resource state require external serialization.
// Pipeline, executor and displayer handles are move-only. Queued work retains its pipeline.

enum class ImageFormat : uint32_t
{
    RGBA8_UINT,
    RGBA8_SINT,
    RGBA8_SRGB,

    RGBA16_UINT,
    RGBA16_SINT,
    RGBA16_FLOAT,

    RGBA32_UINT,
    RGBA32_SINT,
    RGBA32_FLOAT,

    RG32_FLOAT,

    D16_UNORM,
    D32_FLOAT,

    BC1_RGB_UNORM,
    BC1_RGB_SRGB,
    BC2_RGBA_UNORM,
    BC2_RGBA_SRGB,
    BC3_RGBA_UNORM,
    BC3_RGBA_SRGB,
    BC4_R_UNORM,
    BC4_R_SNORM,
    BC5_RG_UNORM,
    BC5_RG_SNORM,

    ASTC_4x4_UNORM,
    ASTC_4x4_SRGB,
    RGBA8_UNORM, R32_FLOAT, BC7_UNORM, BC7_SRGB
};

enum class ImageUsage : uint32_t
{
    SampledImage = 1,
    StorageImage = 2,
    DepthImage = 3,
    SampledStorageImage = 4,
};

enum class BufferUsage : uint32_t
{
    VertexBuffer = 1,
    IndexBuffer = 2,
    UniformBuffer = 4,
    StorageBuffer = 8,
    RasterIndirect = 16,
};

namespace horizon::detail {
template <typename T>
concept IsContainer = requires(T a) {
    { a.size() } -> std::convertible_to<size_t>;
    { a.data() };
    { a[0] };
};
}

struct ExternalHandle
{
#if defined(_WIN32)
    void* handle = nullptr;
#else
    int fd = -1;
#endif
};

// ================= 对外封装：HardwareBuffer =================
struct HardwareBuffer
{
public:
    HardwareBuffer();
    HardwareBuffer(const HardwareBuffer& other);
    HardwareBuffer(HardwareBuffer&& other) noexcept;
    HardwareBuffer(uint32_t bufferSize, uint32_t elementSize, BufferUsage usage, const void* data = nullptr, bool useDedicated = true);

    HardwareBuffer(uint32_t size, BufferUsage usage, const void* data = nullptr, bool useDedicated = true)
        : HardwareBuffer(1, size, usage, data, useDedicated)
    {
    }

    template <horizon::detail::IsContainer Container>
    HardwareBuffer(const Container& input, BufferUsage usage, bool useDedicated = true)
        : HardwareBuffer(input.size(), sizeof(input[0]), usage, input.data(), useDedicated)
    {
    }

    // Replace this handle with a new device-local allocation and queue its initial upload.
    // The executor owns the staged bytes, so the caller may release data on return.
    void createDeviceLocal(HardwareExecutor& executor, BufferUsage usage, const void* data,
                           size_t elementCount, size_t elementSize);

    HardwareBuffer(const ExternalHandle& memHandle, uint32_t bufferSize, uint32_t elementSize, uint32_t allocSize, BufferUsage usage);

    ~HardwareBuffer();

    HardwareBuffer& operator=(const HardwareBuffer& other);
    HardwareBuffer& operator=(HardwareBuffer&& other) noexcept;

    explicit operator bool() const;

    ExternalHandle exportBufferMemory();

    [[nodiscard]] uint32_t storeDescriptor() const;

    // 流式拷贝命令（用于 HardwareExecutor << ）
    [[nodiscard]] CopyCommand copyTo(const HardwareBuffer& dst, uint64_t srcOffset = 0,
                                     uint64_t dstOffset = 0, uint64_t size = 0) const;
    [[nodiscard]] CopyCommand copyTo(const HardwareImage& dst, uint64_t bufferOffset = 0,
                                     uint32_t imageLayer = 0, uint32_t imageMip = 0) const;

    // CPU 映射内存操作（非 GPU 命令）
    bool copyFromData(const void* inputData, uint64_t size) const;
    bool copyToData(void* outputData, uint64_t size) const;

    template <std::ranges::contiguous_range R>
        requires std::ranges::sized_range<R> && std::ranges::borrowed_range<R> &&
                 std::is_trivially_copyable_v<std::ranges::range_value_t<R>> &&
                 std::same_as<std::ranges::range_reference_t<R>, std::ranges::range_value_t<R>&>
    bool copyToData(R&& output) const
    {
        const auto destination = std::span{output};
        return copyToData(destination.data(), destination.size_bytes());
    }

    [[nodiscard]] void* getMappedData() const;
    // Make CPU writes through getMappedData() visible to subsequent GPU work.
    void flushMapped() const;
    [[nodiscard]] uint64_t getElementSize() const;
    [[nodiscard]] uint64_t getElementCount() const;

    [[nodiscard]] uintptr_t getBufferID() const
    {
        return bufferID;
    }

private:
    static HardwareBuffer rasterUpload(uint32_t bytes);
    std::uintptr_t bufferID{0};

    friend struct horizon::detail::RasterizerPipelineVulkan;
};

// ================= HardwareImage 参数结构体 =================
struct HardwareImageCreateInfo
{
    uint32_t width { 0 };
    uint32_t height { 0 };
    ImageFormat format = ImageFormat::RGBA8_SRGB;
    ImageUsage usage = ImageUsage::SampledImage;
    int arrayLayers { 1 };
    int mipLevels { 1 };
    bool cube{};
    bool linearFiltering{}; // Linear min/mag/mip filtering with supported anisotropy.
};

// ================= 对外封装：HardwareImage =================
struct HardwareImage
{
public:
    HardwareImage();
    HardwareImage(const HardwareImage& other);
    HardwareImage(HardwareImage&& other) noexcept;
    HardwareImage(uint32_t width, uint32_t height, ImageFormat imageFormat, ImageUsage imageUsage = ImageUsage::SampledImage, int arrayLayers = 1, void* imageData = nullptr);
    HardwareImage(const HardwareImageCreateInfo& createInfo);

    ~HardwareImage();

    HardwareImage& operator=(const HardwareImage& other);
    HardwareImage& operator=(HardwareImage&& other) noexcept;
    // image[layer][mip]
    HardwareImage operator[](const uint32_t index);
    explicit operator bool() const;

    [[nodiscard]] uint32_t storeDescriptor() const;
    [[nodiscard]] uint32_t storeSampledDescriptor() const;
    [[nodiscard]] uintptr_t getImageID() const
    {
        return imageID;
    }

    /// Set the clear color used by the RasterizerPipeline render pass (LOAD_OP_CLEAR).
    /// Default is (0, 0, 0, 1).  For transparent render targets, use (0, 0, 0, 0).
    void setClearColor(float r, float g, float b, float a);

    // 流式拷贝命令（用于 HardwareExecutor << ）
    [[nodiscard]] CopyCommand copyTo(const HardwareImage& dst, uint32_t srcLayer = 0, uint32_t dstLayer = 0,
                                     uint32_t srcMip = 0, uint32_t dstMip = 0) const;
    [[nodiscard]] CopyCommand copyTo(const HardwareBuffer& dst, uint32_t imageLayer = 0,
                                     uint32_t imageMip = 0, uint64_t bufferOffset = 0) const;
    [[nodiscard]] CopyCommand copyFrom(const void* inputData, uint32_t imageLayer = 0, uint32_t imageMip = 0) const;

private:
    std::uintptr_t imageID{0};
};

enum class ResourceAccess : uint8_t
{
    Read,
    Write,
    ReadWrite,
};

struct ResourceUse
{
    std::variant<HardwareBuffer, HardwareImage> resource;
    ResourceAccess access;
};

template<class VS = void, class FS = void>
struct RasterizerPipeline;

struct ScissorRect
{
    int32_t x { 0 };
    int32_t y { 0 };
    uint32_t width { 0 };
    uint32_t height { 0 };
};

enum class RasterCompare { Less, LessEqual, Equal, Always };
enum class RasterBlend { Alpha, Opaque, Additive };
enum class RasterTopology { TriangleList, TriangleStrip };
struct RasterState {
    bool depthTest{true}, depthWrite{true};
    RasterCompare compare{RasterCompare::Less};
    RasterBlend blend{RasterBlend::Alpha};
    RasterTopology topology{RasterTopology::TriangleList};
    bool operator==(const RasterState&) const = default;
};
struct RasterPass {
    bool loadColor{}, loadDepth{};
    bool operator==(const RasterPass&) const = default;
};

// Vulkan consumes the first four words. The vertex shader consumes the remaining words.
struct RasterIndirectPacket {
    uint32_t vertexCount{}, instanceCount{}, firstVertex{}, firstInstance{};
    uint32_t geometryIndex{}, firstIndex{};
    int32_t baseVertex{};
    uint32_t parameterWord{};
};

class RasterBatch {
public:
    RasterBatch(RasterBatch&&) noexcept = default;
    RasterBatch& operator=(RasterBatch&&) noexcept = default;
    // Append the first count packets in order, with one geometry pair and scissor for this call.
    // Each packet uses geometryIndex = firstVertex = 0. parameterWord is an offset into parameters in 4-byte words.
    // The batch retains its buffers until GPU completion. Omit parameters when the shader does not read them.
    void drawIndirect(const HardwareBuffer& vertices, const HardwareBuffer& indices, const HardwareBuffer& packets,
                      uint32_t count, const HardwareBuffer& parameters = {},
                      std::optional<ScissorRect> scissor = {});
private:
    friend struct HardwareExecutor;
    friend struct RasterizerPipeline<>;
    RasterBatch(std::shared_ptr<horizon::detail::CommandRecordVulkan> record, const std::vector<ResourceUse>& uses)
        : record_(std::move(record)), uses_(uses) {}
    std::shared_ptr<horizon::detail::CommandRecordVulkan> record_;
    std::vector<ResourceUse> uses_;
};

// ================= 对外封装：HardwareDisplayer =================
enum class PresentDevicePreference { RenderDevice, AlternateDevice };

struct HardwareDisplayer
{
public:
    explicit HardwareDisplayer(void* surface = nullptr,
                               PresentDevicePreference preference = PresentDevicePreference::AlternateDevice);
    HardwareDisplayer(HardwareDisplayer&& other) noexcept;
    ~HardwareDisplayer();

    HardwareDisplayer& operator=(HardwareDisplayer&& other) noexcept;
    HardwareDisplayer& operator<<(const HardwareImage& image);
    // Commit pending work and present its output. A skipped presentation still commits the work.
    bool present(HardwareExecutor& pending, const HardwareImage& image);
    // Present tightly packed RGBA8 pixels (UNORM or SRGB). The span may be reused on return.
    bool present(std::span<const uint32_t> pixels, uint32_t width, uint32_t height, ImageFormat format);

    HardwareDisplayer& wait(const HardwareExecutor& executor);

    [[nodiscard]] uintptr_t getDisplayerID() const
    {
        return displaySurfaceID;
    }

private:
    std::uintptr_t displaySurfaceID{0};
    void* displaySurface{nullptr};
};

template<class CS = void>
struct ComputePipeline
{
    static_assert(std::same_as<CS, void>, "A named compute stage must provide shader bindings");

private:
    template<class> friend struct ComputePipeline;
    friend struct HardwareExecutor;
    friend struct EmbeddedShader::BoundField<ComputePipeline>;

    explicit ComputePipeline(const EmbeddedShader::ShaderCodeModule& module);
    explicit ComputePipeline(EmbeddedShader::ComputePipelineObject shader);
    void setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer& buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage& image, int32_t bindType, uint32_t = 0);
    horizon::detail::ComputePipelineVulkan& backend() const;

    std::shared_ptr<horizon::detail::ComputePipelineVulkan> computePipeline_;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries_;
    std::vector<ResourceUse> resourceUses_;

public:
    ComputePipeline();

    template<class F>
        requires std::invocable<F>
    ComputePipeline(F&& shader, ktm::uvec3 numthreads = ktm::uvec3(1), EmbeddedShader::CompilerOption options = {},
                    const std::source_location& location = std::source_location::current())
        : ComputePipeline(EmbeddedShader::ComputePipelineObject::compile(
              std::forward<F>(shader), numthreads, options, location)) {}

    ComputePipeline(ComputePipeline&&) noexcept = default;
    ComputePipeline& operator=(ComputePipeline&&) noexcept = default;

    // Set workgroup counts and read proxy bindings. The executor snapshots them at enqueue.
    ComputePipeline& operator()(uint16_t x, uint16_t y = 1, uint16_t z = 1);

    ComputePipeline& uses(std::span<const ResourceUse> resources)
    {
        resourceUses_.assign(resources.begin(), resources.end());
        return *this;
    }
    ComputePipeline& uses(std::initializer_list<ResourceUse> resources) { return uses(std::span(resources)); }
};

extern template struct ComputePipeline<void>;

template<class CS> requires (!std::is_void_v<CS>)
struct ComputePipeline<CS> : CS::template Bindings<ComputePipeline<>>
{
private:
    friend struct HardwareExecutor;
    using Fields = typename CS::template Bindings<ComputePipeline<>>;
    ComputePipeline<> pipeline_;

public:
    ComputePipeline() : Fields(&pipeline_), pipeline_(CS::shaderModule) {}

    ComputePipeline(const CS&) : ComputePipeline() {}

    ComputePipeline(ComputePipeline&& other) noexcept
        : Fields(&pipeline_), pipeline_(std::move(other.pipeline_)) {}

    ComputePipeline& operator=(ComputePipeline&& other) noexcept
    {
        pipeline_ = std::move(other.pipeline_);
        return *this;
    }

    ComputePipeline& operator()(uint16_t x, uint16_t y = 1, uint16_t z = 1)
    {
        pipeline_(x, y, z);
        return *this;
    }

    ComputePipeline& uses(std::span<const ResourceUse> resources)
    {
        pipeline_.uses(resources);
        return *this;
    }
    ComputePipeline& uses(std::initializer_list<ResourceUse> resources) { return uses(std::span(resources)); }
};

template<class CS> requires requires { CS::shaderModule; typename CS::template Bindings<ComputePipeline<>>; }
ComputePipeline(const CS&) -> ComputePipeline<CS>;

template<class VS, class FS>
struct RasterizerPipeline
{
    static_assert(std::same_as<VS, void> && std::same_as<FS, void>,
                  "Register this VS/FS pair in RASTER_PAIRS and include both stage headers or the generated pipeline header");

private:
    template<class, class> friend struct RasterizerPipeline;
    friend struct EmbeddedShader::BoundField<RasterizerPipeline>;

    RasterizerPipeline(const EmbeddedShader::ShaderCodeModule& vertex,
                       const EmbeddedShader::ShaderCodeModule& fragment, uint32_t views);
    RasterizerPipeline(EmbeddedShader::RasterizedPipelineObject shaders, uint32_t views);

    void setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer& buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage& image, int32_t bindType, uint32_t location = 0);
    void refreshShaderVariant();
    RasterBatch capture(std::optional<ktm::uvec2> extent, RasterPass pass);
    horizon::detail::RasterizerPipelineVulkan& backend() const;

    std::shared_ptr<horizon::detail::RasterizerPipelineVulkan> rasterizerPipeline_;
    std::unique_ptr<EmbeddedShader::RasterizedPipelineObject> variants_;
    EmbeddedShader::ShaderCodeCompiler::ConditionInfo vertexConditions_, fragmentConditions_;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries_;
    size_t explicitTargetCount_{}; // The last entries override reflected outputs.
    std::vector<ResourceUse> resourceUses_;

public:
    RasterizerPipeline();

    template<class V, class F>
        requires requires(V& vertex, F& fragment) { std::function{vertex}; std::function{fragment}; }
    RasterizerPipeline(V&& vertex, F&& fragment, EmbeddedShader::CompilerOption options = {}, uint32_t views = 1,
                       std::source_location location = std::source_location::current())
        : RasterizerPipeline(EmbeddedShader::RasterizedPipelineObject::compile(
              std::forward<V>(vertex), std::forward<F>(fragment), options, location), views) {}

    void setRasterState(RasterState state);
    void setDepthImage(HardwareImage& image);
    [[nodiscard]] HardwareImage getDepthImage();

    [[nodiscard]] RasterBatch beginBatch(uint16_t width, uint16_t height, RasterPass pass = {})
    {
        return capture(ktm::uvec2(width, height), pass);
    }
    [[nodiscard]] RasterBatch beginBatch(RasterPass pass = {}) { return capture(std::nullopt, pass); }

    RasterizerPipeline& uses(std::initializer_list<ResourceUse> resources)
    {
        resourceUses_.assign(resources.begin(), resources.end());
        return *this;
    }

    template<class T>
    RasterizerPipeline& bindRenderTarget(uint32_t location, EmbeddedShader::Texture2DProxy<T>& proxy)
    {
        autoBindEntries_.push_back({ &proxy.boundResource_, 0, 0,
            static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs), location });
        ++explicitTargetCount_;
        return *this;
    }
};

extern template struct RasterizerPipeline<void, void>;

template<class VS, class FS> requires requires { VS::shaderModule; FS::shaderModule; }
RasterizerPipeline(const VS&, const FS&, uint32_t = 1) -> RasterizerPipeline<VS, FS>;

// ================= 对外封装：HardwareExecutor =================
struct HardwareExecutor
{
public:
    HardwareExecutor();
    HardwareExecutor(HardwareExecutor&& other) noexcept;
    ~HardwareExecutor();

    HardwareExecutor& operator=(HardwareExecutor&& other) noexcept;

    template<class CS>
    HardwareExecutor& operator<<(ComputePipeline<CS>& pipeline)
    {
        if constexpr (std::is_void_v<CS>) return enqueue(pipeline);
        else return enqueue(pipeline.pipeline_);
    }
    HardwareExecutor& operator<<(RasterBatch&& batch);
    HardwareExecutor& operator<<(const CopyCommand& cmd);

    // Depend on the most recent successful submission of other at the time of this call.
    HardwareExecutor& wait(HardwareExecutor& other);
    HardwareExecutor& commit();

    // ========== 延迟释放相关接口 ==========
    /// @brief 等待所有延迟释放的资源完成（阻塞）
    void waitForDeferredResources();

    /// @brief 手动触发一次清理（非阻塞）
    void cleanupDeferredResources();

    [[nodiscard]] uintptr_t getExecutorID() const
    {
        return executorID;
    }

private:
    HardwareExecutor& enqueue(const ComputePipeline<>& pipeline);
    std::uintptr_t executorID{0};
};
