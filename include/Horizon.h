#pragma once

#include <atomic>
#include <concepts>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <source_location>
#include <span>
#include <string>
#include <utility>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>

#include "Codegen/ComputePipelineObject.h"
#include "Codegen/RasterizedPipelineObject.h"
#include "Codegen/VariateProxy.h"
#include "Compiler/ShaderCodeCompiler.h"
#include <ktm/ktm.h>

// Forward declare platform-specific types instead of including platform headers
#if defined(_WIN32)
// Forward declare HANDLE without including Windows.h
#if !defined(_WINDEF_)
typedef void* HANDLE;
#endif
#endif

namespace horizon::detail {
struct CommandRecordVulkan;
struct ComputePipelineVulkan;
struct RasterizerPipelineVulkan;
struct CopyCommandImpl;
}

struct HardwareExecutor;
struct HardwareBuffer;
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
#if _WIN32 || _WIN64
    HANDLE handle = nullptr;
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

    // Stage initial data once for GPU reads. The returned allocation is not mapped.
    template <horizon::detail::IsContainer Container>
    [[nodiscard]] static HardwareBuffer uploadDeviceLocal(const Container& input, BufferUsage usage) {
        static_assert(std::is_trivially_copyable_v<std::remove_cvref_t<decltype(input[0])>>);
        return uploadDeviceLocal(input.size(), sizeof(input[0]), usage, input.data());
    }

    HardwareBuffer(const ExternalHandle& memHandle, uint32_t bufferSize, uint32_t elementSize, uint32_t allocSize, BufferUsage usage);

    ~HardwareBuffer();

    HardwareBuffer& operator=(const HardwareBuffer& other);
    HardwareBuffer& operator=(HardwareBuffer&& other) noexcept;

    explicit operator bool() const;

    ExternalHandle exportBufferMemory();
    // HardwareBuffer importBufferMemory(const ExternalHandle &memHandle);

    [[nodiscard]] uint32_t storeDescriptor() const;

    // 流式拷贝命令（用于 HardwareExecutor << ）
    [[nodiscard]] CopyCommand copyTo(const HardwareBuffer& dst, uint64_t srcOffset = 0,
                                     uint64_t dstOffset = 0, uint64_t size = 0) const;
    [[nodiscard]] CopyCommand copyTo(const HardwareImage& dst, uint64_t bufferOffset = 0,
                                     uint32_t imageLayer = 0, uint32_t imageMip = 0) const;

    // CPU 映射内存操作（非 GPU 命令）
    bool copyFromData(const void* inputData, uint64_t size) const;
    bool copyToData(void* outputData, uint64_t size) const;

    template <typename T>
    bool copyFromVector(const std::vector<T>& input)
    {
        return copyFromData(input.data(), input.size() * sizeof(T));
    }

    [[nodiscard]] void* getMappedData() const;
    [[nodiscard]] uint64_t getElementSize() const;
    [[nodiscard]] uint64_t getElementCount() const;

    [[nodiscard]] uintptr_t getBufferID() const
    {
        return bufferID;
    }

private:
    static HardwareBuffer uploadDeviceLocal(size_t count, size_t stride, BufferUsage usage, const void* data);
    static HardwareBuffer stageDeviceLocal(HardwareExecutor& upload, size_t count, size_t stride,
                                           BufferUsage usage, const void* data);
    static HardwareBuffer rasterUpload(uint32_t bytes);
    std::uintptr_t bufferID{0};

    friend class HardwareImage;
    friend struct RasterGeometry;
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
    // void *initialData{nullptr};

    HardwareImageCreateInfo() = default;
    HardwareImageCreateInfo(uint32_t w, uint32_t h, ImageFormat fmt = ImageFormat::RGBA8_SRGB)
        : width(w), height(h), format(fmt)
    {
    }
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

    //[[nodiscard]] uint32_t getNumMipLevels() const;
    //[[nodiscard]] uint32_t getArrayLayers() const;

private:
    std::uintptr_t imageID{0};

    friend class HardwareDisplayer;
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

// Forward declarations
template<class CS = void>
struct ComputePipeline;
template<class VS = void, class FS = void>
struct RasterizerPipeline;

// Compile the generated Slang modules with the current Helicon/Slang toolchain.
EmbeddedShader::ShaderCodeModule compileLegacyShaderModule(
    EmbeddedShader::ShaderStage stage, const EmbeddedShader::SlangModule& module);

