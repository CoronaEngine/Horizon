#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <type_traits>
#include <vector>

#include <ktm/ktm.h>
#include "Compiler/ShaderCodeCompiler.h"
#include "Codegen/ComputePipelineObject.h"
#include "Codegen/RasterizedPipelineObject.h"
#include "Codegen/VariateProxy.h"
#include "HardwareCommands.h"

// Forward declare platform-specific types instead of including platform headers
#if defined(_WIN32)
// Forward declare HANDLE without including Windows.h
#if !defined(_WINDEF_)
typedef void *HANDLE;
#endif
#endif

struct HardwareExecutor;

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
    ASTC_4x4_SRGB
};

enum class ImageUsage : uint32_t
{
    SampledImage = 1,
    StorageImage = 2,
    DepthImage = 3,
};

enum class BufferUsage : uint32_t
{
    VertexBuffer = 1,
    IndexBuffer = 2,
    UniformBuffer = 4,
    StorageBuffer = 8,
};

template <typename T>
concept IsContainer = requires(T a) {
    { a.size() } -> std::convertible_to<size_t>;
    { a.data() };
    { a[0] };
};

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
    HardwareBuffer(const HardwareBuffer &other);
    HardwareBuffer(HardwareBuffer &&other) noexcept;
    HardwareBuffer(uint32_t bufferSize, uint32_t elementSize, BufferUsage usage, const void *data = nullptr, bool useDedicated = true);

    HardwareBuffer(uint32_t size, BufferUsage usage, const void *data = nullptr, bool useDedicated = true)
        : HardwareBuffer(1, size, usage, data, useDedicated)
    {
    }

    template <IsContainer Container>
    HardwareBuffer(const Container &input, BufferUsage usage, bool useDedicated = true)
        : HardwareBuffer(input.size(), sizeof(input[0]), usage, input.data(), useDedicated)
    {
    }

    HardwareBuffer(const ExternalHandle &memHandle, uint32_t bufferSize, uint32_t elementSize, uint32_t allocSize, BufferUsage usage);

    ~HardwareBuffer();

    HardwareBuffer &operator=(const HardwareBuffer &other);
    HardwareBuffer &operator=(HardwareBuffer &&other) noexcept;

    explicit operator bool() const;

    ExternalHandle exportBufferMemory();
    // HardwareBuffer importBufferMemory(const ExternalHandle &memHandle);

    [[nodiscard]] uint32_t storeDescriptor() const;

    // 流式拷贝命令（用于 HardwareExecutor << ）
    [[nodiscard]] BufferCopyCommand copyTo(const HardwareBuffer &dst,
                                           uint64_t srcOffset = 0,
                                           uint64_t dstOffset = 0,
                                           uint64_t size = 0) const;
    [[nodiscard]] BufferToImageCommand copyTo(const HardwareImage &dst,
                                              uint64_t bufferOffset = 0,
                                              uint32_t imageLayer = 0,
                                              uint32_t imageMip = 0) const;

    // CPU 映射内存操作（非 GPU 命令）
    bool copyFromData(const void *inputData, uint64_t size) const;
    bool copyToData(void *outputData, uint64_t size) const;

    template <typename T>
    bool copyFromVector(const std::vector<T> &input)
    {
        return copyFromData(input.data(), input.size() * sizeof(T));
    }

    [[nodiscard]] void *getMappedData() const;
    [[nodiscard]] uint64_t getElementSize() const;
    [[nodiscard]] uint64_t getElementCount() const;

    [[nodiscard]] uintptr_t getBufferID() const
    {
        return bufferID.load(std::memory_order_acquire);
    }

  private:
    std::atomic<std::uintptr_t> bufferID;
    mutable std::mutex bufferMutex;

    friend class HardwareImage;
};

// ================= HardwareImage 参数结构体 =================
struct HardwareImageCreateInfo
{
    uint32_t width{0};
    uint32_t height{0};
    ImageFormat format = ImageFormat::RGBA8_SRGB;
    ImageUsage usage = ImageUsage::SampledImage;
    int arrayLayers{1};
    int mipLevels{1};
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
    HardwareImage(const HardwareImage &other);
    HardwareImage(HardwareImage &&other) noexcept;
    HardwareImage(uint32_t width, uint32_t height, ImageFormat imageFormat, ImageUsage imageUsage = ImageUsage::SampledImage, int arrayLayers = 1, void *imageData = nullptr);
    HardwareImage(const HardwareImageCreateInfo &createInfo);

