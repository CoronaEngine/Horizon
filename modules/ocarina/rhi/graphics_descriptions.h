//
// Created by Zero on 2022/8/10.
//

#pragma once

#include "core/stl.h"
#include "core/image/image_base.h"

namespace ocarina {

struct InstanceCreation {
    const char *applicationName;
#ifdef _DEBUG
    bool validation = true;
#else
    bool validation = false;
#endif
    std::vector<const char *> instanceExtentions;
    uint64_t windowHandle = InvalidUI64;
};

enum class ShaderVariableType : uint8_t
{
    FLOAT,
    FLOAT2,
    FLOAT3,
    FLOAT4,
    INT,
    INT2,
    INT3,
    INT4,
    FLOAT4X3,
    FLOAT4X4,
    COUNT,
};

struct SwapChainCreation {
    PixelStorage format = PixelStorage::BYTE4; ///< Back buffers format.
    ColorSpace colorSpace = ColorSpace::SRGB;   ///< Back buffers color space.
    uint32_t bufferCount = 1;         ///< Back buffers count.
    uint32_t width = 800;          ///< Back buffers width.
    uint32_t height = 600;              ///< Back buffers height.
    uint32_t refreshRate = 60;    ///< Refresh rate in Hz.
    bool vsync = false;///< 0 - The presentation occurs immediately, there is no synchronization. 1 through 4 - Synchronize presentation after the nth vertical blank.
    bool isWindowed = true;   //.
};

enum class GraphicBufferBindFlags {
    None = (0 << 0),                    ///< None
    VertexBuffer = (1 << 0),            ///< Can bind as a vertex buffer
    IndexBuffer = (1 << 1),             ///< Can bind as an index buffer
    ConstantBuffer = (1 << 2),          ///< Can bind as a constant buffer.
    StructuredBuffer = (1 << 3),        ///< Can bind as a structured buffer.
    ByteAddressBuffer = (1 << 4),       ///< Can bind as a byte address buffer.
    FormattedBuffer = (1 << 5),         ///< Can bind as a formatted buffer.
    CopySrc = (1 << 6),                 ///< Can bind as a copy source.
    CopyDst = (1 << 7),                 ///< Can bind as a copy destination.
    ShaderReadWrite = (1 << 8),         ///< Can bind for usage as ReadWrite buffer for any shader stage (UAV on D3D12).
    PredicationExt = (1 << 9),          ///< Can use the buffer for SetPredicationEx calls (must have AdapterFeatures::Predication requested feature enabled)
    AccelerationStructureExt = (1 << 10)///< Can bind as a ray tracing acceleration structure (require AdapterFeatures::RayTracing)
};

enum QueueType
{
    Graphics,///< Graphics queue
    Compute, ///< Compute queue
    Copy,    ///< Copy queue
    NumQueueType,
};

enum class ShaderType {
    VertexShader,           ///< Vertex shader.
    PixelShader,            ///< Pixel shader.
    GeometryShader,         ///< Geometry shader.
    ComputeShader,          ///< Compute shader.
    MeshShader,             ///< Mesh shader.
    NumShaderType,          ///< Number of shader types.
};

struct BufferCreation {
    //AccessFlags AccessFlags;   ///< Access flags.
    GraphicBufferBindFlags BindFlags;///< Buffer bind flags.
    //BufferFlags Flags;         ///< Buffer flags.
    uint32_t ElementSize;           ///< Element size in bytes.
    uint32_t NumElements;      ///< Number of elements in the resource.
    //Format Format;             ///< Data format.
    //ResourceState InitialState;///< Initial resource state.
    //Extension Extensions;      ///< Extensions
};

//! blending operator function
enum class BlendOperator : uint8_t {
    ADD,             //!< the fragment is added to the color buffer
    SUBTRACT,        //!< the fragment is subtracted from the color buffer
    REVERSE_SUBTRACT,//!< the color buffer is subtracted from the fragment
    MIN,             //!< the min between the fragment and color buffer
    MAX              //!< the max between the fragment and color buffer
};

//! blending function
enum class BlendFunction : uint8_t {
    ZERO,               //!< f(src, dst) = 0
    ONE,                //!< f(src, dst) = 1
    SRC_COLOR,          //!< f(src, dst) = src
    ONE_MINUS_SRC_COLOR,//!< f(src, dst) = 1-src
    DST_COLOR,          //!< f(src, dst) = dst
    ONE_MINUS_DST_COLOR,//!< f(src, dst) = 1-dst
    SRC_ALPHA,          //!< f(src, dst) = src.a
    ONE_MINUS_SRC_ALPHA,//!< f(src, dst) = 1-src.a
    DST_ALPHA,          //!< f(src, dst) = dst.a
    ONE_MINUS_DST_ALPHA,//!< f(src, dst) = 1-dst.a
    SRC_ALPHA_SATURATE  //!< f(src, dst) = (1,1,1) * min(src.a, 1 - dst.a), 1
};

//! stencil operation
enum class StencilOperation : uint8_t {
    KEEP,     //!< Keeps the current value.
    ZERO,     //!< Sets the value to 0.
    REPLACE,  //!< Sets the value to the stencil reference value.
    INCR,     //!< Increments the current value. Clamps to the maximum representable unsigned value.
    INCR_WRAP,//!< Increments the current value. Wraps value to zero when incrementing the maximum representable unsigned value.
    DECR,     //!< Decrements the current value. Clamps to 0.
    DECR_WRAP,//!< Decrements the current value. Wraps value to the maximum representable unsigned value when decrementing a value of zero.
    INVERT,   //!< Bitwise inverts the current value.
};

//! comparison function for the depth / stencil sampler
enum class SamplerCompareFunc : uint8_t {
    // don't change the enums values
    LE = 0,//!< Less or equal
    GE,    //!< Greater or equal
    L,     //!< Strictly less than
    G,     //!< Strictly greater than
    E,     //!< Equal
    NE,    //!< Not equal
    A,     //!< Always. Depth / stencil testing is deactivated.
    N      //!< Never. The depth / stencil test always fails.
};

enum class PrimitiveType : uint8_t
{
    POINTS = 0,       //!< points
    LINES = 1,        //!< lines
    LINE_STRIP = 3,   //!< line strip
    TRIANGLES = 4,    //!< triangles
    TRIANGLE_STRIP = 5//!< triangle strip
};

enum class CullingMode : uint8_t {
    NONE,         //!< No culling, front and back faces are visible
    FRONT,        //!< Front face culling, only back faces are visible
    BACK,         //!< Back face culling, only front faces are visible
    FRONT_AND_BACK//!< Front and Back, geometry is not visible
};

enum class FilterMode : uint8_t {
    POINT,     //!< Point filtering, also known as nearest neighbor filtering
    BILINEAR,  //!< Bilinear filtering
    TRILINEAR, //!< Trilinear filtering
    ANISOTROPIC//!< Anisotropic filtering
};

enum class AddressMode : uint8_t {
    WRAP,      //!< Texture coordinates outside 0..1 are wrapped around
    MIRROR,    //!< Texture coordinates outside 0..1 are mirrored
    CLAMP,     //!< Texture coordinates outside 0..1 are clamped to 0 or 1
    BORDER,    //!< Texture coordinates outside 0..1 are given a user-defined border color
    MIRROR_ONCE//!< Texture coordinates outside 0..1 are mirrored once and then clamped
};

struct DescriptorCount {
    uint32_t ubo : 8;
    uint32_t srv : 8;
    uint32_t uav : 8;
    uint32_t samplers : 8;

