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
#include "Codegen/RasterPipelineObject.h"
#include "Codegen/VariateProxy.h"
#include "Compiler/ShaderCodeCompiler.h"
#include <ktm/ktm.h>

struct HardwareExecutor
{
public:
    // Copying a command retains its captured resources and parameters for later enqueue.
    class Command
    {
        friend struct HardwareExecutor;
        friend struct HardwareBuffer;
        friend struct HardwareImage;
        friend struct HardwareDisplayer;
        template<class> friend struct ComputePipeline;
        template<class, class> friend struct RasterPipeline;

        struct Impl;
        explicit Command(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
        std::shared_ptr<Impl> impl_;
    };

    HardwareExecutor();
    HardwareExecutor(HardwareExecutor&& other) noexcept;
    ~HardwareExecutor();

    HardwareExecutor& operator=(HardwareExecutor&& other) noexcept;

    HardwareExecutor& operator<<(Command command);
    HardwareExecutor& commit();

    // Null or self waits for this executor's submitted work. Queued commands stay queued.
    // Another executor's latest successful submission becomes a dependency of the next commit.
    HardwareExecutor& wait(HardwareExecutor* other = nullptr);

private:
    friend struct HardwareBackendAccess;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};


// ================= 对外封装：HardwareBuffer =================
struct HardwareBuffer
{
public:
    enum class Usage : uint32_t {
        VertexBuffer = 1, IndexBuffer = 2, UniformBuffer = 4, StorageBuffer = 8, RasterIndirect = 16,
    };

    struct ExternalHandle {
#if defined(_WIN32)
        void* handle = nullptr;
#else
        int fd = -1;
#endif
    };

    HardwareBuffer();
    HardwareBuffer(const HardwareBuffer& other);
    HardwareBuffer(HardwareBuffer&& other) noexcept;
    HardwareBuffer(uint32_t bufferSize, uint32_t elementSize, Usage usage, const void* data = nullptr, bool useDedicated = true);

    HardwareBuffer(uint32_t size, Usage usage, const void* data = nullptr, bool useDedicated = true)
        : HardwareBuffer(1, size, usage, data, useDedicated)
    {
    }

    template <class R>
        requires std::ranges::contiguous_range<const R> && std::ranges::sized_range<const R> &&
                 std::is_trivially_copyable_v<std::ranges::range_value_t<const R>>
    HardwareBuffer(const R& input, Usage usage, bool useDedicated = true)
        : HardwareBuffer(std::ranges::size(input), sizeof(std::ranges::range_value_t<const R>),
                         usage, std::ranges::data(input), useDedicated)
    {
    }

    // Replace this buffer and return its upload command. The command retains the staged data and destination.
    [[nodiscard]] HardwareExecutor::Command createDeviceLocal(Usage usage, const void* data,
                                                              size_t elementCount, size_t elementSize);

    HardwareBuffer(const ExternalHandle& memHandle, uint32_t bufferSize, uint32_t elementSize, uint32_t allocSize, Usage usage);

    ~HardwareBuffer();

    HardwareBuffer& operator=(const HardwareBuffer& other);
    HardwareBuffer& operator=(HardwareBuffer&& other) noexcept;

    explicit operator bool() const;

    ExternalHandle exportBufferMemory();

    [[nodiscard]] uint32_t storeDescriptor() const;