    ~HardwareImage();

    HardwareImage &operator=(const HardwareImage &other);
    HardwareImage &operator=(HardwareImage &&other) noexcept;
    // image[layer][mip]
    HardwareImage operator[](const uint32_t index);
    explicit operator bool() const;

    [[nodiscard]] uint32_t storeDescriptor();
    [[nodiscard]] uintptr_t getImageID() const
    {
        return imageID.load(std::memory_order_acquire);
    }

    /// Set the clear color used by the RasterizerPipeline render pass (LOAD_OP_CLEAR).
    /// Default is (0, 0, 0, 1).  For transparent render targets, use (0, 0, 0, 0).
    void setClearColor(float r, float g, float b, float a);

    // 流式拷贝命令（用于 HardwareExecutor << ）
    [[nodiscard]] ImageCopyCommand copyTo(const HardwareImage &dst,
                                          uint32_t srcLayer = 0, uint32_t dstLayer = 0,
                                          uint32_t srcMip = 0, uint32_t dstMip = 0) const;
    [[nodiscard]] ImageToBufferCommand copyTo(const HardwareBuffer &dst,
                                              uint32_t imageLayer = 0,
                                              uint32_t imageMip = 0,
                                              uint64_t bufferOffset = 0) const;
    [[nodiscard]] BufferToImageCommand copyFrom(const void *inputData,
                                                uint32_t imageLayer = 0,
                                                uint32_t imageMip = 0) const;

    //[[nodiscard]] uint32_t getNumMipLevels() const;
    //[[nodiscard]] uint32_t getArrayLayers() const;

  private:
    std::atomic<std::uintptr_t> imageID;
    mutable std::mutex imageMutex;

    friend class HardwareDisplayer;
};

// ================= 对外封装：HardwarePushConstant =================
struct HardwarePushConstant
{
  public:
    HardwarePushConstant();
    HardwarePushConstant(const HardwarePushConstant &other);
    HardwarePushConstant(HardwarePushConstant &&other) noexcept;

    template <typename T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, HardwarePushConstant>)
    explicit HardwarePushConstant(T data)
    {
        copyFromRaw(&data, sizeof(T));
    }

    // NOTE: the sub and whole must in the same thread
    HardwarePushConstant(uint64_t size, uint64_t offset, HardwarePushConstant *whole = nullptr);

    ~HardwarePushConstant();

    HardwarePushConstant &operator=(const HardwarePushConstant &other);
    HardwarePushConstant &operator=(HardwarePushConstant &&other) noexcept;

    template <typename T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, HardwarePushConstant>)
    HardwarePushConstant &operator=(const T &data)
    {
        copyFromRaw(&data, sizeof(T));
        return *this;
    }

    template <typename T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, HardwarePushConstant>)
    HardwarePushConstant &operator=(T &&data)
    {
        copyFromRaw(&data, sizeof(T));
        return *this;
    }

    // NOTE: must in the same thread
    [[nodiscard]] uint8_t *getData() const;
    [[nodiscard]] uint64_t getSize() const;
    [[nodiscard]] uintptr_t getPushConstantID() const
    {
        return pushConstantID.load(std::memory_order_acquire);
    }

  private:
    void copyFromRaw(const void *src, uint64_t size);

    std::atomic<std::uintptr_t> pushConstantID;
    mutable std::mutex pushConstantMutex;
};

// Forward declarations
struct ComputePipelineBase;
struct RasterizerPipelineBase;

enum class IndexType : uint32_t
{
    Auto = 0,
    UInt16 = 1,
    UInt32 = 2,
};

struct ScissorRect
{
    int32_t x{0};
    int32_t y{0};
    uint32_t width{0};
    uint32_t height{0};
};