struct ScissorRect
{
    int32_t x { 0 };
    int32_t y { 0 };
    uint32_t width { 0 };
    uint32_t height { 0 };
};

struct RasterGeometry {
    HardwareBuffer vertices;
    HardwareBuffer indices; // Elements must be uint16_t or uint32_t.

    // Upload both streams in one submission and return them ready for drawing.
    template<class Vertices, class Indices>
    [[nodiscard]] static RasterGeometry upload(const Vertices& vertices, const Indices& indices);
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
struct RasterDraw {
    uint32_t indexCount{}; // Zero consumes the remainder after firstIndex.
    uint32_t firstIndex{};
    int32_t vertexOffset{};
    uint32_t instanceCount{1};
    std::optional<ScissorRect> scissor;
    uint32_t firstInstance{};
};

enum class IndirectFirstInstance { MayBeNonzero, AlwaysZero };
class RasterBatch {
public:
    RasterBatch(RasterBatch&&) noexcept = default;
    RasterBatch& operator=(RasterBatch&&) noexcept = default;
    template<class T> requires std::is_trivially_copyable_v<T>
    void draw(const RasterGeometry& geometry, const RasterDraw& range, const T& parameters) {
        append(geometry, range, &parameters, sizeof(T));
    }
    void draw(const RasterGeometry& geometry, const RasterDraw& range = {}) {
        append(geometry, range, nullptr, 0);
    }
    using ParameterWriter = std::function<void(std::span<std::byte>)>;
    // Fill the span synchronously, keeping the batch idle until the writer returns.
    // The span expires when the writer returns.
    void drawIndirect(const RasterGeometry&, const HardwareBuffer& packets, uint32_t count, size_t parameterBytes,
                      const ParameterWriter&, IndirectFirstInstance firstInstance = IndirectFirstInstance::MayBeNonzero);
    // Packets share geometry 0. firstVertex is zero for index pulling.
    // parameterWord indexes the shared parameter payload in four-byte words.
    // AlwaysZero is a promise about every packet, including GPU-written packets.
    void drawIndirect(const RasterGeometry&, const HardwareBuffer& packets, uint32_t count,
                      IndirectFirstInstance firstInstance = IndirectFirstInstance::MayBeNonzero);
    template<class T, size_t Extent> requires (std::is_trivially_copyable_v<T> && !std::is_volatile_v<T>)
    void drawIndirect(const RasterGeometry& geometry, const HardwareBuffer& packets, uint32_t count,
                      std::span<T, Extent> parameters, IndirectFirstInstance firstInstance = IndirectFirstInstance::MayBeNonzero) {
        appendIndirect(geometry, packets, count, parameters.data(), parameters.size_bytes(), firstInstance);
    }
    template<class T> requires std::is_trivially_copyable_v<T>
    void drawIndirect(const RasterGeometry& geometry, const HardwareBuffer& packets, uint32_t count, const T& parameters,
                      IndirectFirstInstance firstInstance = IndirectFirstInstance::MayBeNonzero) {
        appendIndirect(geometry, packets, count, &parameters, sizeof(T), firstInstance);
    }
private:
    void appendIndirect(const RasterGeometry&, const HardwareBuffer&, uint32_t, const void*, size_t, IndirectFirstInstance);
    friend struct HardwareExecutor;
    friend struct RasterizerPipeline<>;
    RasterBatch(std::shared_ptr<horizon::detail::CommandRecordVulkan> record, const std::vector<ResourceUse>& uses)
        : record_(std::move(record)), uses_(uses) {}
    void append(const RasterGeometry&, const RasterDraw&, const void*, size_t);
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
    HardwareDisplayer(const HardwareDisplayer& other) = delete;
    HardwareDisplayer(HardwareDisplayer&& other) noexcept;
    ~HardwareDisplayer();

    HardwareDisplayer& operator=(const HardwareDisplayer& other) = delete;
    HardwareDisplayer& operator=(HardwareDisplayer&& other) noexcept;
    HardwareDisplayer& operator<<(const HardwareImage& image);
    // Commit pending work and present its output. A skipped presentation still commits the work.
    bool present(HardwareExecutor& pending, const HardwareImage& image);

    HardwareDisplayer& wait(const HardwareExecutor& executor);