    // 流式拷贝命令（用于 HardwareExecutor << ）
    [[nodiscard]] HardwareExecutor::Command copyTo(const HardwareBuffer& dst, uint64_t srcOffset = 0,
                                                   uint64_t dstOffset = 0, uint64_t size = 0) const;
    [[nodiscard]] HardwareExecutor::Command copyTo(const HardwareImage& dst, uint64_t bufferOffset = 0,
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

private:
    static HardwareBuffer rasterUpload(uint32_t bytes);
    struct Impl;
    Impl* impl_{};
    friend struct ResourceManager;
    friend class HardwareExecutor::Command;
};

// ================= 对外封装：HardwareImage =================
struct HardwareImage
{
public:
    enum class Format : uint32_t {
        RGBA8_UINT, RGBA8_SINT, RGBA8_SRGB,
        RGBA16_UINT, RGBA16_SINT, RGBA16_FLOAT,
        RGBA32_UINT, RGBA32_SINT, RGBA32_FLOAT, RG32_FLOAT,
        D16_UNORM, D32_FLOAT,
        BC1_RGB_UNORM, BC1_RGB_SRGB,
        BC2_RGBA_UNORM, BC2_RGBA_SRGB,
        BC3_RGBA_UNORM, BC3_RGBA_SRGB,
        BC4_R_UNORM, BC4_R_SNORM,
        BC5_RG_UNORM, BC5_RG_SNORM,
        ASTC_4x4_UNORM, ASTC_4x4_SRGB,
        RGBA8_UNORM, R32_FLOAT,
        BC7_UNORM, BC7_SRGB,
    };

    enum class Usage : uint32_t {
        SampledImage = 1, StorageImage = 2, DepthImage = 3, SampledStorageImage = 4,
    };

    struct CreateInfo {
        uint32_t width{}, height{};
        Format format = Format::RGBA8_SRGB;
        Usage usage = Usage::SampledImage;
        int arrayLayers{1}, mipLevels{1};
        bool cube{}, linearFiltering{}; // Linear filtering covers min/mag/mip with supported anisotropy.
    };

    HardwareImage();
    HardwareImage(const HardwareImage& other);
    HardwareImage(HardwareImage&& other) noexcept;
    HardwareImage(uint32_t width, uint32_t height, Format format, Usage usage = Usage::SampledImage, int arrayLayers = 1);
    HardwareImage(const CreateInfo& createInfo);

    ~HardwareImage();

    HardwareImage& operator=(const HardwareImage& other);
    HardwareImage& operator=(HardwareImage&& other) noexcept;
    // image[layer][mip]
    HardwareImage operator[](const uint32_t index);
    explicit operator bool() const;

    [[nodiscard]] uint32_t storeDescriptor() const;
    [[nodiscard]] uint32_t storeSampledDescriptor() const;

    /// Set the clear color used by the RasterPipeline render pass (LOAD_OP_CLEAR).
    /// Default is (0, 0, 0, 1).  For transparent render targets, use (0, 0, 0, 0).
    void setClearColor(float r, float g, float b, float a);

    // 流式拷贝命令（用于 HardwareExecutor << ）
    [[nodiscard]] HardwareExecutor::Command copyTo(const HardwareImage& dst, uint32_t srcLayer = 0,
                                                   uint32_t dstLayer = 0, uint32_t srcMip = 0, uint32_t dstMip = 0) const;
    [[nodiscard]] HardwareExecutor::Command copyTo(const HardwareBuffer& dst, uint32_t imageLayer = 0,
                                                   uint32_t imageMip = 0, uint64_t bufferOffset = 0) const;
    [[nodiscard]] HardwareExecutor::Command copyFrom(const void* inputData, uint32_t imageLayer = 0,
                                                     uint32_t imageMip = 0) const;

private:
    friend struct ResourceManager;
    struct Impl;
    Impl* impl_{};
};

struct HardwareDisplayer
{
public:
    // Prefer the render device when it can present to the surface.
    explicit HardwareDisplayer(void* surface = nullptr);
    HardwareDisplayer(HardwareDisplayer&& other) noexcept;
    ~HardwareDisplayer();

    HardwareDisplayer& operator=(HardwareDisplayer&& other) noexcept;
    // Each execution presents the current contents of the captured image. The native window must remain valid.
    [[nodiscard]] HardwareExecutor::Command operator()(const HardwareImage& image) const;

private:
    friend class HardwareExecutor::Command;
    struct Impl;
    std::shared_ptr<Impl> impl_;
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

template<class CS = void>
struct ComputePipeline
{
    static_assert(std::same_as<CS, void>, "Include the generated compute shader header for this stage");

private:
    template<class> friend struct ComputePipeline;
    friend struct EmbeddedShader::BoundField<ComputePipeline>;