struct DrawIndexedParams
{
    uint32_t indexCount{0};
    uint32_t firstIndex{0};
    int32_t vertexOffset{0};
    IndexType indexType = IndexType::Auto;
    bool enableScissor{false};
    ScissorRect scissor{};
};

// ================= 资源代理类：ResourceProxy =================
// 内部实现细节，用于支持 pipeline[key] = value 语法
struct ResourceProxy
{
  private:
    ComputePipelineBase *compute_pipeline_{nullptr};
    RasterizerPipelineBase *rasterizer_pipeline_{nullptr};

    // Direct-access metadata (from BindingKey)
    uint64_t byte_offset_{0};
    uint32_t type_size_{0};
    int32_t  bind_type_{-1};
    uint32_t location_{0};

  public:
    ResourceProxy(ComputePipelineBase *p, uint64_t offset, uint32_t size, int32_t type, uint32_t loc)
        : compute_pipeline_(p),
          byte_offset_(offset), type_size_(size), bind_type_(type), location_(loc)
    {
    }

    ResourceProxy(RasterizerPipelineBase *p, uint64_t offset, uint32_t size, int32_t type, uint32_t loc)
        : rasterizer_pipeline_(p),
          byte_offset_(offset), type_size_(size), bind_type_(type), location_(loc)
    {
    }

    template <typename T>
    ResourceProxy &operator=(const T &value);
};

// ================= 对外封装：HardwareDisplayer =================
struct HardwareDisplayer
{
  public:
    explicit HardwareDisplayer(void *surface = nullptr);
    HardwareDisplayer(const HardwareDisplayer &other);
    HardwareDisplayer(HardwareDisplayer &&other) noexcept;
    ~HardwareDisplayer();

    HardwareDisplayer &operator=(const HardwareDisplayer &other);
    HardwareDisplayer &operator=(HardwareDisplayer &&other) noexcept;
    HardwareDisplayer &operator<<(const HardwareImage &image);

    HardwareDisplayer &wait(const HardwareExecutor &executor);

    [[nodiscard]] uintptr_t getDisplayerID() const
    {
        return displaySurfaceID.load(std::memory_order_acquire);
    }

  private:
    std::atomic<std::uintptr_t> displaySurfaceID;
    mutable std::mutex displayerMutex;
};

// ================= 对外封装：ComputePipelineBase =================
struct ComputePipelineBase
{
  public:
    ComputePipelineBase();
    ComputePipelineBase(const std::string &shaderCode,
                    EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
                    const std::source_location &sourceLocation = std::source_location::current());

    // 从预编译 SPIR-V 二进制构造（配合 #include GLSL(xxx) 生成的 spirv 变量使用）
    ComputePipelineBase(const std::vector<uint32_t> &spirV,
                    const std::source_location &sourceLocation = std::source_location::current());

    // 模板构造函数：接受 DSL lambda，内部完成编译
    template <typename F>
        requires std::invocable<F> && (!std::is_convertible_v<F, std::string>)
    ComputePipelineBase(F &&computeShaderCode,
                    ktm::uvec3 numthreads = ktm::uvec3(1),
                    EmbeddedShader::CompilerOption compilerOption = {},
                    std::source_location sourceLocation = std::source_location::current());

    ComputePipelineBase(const ComputePipelineBase &other);
    ComputePipelineBase(ComputePipelineBase &&other) noexcept;
    ~ComputePipelineBase();

    ComputePipelineBase &operator=(const ComputePipelineBase &other);
    ComputePipelineBase &operator=(ComputePipelineBase &&other) noexcept;

    ComputePipelineBase &operator()(uint16_t x, uint16_t y, uint16_t z);

    [[nodiscard]] uintptr_t getComputePipelineID() const
    {
        return computePipelineID.load(std::memory_order_acquire);
    }

    template<typename ProxyType>
        requires requires(const ProxyType& t) { t.byteOffset; t.typeSize; t.bindType; t.location; }
    ResourceProxy operator[](const ProxyType& proxy)
    {
        return ResourceProxy(this, proxy.byteOffset, proxy.typeSize, proxy.bindType, proxy.location);
    }

  private:
    friend struct ResourceProxy;


    void setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType);

    mutable std::mutex computePipelineMutex;
    std::atomic<std::uintptr_t> computePipelineID;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries_;
};