    [[nodiscard]] uintptr_t getDisplayerID() const
    {
        return displaySurfaceID;
    }

private:
    std::uintptr_t displaySurfaceID{0};
    void* displaySurface{nullptr};
};

// ComputePipeline for EDSL shaders, Slang source, and compiled modules.
template <>
struct ComputePipeline<void>
{
public:
    ComputePipeline();
    ComputePipeline(const std::string& slangSource,
                    const std::source_location& sourceLocation = std::source_location::current());

    // 从当前编译器的 ShaderCodeModule（SPIR-V + Slang 反射）构造
    ComputePipeline(const EmbeddedShader::ShaderCodeModule& spirV,
                    const std::source_location& sourceLocation = std::source_location::current());

    // A binary alone lacks the reflection that these pipelines require.
    ComputePipeline(const std::vector<uint32_t>&,
                    const std::source_location& = std::source_location::current()) = delete;

    // 模板构造函数：接受 DSL lambda，内部完成编译
    template <typename F>
        requires std::invocable<F> && (!std::is_convertible_v<F, std::string>)
    ComputePipeline(F&& computeShaderCode,
                    ktm::uvec3 numthreads = ktm::uvec3(1),
                    EmbeddedShader::CompilerOption compilerOption = {},
                    std::source_location sourceLocation = std::source_location::current());

    ComputePipeline(const ComputePipeline& other) = delete;
    ComputePipeline(ComputePipeline&& other) noexcept;
    ~ComputePipeline();

    ComputePipeline& operator=(const ComputePipeline& other) = delete;
    ComputePipeline& operator=(ComputePipeline&& other) noexcept;

    // Set workgroup counts and read proxy bindings. The executor snapshots them at enqueue.
    ComputePipeline& operator()(uint16_t x, uint16_t y = 1, uint16_t z = 1);

    // Replace the declared resource uses; this does not bind or execute resources.
    ComputePipeline& uses(std::span<const ResourceUse> resources)
    {
        resourceUses_.assign(resources.begin(), resources.end());
        return *this;
    }
    ComputePipeline& uses(std::initializer_list<ResourceUse> resources)
    {
        resourceUses_.assign(resources.begin(), resources.end());
        return *this;
    }

    [[nodiscard]] uintptr_t getComputePipelineID() const
    {
        return reinterpret_cast<uintptr_t>(computePipeline_.get());
    }

private:
    friend struct HardwareExecutor;

    template <typename>
    friend struct EmbeddedShader::BoundField;

    void setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer& buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage& image, int32_t bindType);

    void initFromCompiler(EmbeddedShader::ShaderCodeCompiler& compiler, const std::source_location& src);
    horizon::detail::ComputePipelineVulkan& backend() const;
    std::shared_ptr<horizon::detail::ComputePipelineVulkan> computePipeline_;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries_;
    std::vector<ResourceUse> resourceUses_;
};

// RasterizerPipeline for EDSL shaders, Slang source, and compiled modules.
template <>
struct RasterizerPipeline<void, void>
{
public:
    RasterizerPipeline();
    RasterizerPipeline(std::string vertexSlang, std::string fragmentSlang, uint32_t multiviewCount = 1,
                       const std::source_location& sourceLocation = std::source_location::current());

    // 从当前编译器的 ShaderCodeModule（SPIR-V + Slang 反射）构造
    RasterizerPipeline(const EmbeddedShader::ShaderCodeModule& vertexSpirV,
                       const EmbeddedShader::ShaderCodeModule& fragmentSpirV,
                       uint32_t multiviewCount = 1,
                       const std::source_location& sourceLocation = std::source_location::current());

    RasterizerPipeline(const std::vector<uint32_t>&, const std::vector<uint32_t>&, uint32_t = 1,
                       const std::source_location& = std::source_location::current()) = delete;

    // 模板构造函数：接受 DSL lambda，内部完成编译
    template <typename VF, typename FF>
        requires(!std::is_convertible_v<VF, std::string>) && (!std::is_convertible_v<FF, std::string>) && (!std::is_same_v<std::remove_cvref_t<VF>, EmbeddedShader::ShaderCodeModule>) && (!std::is_same_v<std::remove_cvref_t<FF>, EmbeddedShader::ShaderCodeModule>) && (!std::is_same_v<std::remove_cvref_t<VF>, std::vector<uint32_t>>) && (!std::is_same_v<std::remove_cvref_t<FF>, std::vector<uint32_t>>)
    RasterizerPipeline(VF&& vertexShaderCode,
                       FF&& fragmentShaderCode,
                       uint32_t multiviewCount = 1,
                       EmbeddedShader::CompilerOption compilerOption = {},
                       std::source_location sourceLocation = std::source_location::current());