    bool operator==(DescriptorCount const &right) const {
        return ubo == right.ubo && srv == right.srv && uav == right.uav &&
               samplers == right.samplers;
    }
};

struct VertexBinding
{
    uint32_t binding = 0;
    uint32_t stride = 0;
};

/// Vertex attribute enum.
struct VertexAttributeType {
    /// Corresponds to vertex shader attribute.
    enum struct Enum : uint8_t {
        Position, 
        Normal,   
        Tangent,  
        Bitangent,
        Color0,   
        Color1,   
        Color2,   
        Color3,   
        Indices,  
        Weight,   
        TexCoord0,
        TexCoord1,
        TexCoord2,
        TexCoord3,
        TexCoord4,
        TexCoord5,
        TexCoord6,
        TexCoord7,

        Count
    };

    static Enum from_string(const char *name) {
        if (strstr(name, "POSITION") != nullptr) return Enum::Position;
        if (strstr(name, "NORMAL") != nullptr) return Enum::Normal;
        if (strstr(name, "TANGENT") != nullptr) return Enum::Tangent;
        if (strstr(name, "BITANGENT") != nullptr) return Enum::Bitangent;
        if (strstr(name, "COLOR0") != nullptr) return Enum::Color0;
        if (strstr(name, "COLOR1") != nullptr) return Enum::Color1;
        if (strstr(name, "COLOR2") != nullptr) return Enum::Color2;
        if (strstr(name, "COLOR3") != nullptr) return Enum::Color3;
        if (strstr(name, "INDICES") != nullptr) return Enum::Indices;
        if (strstr(name, "WEIGHT") != nullptr) return Enum::Weight;
        if (strstr(name, "TEXCOORD0") != nullptr) return Enum::TexCoord0;
        if (strstr(name, "TEXCOORD1") != nullptr) return Enum::TexCoord1;
        if (strstr(name, "TEXCOORD2") != nullptr) return Enum::TexCoord2;
        if (strstr(name, "TEXCOORD3") != nullptr) return Enum::TexCoord3;
        if (strstr(name, "TEXCOORD4") != nullptr) return Enum::TexCoord4;
        if (strstr(name, "TEXCOORD5") != nullptr) return Enum::TexCoord5;
        if (strstr(name, "TEXCOORD6") != nullptr) return Enum::TexCoord6;
        if (strstr(name, "TEXCOORD7") != nullptr) return Enum::TexCoord7;

        return Enum::Count;

    };
};

struct VertexAttribute
{
    uint8_t location = 0;      //!< attribute location index
    uint8_t binding = 0;
    uint8_t offset = 0;
    uint8_t type = 0;         //!< attribute type
    uint32_t format = 0;                //!< attribute format
};

enum class BufferType : uint8_t
{
    VertexBuffer,
    IndexBuffer,
    ConstantBuffer,
};

enum class DeviceMemoryUsage : uint8_t {
    /** No intended memory usage specified.
        Use other members of VmaAllocationCreateInfo to specify your requirements.
        */
    MEMORY_USAGE_UNKNOWN = 0,
    /** Memory will be used on device only, so faster access from the device is preferred.
        It usually means device-local GPU memory.
        No need to be mappable on host.
        Good e.g. for images to be used as attachments, images containing textures to be sampled,
        buffers used as vertex buffer, index buffer, uniform buffer and majority of
        other types of resources used by device.
        You can still do transfers from/to such resource to/from host memory.

        The allocation may still end up in `HOST_VISIBLE` memory on some implementations.
        In such case, you are free to map it.
        You can also use `VMA_ALLOCATION_CREATE_MAPPED_BIT` with this usage type.
        */
    MEMORY_USAGE_GPU_ONLY = 1,
    /** Memory will be mapped and used on host.
        It usually means CPU system memory.
        Could be used for transfer to/from device.
        Good e.g. for "staging" copy of buffers and images, used as transfer source or destination.
        Resources created in this pool may still be accessible to the device, but access to them can be slower.

        Guarantees to be `HOST_VISIBLE` and `HOST_COHERENT`.
        */
    MEMORY_USAGE_CPU_ONLY = 2,
    /** Memory will be used for frequent (dynamic) updates from host and reads on device (upload).
        Good e.g. for vertex buffers or uniform buffers updated every frame.

        Guarantees to be `HOST_VISIBLE`.
        */
    MEMORY_USAGE_CPU_TO_GPU = 3,
    /** Memory will be used for frequent writing on device and readback on host (download).

        Guarantees to be `HOST_VISIBLE`.
        */
    MEMORY_USAGE_GPU_TO_CPU = 4,
    //MEMORY_USAGE_MAX_ENUM = 0x7FFFFFFF
};

//enum CommandBatchType {
//    Graphics,///< Graphics command list
//    Compute, ///< Compute command list
//    Copy     ///< Copy command list
//};

enum class TextureUsageFlags : uint32_t {
    None = (0 << 0),            ///
    RenderTarget = (1 << 1),   /// Can bind as a render target.
    DepthStencil = (1 << 2),   /// Can bind as a depth stencil.
    CopySrc = (1 << 3),        /// Can bind as a copy source.
    CopyDst = (1 << 4),        /// Can bind as a copy destination.
    SwapChain = (1 << 5),      /// Can bind as a swap chain
    ShaderReadWrite = (1 << 6),/// Can bind as for read write access from all shader stages (UAV).
    ShaderReadOnly = (1 << 7)  /// Can bind as for read only access from all shader stages (SRV).
};

enum class MultiSampleCount : uint8_t {
    SAMPLE_COUNT_1 = 0, ///< 1 sample per pixel.
    SAMPLE_COUNT_2 = 1, ///< 2 samples per pixel.
    SAMPLE_COUNT_4 = 2, ///< 4 samples per pixel.
    SAMPLE_COUNT_8 = 3, ///< 8 samples per pixel.
    SAMPLE_COUNT_16 = 4,///< 16 samples per pixel.
    SAMPLE_COUNT_32 = 5,///< 32 samples per pixel.
    SAMPLE_COUNT_64 = 6,///< 64 samples per pixel.
};

enum class ColorMask : uint8_t
{
    ColorMaskNone = 0,  ///< No channel mask.
    ColorMaskR = 1 << 0,///< Red channel mask.
    ColorMaskG = 1 << 1,///< Green channel mask.
    ColorMaskB = 1 << 2,///< Blue channel mask.
    ColorMaskA = 1 << 3,///< Alpha channel mask.
    ColorMaskAll = ColorMaskR | ColorMaskG | ColorMaskB | ColorMaskA///< All channels mask.
};

struct RenderTargetCreation {
    PixelStorage format = PixelStorage::BYTE4; ///< Back buffers format.
    bool srgb = false;///< Back buffers sRGB.
    uint32_t width = 800;          ///< Back buffers width.
    uint32_t height = 600;              ///< Back buffers height.
    uint32_t mipmaps = 1;      ///< mipmap level count.
    uint32_t array_size = 1;  /// texture array size
    uint32_t msaa_samples = 1;  /// msaa samples
    TextureUsageFlags usage = TextureUsageFlags::None; ///< texture usage
    float4 clear_color = {0.025f, 0.025f, 0.025f, 1.0f};///< clear color
    float clear_depth = 1.0f;          ///< clear depth
    uint32_t clear_stencil = 0;        ///< clear stencil
};

struct RenderPassCreation
{
    constexpr static uint32_t MAX_RENDER_TARGETS = 8;
    RenderTargetCreation render_targets[MAX_RENDER_TARGETS]; ///< render target
    uint32_t render_target_count = 0; ///< render target count, 0 use swapchain
    float4 swapchain_clear_color = {0.025f, 0.025f, 0.025f, 1.0f};     ///< clear color
    float swapchain_clear_depth = 1.0f;                           ///< clear depth
    uint32_t swapchain_clear_stencil = 0;                              ///< clear stencil
};

struct SamplerCreation {
    FilterMode filter = FilterMode::BILINEAR;///< Filtering mode.
    FilterMode mipmap_filter = FilterMode::BILINEAR;///< Mipmap filtering mode.
    AddressMode address_u = AddressMode::WRAP;///< Addressing mode for U coordinate.
    AddressMode address_v = AddressMode::WRAP;///< Addressing mode for V coordinate.
    AddressMode address_w = AddressMode::WRAP;///< Addressing mode for W coordinate.
};

struct TextureViewCreation {
    PixelStorage format = PixelStorage::BYTE4;        ///< Back buffers format.
    uint32_t mip_level_count = 1;                     ///< mip level count
    uint32_t array_layer_count = 1;                   ///< array layer count
    TextureUsageFlags usage = TextureUsageFlags::None;///< texture usage
    SamplerCreation sampler;                          ///< sampler creation
};

static  constexpr uint max_descriptor_sets_per_shader = 8;

}// namespace ocarina