// ================= 对外封装：RasterizerPipelineBase =================
struct RasterizerPipelineBase
{
  public:
    RasterizerPipelineBase();
    RasterizerPipelineBase(std::string vertexShaderCode,
                       std::string fragmentShaderCode,
                       uint32_t multiviewCount = 1,
                       EmbeddedShader::ShaderLanguage vertexShaderLanguage = EmbeddedShader::ShaderLanguage::GLSL,
                       EmbeddedShader::ShaderLanguage fragmentShaderLanguage = EmbeddedShader::ShaderLanguage::GLSL,
                       const std::source_location &sourceLocation = std::source_location::current());

    // 从预编译 SPIR-V 二进制构造（配合 #include GLSL(xxx) 生成的 spirv 变量使用）
    RasterizerPipelineBase(const std::vector<uint32_t> &vertexSpirV,
                       const std::vector<uint32_t> &fragmentSpirV,
                       uint32_t multiviewCount = 1,
                       const std::source_location &sourceLocation = std::source_location::current());

    // 模板构造函数：接受 DSL lambda，内部完成编译
    template <typename VF, typename FF>
        requires (!std::is_convertible_v<VF, std::string>) && (!std::is_convertible_v<FF, std::string>)
              && (!std::is_same_v<std::remove_cvref_t<VF>, std::vector<uint32_t>>)
              && (!std::is_same_v<std::remove_cvref_t<FF>, std::vector<uint32_t>>)
    RasterizerPipelineBase(VF &&vertexShaderCode,
                       FF &&fragmentShaderCode,
                       uint32_t multiviewCount = 1,
                       EmbeddedShader::CompilerOption compilerOption = {},
                       std::source_location sourceLocation = std::source_location::current());

    RasterizerPipelineBase(const RasterizerPipelineBase &other);
    RasterizerPipelineBase(RasterizerPipelineBase &&other) noexcept;
    ~RasterizerPipelineBase();

    RasterizerPipelineBase &operator=(const RasterizerPipelineBase &other);
    RasterizerPipelineBase &operator=(RasterizerPipelineBase &&other) noexcept;

    void setDepthEnabled(bool enabled);
    //void setDepthWriteEnabled(bool enabled);
    void setDepthImage(HardwareImage &depthImage);
    [[nodiscard]] HardwareImage getDepthImage();

    // 通过 shader 反射键绑定资源（BindingKey 由 GLSL 编译生成的 .hpp 提供）
    template<typename ProxyType>
        requires requires(const ProxyType& t) { t.byteOffset; t.typeSize; t.bindType; t.location; }
    ResourceProxy operator[](const ProxyType& proxy)
    {
        return ResourceProxy(this, proxy.byteOffset, proxy.typeSize, proxy.bindType, proxy.location);
    }

    RasterizerPipelineBase &operator()(uint16_t width, uint16_t height);
    RasterizerPipelineBase &record(const HardwareBuffer &indexBuffer, const HardwareBuffer &vertexBuffer);
    RasterizerPipelineBase &record(const HardwareBuffer &indexBuffer, const HardwareBuffer &vertexBuffer, const DrawIndexedParams &params);

    // 将 Texture2D proxy 注册为渲染目标（render target），在 dispatch 时自动绑定
    template<typename T>
    RasterizerPipelineBase& bindRenderTarget(uint32_t location, EmbeddedShader::Texture2DProxy<T>& proxy)
    {
        autoBindEntries_.push_back({
            &proxy.boundResource_,
            0, 0,
            static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs),
            location
        });
        return *this;
    }

    // 批量绑定 render target
    template<typename... Ts>
    RasterizerPipelineBase& bindOutputTargets(EmbeddedShader::Texture2DProxy<Ts>&... targets)
    {
        uint32_t location = 0;
        (bindRenderTarget(location++, targets), ...);
        return *this;
    }

    [[nodiscard]] uintptr_t getRasterizerPipelineID() const
    {
        return rasterizerPipelineID.load(std::memory_order_acquire);
    }

  private:
    friend struct ResourceProxy;

    void setPushConstantDirect(uint64_t byteOffset, const void *data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer &buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage &image, int32_t bindType, uint32_t location = 0);

    mutable std::mutex rasterizerPipelineMutex;
    std::atomic<std::uintptr_t> rasterizerPipelineID;
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries_;
};