    explicit ComputePipeline(const EmbeddedShader::ShaderCodeModule& module);
    explicit ComputePipeline(EmbeddedShader::ComputePipelineObject shader);

    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    ComputePipeline();

    template<class F>
        requires std::invocable<F>
    ComputePipeline(F&& shader, ktm::uvec3 numthreads = ktm::uvec3(1), EmbeddedShader::CompilerOption options = {},
                    const std::source_location& location = std::source_location::current())
        : ComputePipeline(EmbeddedShader::ComputePipelineObject::compile(
              std::forward<F>(shader), numthreads, options, location)) {}

    ~ComputePipeline();
    ComputePipeline(ComputePipeline&&) noexcept;
    ComputePipeline& operator=(ComputePipeline&&) noexcept;

    [[nodiscard]] HardwareExecutor::Command operator()(uint16_t x, uint16_t y = 1, uint16_t z = 1);

    ComputePipeline& uses(std::span<const ResourceUse> resources);
    ComputePipeline& uses(std::initializer_list<ResourceUse> resources) { return uses(std::span(resources)); }
};

extern template struct ComputePipeline<void>;

template<class VS = void, class FS = void>
struct RasterPipeline
{
    static_assert(std::same_as<VS, void> && std::same_as<FS, void>,
                  "Register this VS/FS pair in RASTER_PAIRS and include both stage headers or the generated pipeline header");

private:
    template<class, class> friend struct RasterPipeline;
    friend struct EmbeddedShader::BoundField<RasterPipeline>;

    RasterPipeline(const EmbeddedShader::ShaderCodeModule& vertex,
                   const EmbeddedShader::ShaderCodeModule& fragment, uint32_t views);
    RasterPipeline(EmbeddedShader::RasterPipelineObject shaders, uint32_t views);

    RasterPipeline& bindRenderTarget(uint32_t location, void** resource);
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    RasterPipeline();

    template<class V, class F>
        requires requires(V& vertex, F& fragment) { std::function{vertex}; std::function{fragment}; }
    RasterPipeline(V&& vertex, F&& fragment, EmbeddedShader::CompilerOption options = {}, uint32_t views = 1,
                   std::source_location location = std::source_location::current())
        : RasterPipeline(EmbeddedShader::RasterPipelineObject::compile(
              std::forward<V>(vertex), std::forward<F>(fragment), options, location), views) {}

    ~RasterPipeline();
    RasterPipeline(RasterPipeline&&) noexcept;
    RasterPipeline& operator=(RasterPipeline&&) noexcept;

    void setRasterState(RasterState state);
    void setDepthImage(HardwareImage& image);
    [[nodiscard]] HardwareImage getDepthImage();

    // Capture the current bindings for an indirect draw of the first count packets.
    // An explicit pass starts a new drawIndex sequence and applies its attachment load/clear operations,
    // even with zero draws. Adjacent draws on the same attachments and extent otherwise continue the pass.
    [[nodiscard]] HardwareExecutor::Command operator()(
        const HardwareBuffer& vertices, const HardwareBuffer& indices, const HardwareBuffer& packets, uint32_t count,
        const HardwareBuffer& parameters = {}, std::optional<ScissorRect> scissor = {},
        std::optional<RasterPass> pass = {}, std::optional<ktm::uvec2> extent = {});

    RasterPipeline& uses(std::initializer_list<ResourceUse> resources);

    template<class T>
    RasterPipeline& bindRenderTarget(uint32_t location, EmbeddedShader::Texture2DProxy<T>& proxy)
    {
        return bindRenderTarget(location, &proxy.boundResource_);
    }
};

extern template struct RasterPipeline<void, void>;

template<class VS, class FS> requires requires { VS::shaderModule; FS::shaderModule; }
RasterPipeline(const VS&, const FS&, uint32_t = 1) -> RasterPipeline<VS, FS>;