    RasterizerPipeline(const RasterizerPipeline& other) = delete;
    RasterizerPipeline(RasterizerPipeline&& other) noexcept;
    ~RasterizerPipeline();

    RasterizerPipeline& operator=(const RasterizerPipeline& other) = delete;
    RasterizerPipeline& operator=(RasterizerPipeline&& other) noexcept;

    void setDepthEnabled(bool enabled);
    void setRasterState(RasterState state);
    void setDepthImage(HardwareImage& depthImage);
    [[nodiscard]] HardwareImage getDepthImage();

    // Snapshot the current shader variant, attachments, parameters and resource bindings.
    [[nodiscard]] RasterBatch beginBatch(uint16_t width, uint16_t height, RasterPass pass = {}) {
        return capture(ktm::uvec2(width, height), pass);
    }
    // Use the first bound color attachment's extent, or the explicitly bound depth image.
    [[nodiscard]] RasterBatch beginBatch(RasterPass pass = {}) { return capture(std::nullopt, pass); }

    // Replace the declared resource uses; this does not bind or execute resources.
    RasterizerPipeline& uses(std::initializer_list<ResourceUse> resources)
    {
        resourceUses_.assign(resources.begin(), resources.end());
        return *this;
    }

    // 将 Texture2D proxy 注册为渲染目标（render target），在 dispatch 时自动绑定
    template <typename T>
    RasterizerPipeline& bindRenderTarget(uint32_t location, EmbeddedShader::Texture2DProxy<T>& proxy)
    {
        autoBindEntries_.push_back({ &proxy.boundResource_,
                                     0, 0,
                                     static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs),
                                     location });
        ++explicitTargetCount_;
        return *this;
    }

    // 批量绑定 render target
    template <typename... Ts>
    RasterizerPipeline& bindOutputTargets(EmbeddedShader::Texture2DProxy<Ts>&... targets)
    {
        uint32_t location = 0;
        (bindRenderTarget(location++, targets), ...);
        return *this;
    }

    [[nodiscard]] uintptr_t getRasterizerPipelineID() const
    {
        return reinterpret_cast<uintptr_t>(rasterizerPipeline_.get());
    }

private:
    friend struct HardwareExecutor;

    template <typename>
    friend struct EmbeddedShader::BoundField;

    void setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer& buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage& image, int32_t bindType, uint32_t location = 0);

    void initFromCompiler(EmbeddedShader::ShaderCodeCompiler& vertex, EmbeddedShader::ShaderCodeCompiler& fragment,
                          uint32_t views, const std::source_location& src);
    void refreshShaderVariant();
    RasterBatch capture(std::optional<ktm::uvec2> extent, RasterPass pass);
    horizon::detail::RasterizerPipelineVulkan& backend() const;
    std::shared_ptr<horizon::detail::RasterizerPipelineVulkan> rasterizerPipeline_;
    std::unique_ptr<EmbeddedShader::RasterizedPipelineObject> variants_;
    EmbeddedShader::ShaderCodeCompiler::ConditionInfo vertexConditions_, fragmentConditions_;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries_;
    size_t explicitTargetCount_{}; // The last entries override reflected outputs.
    std::vector<ResourceUse> resourceUses_;
};

// ================= 对外封装：HardwareExecutor =================
struct HardwareExecutor
{
public:
    HardwareExecutor();
    HardwareExecutor(const HardwareExecutor& other) = delete;
    HardwareExecutor(HardwareExecutor&& other) noexcept;
    ~HardwareExecutor();

    HardwareExecutor& operator=(const HardwareExecutor& other) = delete;
    HardwareExecutor& operator=(HardwareExecutor&& other) noexcept;

    HardwareExecutor& operator<<(ComputePipeline<>& computePipeline);
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
    std::uintptr_t executorID{0};
};