// ================= 对外封装：HardwareExecutor =================
struct HardwareExecutor
{
  public:
    HardwareExecutor();
    HardwareExecutor(const HardwareExecutor &other);
    HardwareExecutor(HardwareExecutor &&other) noexcept;
    ~HardwareExecutor();

    HardwareExecutor &operator=(const HardwareExecutor &other);
    HardwareExecutor &operator=(HardwareExecutor &&other) noexcept;

    HardwareExecutor &operator<<(ComputePipelineBase &computePipeline);
    HardwareExecutor &operator<<(RasterizerPipelineBase &rasterizerPipeline);
    HardwareExecutor &operator<<(HardwareExecutor &other);
    HardwareExecutor &operator<<(const CopyCommand &cmd);

    HardwareExecutor &wait(HardwareExecutor &other);
    HardwareExecutor &commit();

    // ========== 延迟释放相关接口 ==========
    /// @brief 等待所有延迟释放的资源完成（阻塞）
    void waitForDeferredResources();

    /// @brief 手动触发一次清理（非阻塞）
    void cleanupDeferredResources();

    [[nodiscard]] uintptr_t getExecutorID() const
    {
        return executorID.load(std::memory_order_acquire);
    }

  private:
    mutable std::mutex executorMutex;
    std::atomic<std::uintptr_t> executorID;
};

// ================= ResourceProxy Implementation =================

template <typename T>
ResourceProxy &ResourceProxy::operator=(const T &value)
{
    if constexpr (std::is_same_v<std::remove_cvref_t<T>, HardwareImage>)
    {
        if (compute_pipeline_)
            compute_pipeline_->setResourceDirect(byte_offset_, type_size_, value, bind_type_);
        if (rasterizer_pipeline_)
            rasterizer_pipeline_->setResourceDirect(byte_offset_, type_size_, value, bind_type_, location_);
    }
    else if constexpr (std::is_same_v<std::remove_cvref_t<T>, HardwareBuffer>)
    {
        if (compute_pipeline_)
            compute_pipeline_->setResourceDirect(byte_offset_, type_size_, value, bind_type_);
        if (rasterizer_pipeline_)
            rasterizer_pipeline_->setResourceDirect(byte_offset_, type_size_, value, bind_type_);
    }
    else if constexpr (!std::is_same_v<std::remove_cvref_t<T>, ResourceProxy>)
    {
        if (compute_pipeline_)
            compute_pipeline_->setPushConstantDirect(byte_offset_, &value, sizeof(T), bind_type_);
        if (rasterizer_pipeline_)
            rasterizer_pipeline_->setPushConstantDirect(byte_offset_, &value, sizeof(T), bind_type_);
    }
    return *this;
}

// ================= BoundField::operator= Implementation =================
// Deferred from VariateProxy.h — now ResourceProxy is fully defined.
namespace EmbeddedShader {
template<typename PipelineType>
template<typename T>
BoundField<PipelineType>& BoundField<PipelineType>::operator=(const T& value)
{
    ResourceProxy proxy(pipeline_, byteOffset, typeSize, bindType, location);
    proxy = value;
    return *this;
}
} // namespace EmbeddedShader

// ================= ComputePipeline 模板构造函数实现 =================
// 需要访问 Storage 和 ComputePipelineVulkan，通过辅助函数实现
void computePipelineInitFromCompiler(std::atomic<std::uintptr_t> &pipelineID, 
                                      const EmbeddedShader::ShaderCodeCompiler &compiler,
                                      const std::source_location &src);

template <typename F>
    requires std::invocable<F> && (!std::is_convertible_v<F, std::string>)
ComputePipelineBase::ComputePipelineBase(F &&computeShaderCode,
                                  ktm::uvec3 numthreads,
                                  EmbeddedShader::CompilerOption compilerOption,
                                  std::source_location sourceLocation)
{
    // 使用 helicon 编译 DSL lambda 到着色器代码
    auto pipelineObj = EmbeddedShader::ComputePipelineObject::compile(
        std::forward<F>(computeShaderCode),
        numthreads,
        compilerOption,
        sourceLocation
    );

    // 保存自动绑定条目（从 EDSL proxy 回溯指针收集）
    autoBindEntries_ = std::move(pipelineObj.autoBindEntries);

    // 调用辅助函数完成 Vulkan 管线创建
    computePipelineInitFromCompiler(computePipelineID, *pipelineObj.compute, sourceLocation);
}

// ================= RasterizerPipeline 模板构造函数实现 =================
void rasterizerPipelineInitFromCompiler(std::atomic<std::uintptr_t> &pipelineID,
                                         const EmbeddedShader::ShaderCodeCompiler &vertexCompiler,
                                         const EmbeddedShader::ShaderCodeCompiler &fragmentCompiler,
                                         uint32_t multiviewCount,
                                         const std::source_location &src);

template <typename VF, typename FF>
    requires (!std::is_convertible_v<VF, std::string>) && (!std::is_convertible_v<FF, std::string>)
          && (!std::is_same_v<std::remove_cvref_t<VF>, std::vector<uint32_t>>)
          && (!std::is_same_v<std::remove_cvref_t<FF>, std::vector<uint32_t>>)
RasterizerPipelineBase::RasterizerPipelineBase(VF &&vertexShaderCode,
                                        FF &&fragmentShaderCode,
                                        uint32_t multiviewCount,
                                        EmbeddedShader::CompilerOption compilerOption,
                                        std::source_location sourceLocation)
{
    // 使用 helicon 编译 DSL lambda 到着色器代码
    auto pipelineObj = EmbeddedShader::RasterizedPipelineObject::compile(
        std::forward<VF>(vertexShaderCode),
        std::forward<FF>(fragmentShaderCode),
        compilerOption,
        sourceLocation
    );

    // 保存自动绑定条目（从 EDSL proxy 回溯指针收集）
    autoBindEntries_ = std::move(pipelineObj.autoBindEntries);

    // 调用辅助函数完成 Vulkan 管线创建
    rasterizerPipelineInitFromCompiler(rasterizerPipelineID, 
                                        *pipelineObj.vertex, 
                                        *pipelineObj.fragment, 
                                        multiviewCount,
                                        sourceLocation);
}

// ================= RasterizerPipeline: unified template =================
// EDSL path:  RasterizerPipeline rasterizer(vsLambda, fsLambda);          // deduces <void, void>
// GLSL path:  RasterizerPipeline<vert_glsl, frag_glsl> rasterizer;        // direct member access
template<typename VS = void, typename FS = void>
struct RasterizerPipeline;

// void specialization — EDSL / raw GLSL string path (inherits all Base constructors)
template<>
struct RasterizerPipeline<void, void> : RasterizerPipelineBase
{
    using RasterizerPipelineBase::RasterizerPipelineBase;
};

// General template — GLSL code-gen path with direct member access
template<typename VS, typename FS>
struct RasterizerPipeline
    : RasterizerPipelineBase
    , VS::template ResourceBindings<RasterizerPipelineBase>
    , FS::template OutputBindings<RasterizerPipelineBase>
{
    static_assert(VS::pushConstantBlockSize == FS::pushConstantBlockSize,
        "VS and FS push constant block sizes must match");
    static_assert(VS::uniformBufferBlockSize == FS::uniformBufferBlockSize,
        "VS and FS uniform buffer block sizes must match");

    using VSRes = typename VS::template ResourceBindings<RasterizerPipelineBase>;
    using FSOut = typename FS::template OutputBindings<RasterizerPipelineBase>;

    RasterizerPipeline(uint32_t multiviewCount = 1,
                       const std::source_location& sourceLocation = std::source_location::current())
        : RasterizerPipelineBase(VS::spirv, FS::spirv, multiviewCount, sourceLocation)
        , VSRes(static_cast<RasterizerPipelineBase*>(this))
        , FSOut(static_cast<RasterizerPipelineBase*>(this))
    {}

    RasterizerPipeline(const RasterizerPipeline& other)
        : RasterizerPipelineBase(other)
        , VSRes(static_cast<RasterizerPipelineBase*>(this))
        , FSOut(static_cast<RasterizerPipelineBase*>(this))
    {}

    RasterizerPipeline(RasterizerPipeline&& other) noexcept
        : RasterizerPipelineBase(std::move(other))
        , VSRes(static_cast<RasterizerPipelineBase*>(this))
        , FSOut(static_cast<RasterizerPipelineBase*>(this))
    {}

    RasterizerPipeline& operator=(const RasterizerPipeline& other)
    {
        RasterizerPipelineBase::operator=(other);
        return *this;
    }

    RasterizerPipeline& operator=(RasterizerPipeline&& other) noexcept
    {
        RasterizerPipelineBase::operator=(std::move(other));
        return *this;
    }
};