template<class Vertices, class Indices>
RasterGeometry RasterGeometry::upload(const Vertices& vertices, const Indices& indices) {
    const std::span vertexData(vertices);
    const std::span indexData(indices);
    using Vertex = typename decltype(vertexData)::value_type;
    using Index = typename decltype(indexData)::value_type;
    static_assert(std::is_trivially_copyable_v<Vertex> && sizeof(Vertex) % 4 == 0,
                  "Raster vertices must be trivially copyable with a stride divisible by four");
    static_assert(std::same_as<Index, uint16_t> || std::same_as<Index, uint32_t>,
                  "Raster indices must be uint16_t or uint32_t");

    HardwareExecutor executor;
    RasterGeometry geometry{
        HardwareBuffer::stageDeviceLocal(executor, vertexData.size(), sizeof(Vertex), BufferUsage::VertexBuffer, vertexData.data()),
        HardwareBuffer::stageDeviceLocal(executor, indexData.size(), sizeof(Index), BufferUsage::IndexBuffer, indexData.data())};
    executor.commit().waitForDeferredResources();
    return geometry;
}

namespace horizon::detail {
template<class VS, class FS>
using RasterResourceStage = std::conditional_t<(VS::uniformBufferBlockSize == 0 && FS::uniformBufferBlockSize != 0), FS, VS>;
}

template <typename VS, typename FS>
struct RasterizerPipeline
    : RasterizerPipeline<>,
      horizon::detail::RasterResourceStage<VS, FS>::template ResourceBindings<RasterizerPipeline<>>,
      FS::template OutputBindings<RasterizerPipeline<>>
{
    static_assert(!VS::uniformBufferBlockSize || !FS::uniformBufferBlockSize || VS::uniformBufferBlockSize == FS::uniformBufferBlockSize,
                  "VS and FS uniform buffer block sizes must match");

    using VSRes = typename horizon::detail::RasterResourceStage<VS, FS>::template ResourceBindings<RasterizerPipeline<>>;
    using FSOut = typename FS::template OutputBindings<RasterizerPipeline<>>;

    RasterizerPipeline(uint32_t multiviewCount = 1,
                       const std::source_location& sourceLocation = std::source_location::current())
        : RasterizerPipeline<>(
              compileLegacyShaderModule(EmbeddedShader::ShaderStage::VertexShader, VS::slangModule),
              compileLegacyShaderModule(EmbeddedShader::ShaderStage::FragmentShader, FS::slangModule),
              multiviewCount, sourceLocation), VSRes(this), FSOut(this)
    {
    }

    RasterizerPipeline(const RasterizerPipeline& other) = delete;

    RasterizerPipeline(RasterizerPipeline&& other) noexcept
        : RasterizerPipeline<>(std::move(other)), VSRes(this), FSOut(this)
    {
    }

    RasterizerPipeline& operator=(const RasterizerPipeline& other) = delete;

    RasterizerPipeline& operator=(RasterizerPipeline&& other) noexcept
    {
        RasterizerPipeline<>::operator=(std::move(other));
        return *this;
    }
};

template <class Vertex, class Fragment>
RasterizerPipeline(Vertex&&, Fragment&&, uint32_t = 1, EmbeddedShader::CompilerOption = {},
                   std::source_location = std::source_location::current()) -> RasterizerPipeline<>;
template <class... Args> requires std::constructible_from<RasterizerPipeline<>, Args...>
RasterizerPipeline(Args&&...) -> RasterizerPipeline<>;

template <typename CS>
struct ComputePipeline
    : ComputePipeline<>,
      CS::template Bindings<ComputePipeline<>>
{
    using CSBindings = typename CS::template Bindings<ComputePipeline<>>;

    ComputePipeline(const std::source_location& sourceLocation = std::source_location::current())
        : ComputePipeline<>(compileLegacyShaderModule(EmbeddedShader::ShaderStage::ComputeShader, CS::slangModule), sourceLocation), CSBindings(this)
    {
    }

    ComputePipeline(const ComputePipeline& other) = delete;

    ComputePipeline(ComputePipeline&& other) noexcept
        : ComputePipeline<>(std::move(other)), CSBindings(this)
    {
    }

    ComputePipeline& operator=(const ComputePipeline& other) = delete;

    ComputePipeline& operator=(ComputePipeline&& other) noexcept
    {
        ComputePipeline<>::operator=(std::move(other));
        return *this;
    }
};

template <class F> requires std::invocable<F>
ComputePipeline(F&&, ktm::uvec3 = ktm::uvec3(1), EmbeddedShader::CompilerOption = {},
                std::source_location = std::source_location::current()) -> ComputePipeline<>;
template <class... Args> requires std::constructible_from<ComputePipeline<>, Args...>
ComputePipeline(Args&&...) -> ComputePipeline<>;