// Deduction guides: EDSL / string / SPIR-V paths deduce to RasterizerPipeline<void, void>
template<typename VF, typename FF>
RasterizerPipeline(VF&&, FF&&, uint32_t = 1, EmbeddedShader::CompilerOption = {}, std::source_location = std::source_location::current())
    -> RasterizerPipeline<>;
RasterizerPipeline(std::string, std::string, uint32_t, EmbeddedShader::ShaderLanguage, EmbeddedShader::ShaderLanguage, const std::source_location&)
    -> RasterizerPipeline<>;
RasterizerPipeline(const std::vector<uint32_t>&, const std::vector<uint32_t>&, uint32_t, const std::source_location&)
    -> RasterizerPipeline<>;

// ================= ComputePipeline: unified template =================
// EDSL path:  ComputePipeline computer(csLambda, numthreads);             // deduces <void>
// GLSL path:  ComputePipeline<compute_glsl> computer;                     // direct member access
template<typename CS = void>
struct ComputePipeline;

// void specialization — EDSL / raw GLSL string path
template<>
struct ComputePipeline<void> : ComputePipelineBase
{
    using ComputePipelineBase::ComputePipelineBase;
};

// General template — GLSL code-gen path with direct member access
template<typename CS>
struct ComputePipeline
    : ComputePipelineBase
    , CS::template Bindings<ComputePipelineBase>
{
    using CSBindings = typename CS::template Bindings<ComputePipelineBase>;

    ComputePipeline(const std::source_location& sourceLocation = std::source_location::current())
        : ComputePipelineBase(CS::spirv, sourceLocation)
        , CSBindings(static_cast<ComputePipelineBase*>(this))
    {}

    ComputePipeline(const ComputePipeline& other)
        : ComputePipelineBase(other)
        , CSBindings(static_cast<ComputePipelineBase*>(this))
    {}

    ComputePipeline(ComputePipeline&& other) noexcept
        : ComputePipelineBase(std::move(other))
        , CSBindings(static_cast<ComputePipelineBase*>(this))
    {}

    ComputePipeline& operator=(const ComputePipeline& other)
    {
        ComputePipelineBase::operator=(other);
        return *this;
    }

    ComputePipeline& operator=(ComputePipeline&& other) noexcept
    {
        ComputePipelineBase::operator=(std::move(other));
        return *this;
    }
};

// Deduction guides: EDSL / string / SPIR-V paths deduce to ComputePipeline<void>
template<typename F>
ComputePipeline(F&&, ktm::uvec3, EmbeddedShader::CompilerOption = {}, std::source_location = std::source_location::current())
    -> ComputePipeline<>;
ComputePipeline(const std::string&, EmbeddedShader::ShaderLanguage, const std::source_location&)
    -> ComputePipeline<>;
ComputePipeline(const std::vector<uint32_t>&, const std::source_location&)
    -> ComputePipeline<>;

// ================= Texture2DProxy::createResource 实现 =================
// 此处 HardwareImage 和 HardwareImageCreateInfo 已经完整定义
namespace EmbeddedShader {
template<typename Type>
void Texture2DProxy<Type>::createResource(const ::HardwareImageCreateInfo& createInfo)
{
    ownedResource_ = std::make_unique<::HardwareImage>(createInfo);
    boundResource_ = ownedResource_.get();
}
} // namespace EmbeddedShader