// ================= BoundField::operator= Implementation =================
// Deferred from VariateProxy.h until the legacy pipeline types are complete.
namespace EmbeddedShader
{
    template <typename PipelineType>
    template <typename T>
    BoundField<PipelineType>& BoundField<PipelineType>::operator=(const T& value)
    {
        if (!pipeline_)
            return *this;

        if constexpr (std::is_same_v<std::remove_cvref_t<T>, HardwareImage>)
        {
            if constexpr (std::is_base_of_v<RasterizerPipeline<>, PipelineType>)
                pipeline_->setResourceDirect(byteOffset, typeSize, value, bindType, location);
            else
                pipeline_->setResourceDirect(byteOffset, typeSize, value, bindType);
        }
        else if constexpr (std::is_same_v<std::remove_cvref_t<T>, HardwareBuffer>)
        {
            pipeline_->setResourceDirect(byteOffset, typeSize, value, bindType);
        }
        else
        {
            pipeline_->setPushConstantDirect(byteOffset, &value, sizeof(T), bindType);
        }
        return *this;
    }
} // namespace EmbeddedShader

// ================= ComputePipeline 模板构造函数实现 =================
template <typename F>
    requires std::invocable<F> && (!std::is_convertible_v<F, std::string>)
ComputePipeline<>::ComputePipeline(F&& computeShaderCode,
                                   ktm::uvec3 numthreads,
                                   EmbeddedShader::CompilerOption compilerOption,
                                   std::source_location sourceLocation)
{
    // 使用 helicon 编译 DSL lambda 到着色器代码
    auto pipelineObj = EmbeddedShader::ComputePipelineObject::compile(
        std::forward<F>(computeShaderCode),
        numthreads,
        compilerOption,
        sourceLocation);

    // 保存自动绑定条目（从 EDSL proxy 回溯指针收集）
    autoBindEntries_ = std::move(pipelineObj.autoBindEntries);

    // 调用辅助函数完成 Vulkan 管线创建
    initFromCompiler(*pipelineObj.compute, sourceLocation);
}

// ================= RasterizerPipeline 模板构造函数实现 =================
template <typename VF, typename FF>
    requires(!std::is_convertible_v<VF, std::string>) && (!std::is_convertible_v<FF, std::string>) && (!std::is_same_v<std::remove_cvref_t<VF>, EmbeddedShader::ShaderCodeModule>) && (!std::is_same_v<std::remove_cvref_t<FF>, EmbeddedShader::ShaderCodeModule>) && (!std::is_same_v<std::remove_cvref_t<VF>, std::vector<uint32_t>>) && (!std::is_same_v<std::remove_cvref_t<FF>, std::vector<uint32_t>>)
RasterizerPipeline<>::RasterizerPipeline(VF&& vertexShaderCode,
                                         FF&& fragmentShaderCode,
                                         uint32_t multiviewCount,
                                         EmbeddedShader::CompilerOption compilerOption,
                                         std::source_location sourceLocation)
{
    // 使用 helicon 编译 DSL lambda 到着色器代码
    auto pipelineObj = EmbeddedShader::RasterizedPipelineObject::compile(
        std::forward<VF>(vertexShaderCode),
        std::forward<FF>(fragmentShaderCode),
        compilerOption,
        sourceLocation);

    // 保存自动绑定条目（从 EDSL proxy 回溯指针收集）
    autoBindEntries_ = std::move(pipelineObj.autoBindEntries);

    // 调用辅助函数完成 Vulkan 管线创建
    initFromCompiler(*pipelineObj.vertex, *pipelineObj.fragment, multiviewCount, sourceLocation);
    vertexConditions_ = pipelineObj.vertex->getCurrentConditionInfo();
    fragmentConditions_ = pipelineObj.fragment->getCurrentConditionInfo();
    if (!vertexConditions_.empty() || !fragmentConditions_.empty())
        variants_ = std::make_unique<EmbeddedShader::RasterizedPipelineObject>(std::move(pipelineObj));
}

namespace EmbeddedShader
{
    template <typename Type>
    void Texture2DProxy<Type>::createResource(const ::HardwareImageCreateInfo& createInfo)
    {
        ownedResource_ = std::make_unique<::HardwareImage>(createInfo);
        *this = *ownedResource_;
    }

    template <typename Type>
    void TextureCubeProxy<Type>::createResource(const ::HardwareImageCreateInfo& createInfo)
    {
        ownedResource_ = std::make_unique<::HardwareImage>(createInfo);
        *this = *ownedResource_;
    }
} // namespace EmbeddedShader
