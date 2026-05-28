#pragma once

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <ranges>
#include <shared_mutex>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <ktm/ktm.h>

#include "format.h"
#include "Compiler/ShaderCodeCompiler.h"
#include "Codegen/ComputePipelineObject.h"
#include "Codegen/RasterizedPipelineObject.h"
#include "Codegen/VariateProxy.h"

#ifndef HORIZON_ENABLE_VALIDATION
#define HORIZON_ENABLE_VALIDATION 1
#endif

namespace Corona::Horizon
{
    // ================================================================
    // Forward Declarations
    // ================================================================

    struct HardwareValidationConfig;
    struct HardwareBuffer;
    struct HardwareImage;
    struct HardwareImageLayerSelector;
    struct HardwarePushConstant;

    //struct BottomLevelAccelerationStructure;
    //struct TopLevelAccelerationStructure;

    struct PipelineBindingScope;
    struct ResourceProxy;

    struct ComputePipeline;
    struct RasterizerPipeline;
    struct RayTracingPipeline;

    struct HardwareExecutor;
    struct HardwareDisplayer;

    struct BindingSlot;



    // ================================================================
    // Validation
    // ================================================================

    struct HardwareValidationConfig
    {
        HardwareValidationMode mode = HardwareValidationMode::Throw;
    };

    void set_hardware_validation_config(HardwareValidationConfig config);
    [[nodiscard]] HardwareValidationConfig get_hardware_validation_config();
    [[nodiscard]] bool is_hardware_validation_enabled();



    // ================================================================
    // HardwareBuffer
    // ================================================================

    // Hardware data must be plain value data; do not store CPU pointers inside transferred structs.
    template <typename T>
    concept HardwareTransferable = std::is_trivially_copyable_v<std::remove_cvref_t<T>> && !std::is_pointer_v<std::remove_cvref_t<T>>;

    // index 限制 uint16_t/uint32_t。
    template <typename T>
    concept HardwareIndexType = std::same_as<std::remove_cvref_t<T>, uint16_t> || std::same_as<std::remove_cvref_t<T>, uint32_t>;

    struct HardwareBufferDesc
    {
        uint64_t element_count = 0;
        uint32_t element_size = 0;
        BufferUsageFlags usage = BufferUsageFlags::None;
        CpuAccessMode cpu_access = CpuAccessMode::Write;
        bool dedicated = false;
        bool exportable = false;
        std::string debug_name;

        HardwareBufferDesc& apply(const HardwareBufferOptions& options) noexcept
        {
            cpu_access = options.cpu_access;
            dedicated = options.dedicated;
            exportable = options.exportable;
            return *this;
        }

        [[nodiscard]] uint64_t byte_size() const
        {
            if (element_count == 0 || element_size == 0)
                return 0;

            if (element_count > std::numeric_limits<uint64_t>::max() / element_size)
                throw std::overflow_error("HardwareBufferDesc total byte size overflow.");

            return element_count * uint64_t(element_size);
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBufferDesc typed(uint64_t count, BufferUsageFlags usage, std::string name = {}, HardwareBufferOptions options = {})
        {
            HardwareBufferDesc desc;
            desc.element_count = count;
            desc.element_size = uint32_t(sizeof(T));
            desc.usage = usage;
            desc.debug_name = std::move(name);
            desc.apply(options);
            (void)desc.byte_size();
            return desc;
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBufferDesc vertex(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferDst | BufferUsageFlags::Vertex, std::move(name), options);
        }

        template <HardwareIndexType T>
        [[nodiscard]] static HardwareBufferDesc index(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferDst | BufferUsageFlags::Index, std::move(name), options);
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBufferDesc uniform(std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(1, BufferUsageFlags::TransferDst | BufferUsageFlags::Uniform, std::move(name), options);
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBufferDesc storage(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferSrc | BufferUsageFlags::TransferDst | BufferUsageFlags::Storage, std::move(name), options);
        }
    };

    struct HardwareBuffer
    {
    public:
        HardwareBuffer();
        HardwareBuffer(const HardwareBufferDesc &desc, std::span<const std::byte> upload_data = {});

        // Copies share the same underlying GPU buffer handle.
        HardwareBuffer(const HardwareBuffer& other);
        HardwareBuffer(HardwareBuffer&& other) noexcept;
        ~HardwareBuffer();

        HardwareBuffer& operator=(const HardwareBuffer& other);
        HardwareBuffer& operator=(HardwareBuffer&& other) noexcept;
        explicit operator bool() const;

        [[nodiscard]] std::uintptr_t get_buffer_id() const noexcept { return buffer_id; }
        [[nodiscard]] uint64_t get_element_size() const;
        [[nodiscard]] uint64_t get_element_count() const;
        [[nodiscard]] uint64_t get_byte_size() const
        {
            const uint64_t element_count = get_element_count();
            const uint64_t element_size = get_element_size();
            if (element_size != 0 && element_count > std::numeric_limits<uint64_t>::max() / element_size)
                throw std::overflow_error("HardwareBuffer total byte size overflow.");

            return element_count * element_size;
        }
        [[nodiscard]] void* get_mapped_data() const;
        [[nodiscard]] std::span<std::byte> get_mapped_bytes() const
        {
            auto* data = static_cast<std::byte*>(get_mapped_data());
            if (data == nullptr)
                return {};

            const uint64_t mapped_size = get_byte_size();
            if (mapped_size > std::numeric_limits<size_t>::max())
                return {};

            return {data, static_cast<size_t>(mapped_size)};
        }

        [[nodiscard]] bool write_bytes(std::span<const std::byte> data, uint64_t byte_offset = 0) const;
        [[nodiscard]] bool read_bytes(std::span<std::byte> output, uint64_t byte_offset = 0) const;

        template <HardwareTransferable T>
        [[nodiscard]] bool write(std::span<const T> data, uint64_t byte_offset = 0) const
        {
            return write_bytes(std::as_bytes(data), byte_offset);
        }

        template <HardwareTransferable T>
        [[nodiscard]] bool write_elements(std::span<const T> data, uint64_t first_element = 0) const
        {
            if (first_element > std::numeric_limits<uint64_t>::max() / sizeof(T))
                return false;

            return write(data, first_element * sizeof(T));
        }

        template <HardwareTransferable T>
        [[nodiscard]] bool write_value(const T& value, uint64_t byte_offset = 0) const
        {
            return write(std::span<const T>(&value, 1), byte_offset);
        }

        template <HardwareTransferable T>
        [[nodiscard]] bool write_element(const T& value, uint64_t element_index = 0) const
        {
            return write_elements(std::span<const T>(&value, 1), element_index);
        }

        template <HardwareTransferable T>
        requires (!std::is_const_v<T>)
        [[nodiscard]] bool read(std::span<T> output, uint64_t byte_offset = 0) const
        {
            return read_bytes(std::as_writable_bytes(output), byte_offset);
        }

        template <HardwareTransferable T>
        requires (!std::is_const_v<T>)
        [[nodiscard]] bool read_elements(std::span<T> output, uint64_t first_element = 0) const
        {
            if (first_element > std::numeric_limits<uint64_t>::max() / sizeof(T))
                return false;

            return read(output, first_element * sizeof(T));
        }

        [[nodiscard]] static HardwareBuffer from_bytes(std::span<const std::byte> data, uint32_t element_size, BufferUsageFlags usage, std::string name = {}, HardwareBufferOptions options = {});

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBuffer vertex(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::vertex<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareTransferable<std::ranges::range_value_t<Range>>
        [[nodiscard]] static HardwareBuffer vertex(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return vertex<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        template <HardwareIndexType T>
        [[nodiscard]] static HardwareBuffer index(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::index<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareIndexType<std::ranges::range_value_t<Range>>
        [[nodiscard]] static HardwareBuffer index(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return index<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBuffer uniform(const T& value, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::uniform<T>(std::move(name), options), std::as_bytes(std::span<const T>(&value, 1)));
        }

        template <HardwareTransferable T>
        [[nodiscard]] static HardwareBuffer storage(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::storage<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareTransferable<std::ranges::range_value_t<Range>>
        [[nodiscard]] static HardwareBuffer storage(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return storage<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        //[[nodiscard]] BufferCopyCommand copy_to(const HardwareBuffer& dst, BufferRange src = BufferRange::entire(), uint64_t dst_offset = 0) const;
        //[[nodiscard]] BufferToImageCommand copy_to(const HardwareImage& dst, uint64_t buffer_offset = 0, uint32_t image_layer = 0, uint32_t image_mip = 0) const;
        //[[nodiscard]] uint32_t store_descriptor() const;
        //[[nodiscard]] static HardwareBuffer import_external(const ExternalMemoryHandle &handle, const HardwareBufferDesc &desc);
        //[[nodiscard]] ExternalMemoryHandle export_external() const;

    private:
        std::atomic<std::uintptr_t> buffer_id;
        mutable std::mutex buffer_mutex;

        friend class HardwareImage;
    };



    // ================================================================
    // HardwareImage
    // ================================================================

    /*struct HardwareImageDesc
    {
        ImageDimension dimension = ImageDimension::Image2D;
        ImageExtent extent {};
        Format format = Format::UNKNOWN;
        ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst;
        CpuAccessMode cpu_access = CpuAccessMode::None;
        uint32_t array_layers = 1;
        uint32_t mip_levels = 1;
        uint32_t sample_count = 1;
        bool dedicated = false;
        bool exportable = false;
        std::string debug_name;

        HardwareImageDesc& apply(const HardwareImageOptions& options) noexcept
        {
            cpu_access = options.cpu_access;
            dedicated = options.dedicated;
            exportable = options.exportable;
            return *this;
        }

        static HardwareImageDesc texture_2d(uint32_t width, 
                                            uint32_t height,
                                            Format format,
                                            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                            std::string name = {},
                                            HardwareImageOptions options = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Image2D;
            desc.extent = {width, height, 1};
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        static HardwareImageDesc texture_2d_array(uint32_t width,
                                                  uint32_t height,
                                                  uint32_t layers,
                                                  Format format,
                                                  ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                                  std::string name = {},
                                                  HardwareImageOptions options = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Image2DArray;
            desc.extent = {width, height, 1};
            desc.array_layers = layers;
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        static HardwareImageDesc texture_3d(uint32_t width,
                                            uint32_t height,
                                            uint32_t depth,
                                            Format format,
                                            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                            std::string name = {},
                                            HardwareImageOptions options = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Image3D;
            desc.extent = {width, height, depth};
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        static HardwareImageDesc cube(uint32_t size,
                                      Format format,
                                      ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                      std::string name = {},
                                      HardwareImageOptions options = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Cube;
            desc.extent = {size, size, 1};
            desc.array_layers = 6;
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        static HardwareImageDesc cube_array(uint32_t size,
                                            uint32_t cube_count,
                                            Format format,
                                            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
                                            std::string name = {},
                                            HardwareImageOptions options = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::CubeArray;
            desc.extent = {size, size, 1};
            desc.array_layers = cube_count * 6;
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        static HardwareImageDesc color_attachment(uint32_t width,
                                                  uint32_t height,
                                                  Format format,
                                                  std::string name = {},
                                                  HardwareImageOptions options = {})
        {
            return texture_2d(width,
                              height,
                              format,
                              ImageUsageFlags::ColorAttachment | ImageUsageFlags::Sampled | ImageUsageFlags::TransferSrc | ImageUsageFlags::TransferDst,
                              std::move(name),
                              options);
        }

        static HardwareImageDesc depth_attachment(uint32_t width,
                                                  uint32_t height,
                                                  Format format,
                                                  std::string name = {},
                                                  HardwareImageOptions options = {})
        {
            return texture_2d(width,
                              height,
                              format,
                              ImageUsageFlags::DepthStencilAttachment | ImageUsageFlags::Sampled | ImageUsageFlags::TransferSrc | ImageUsageFlags::TransferDst,
                              std::move(name),
                              options);
        }
    };

    struct HardwareImage
    {
    public:
        HardwareImage();
        HardwareImage(const HardwareImageDesc& desc, std::span<const std::byte> upload_data = {});

        HardwareImage(const HardwareImage& other);
        HardwareImage(HardwareImage&& other) noexcept;
        ~HardwareImage();

        HardwareImage& operator=(const HardwareImage& other);
        HardwareImage& operator=(HardwareImage&& other) noexcept;

        [[nodiscard]] std::uintptr_t get_image_id() const noexcept { return image_id; }
        [[nodiscard]] HardwareImageLayerSelector operator[](uint32_t layer) const;
        [[nodiscard]] HardwareImage whole() const;
        [[nodiscard]] HardwareImage layer(uint32_t layer_index) const;
        [[nodiscard]] HardwareImage mip(uint32_t mip_index) const;
        [[nodiscard]] HardwareImage subresource(uint32_t layer_index, uint32_t mip_index) const;
        [[nodiscard]] uint32_t subresource_index(uint32_t layer_index, uint32_t mip_index) const;
        [[nodiscard]] uint32_t subresource_count() const noexcept;
        [[nodiscard]] ImageExtent mip_extent(uint32_t mip_index) const;
        [[nodiscard]] ImageExtent extent() const;
        bool write_subresource_bytes(uint32_t layer_index, uint32_t mip_index, std::span<const std::byte> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;
        bool read_subresource_bytes(uint32_t layer_index, uint32_t mip_index, std::span<std::byte> output, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;
        bool write_bytes(std::span<const std::byte> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;
        bool read_bytes(std::span<std::byte> output, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const;
        
        template <HardwareTransferable T>
        bool write_subresource(uint32_t layer_index, uint32_t mip_index, std::span<const T> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return write_subresource_bytes(layer_index, mip_index, std::as_bytes(data), row_pitch, slice_pitch);
        }

        template <HardwareTransferable T>
        requires (!std::is_const_v<T>)
        bool read_subresource(uint32_t layer_index,uint32_t mip_index,std::span<T> output,uint64_t row_pitch = 0,uint64_t slice_pitch = 0) const
        {
            return read_subresource_bytes(layer_index, mip_index, std::as_writable_bytes(output), row_pitch, slice_pitch);
        }

        template <HardwareTransferable T>
        bool write(std::span<const T> data, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return write_bytes(std::as_bytes(data), row_pitch, slice_pitch);
        }

        template <HardwareTransferable T>
        requires (!std::is_const_v<T>)
        bool read(std::span<T> output, uint64_t row_pitch = 0, uint64_t slice_pitch = 0) const
        {
            return read_bytes(std::as_writable_bytes(output), row_pitch, slice_pitch);
        }

        void set_clear_color(float r, float g, float b, float a);

        [[nodiscard]] ImageCopyCommand copy_to(const HardwareImage &dst, uint32_t src_layer = 0, uint32_t dst_layer = 0, uint32_t src_mip = 0, uint32_t dst_mip = 0) const;
        [[nodiscard]] ImageToBufferCommand copy_to(const HardwareBuffer &dst, uint32_t image_layer = 0, uint32_t image_mip = 0, uint64_t buffer_offset = 0) const;
        [[nodiscard]] BufferToImageCommand copy_from(const HardwareBuffer &src, uint64_t buffer_offset = 0, uint32_t image_layer = 0, uint32_t image_mip = 0) const;
        [[nodiscard]] uint32_t store_descriptor() const;
        static HardwareImage import_external(const ExternalMemoryHandle &handle, const HardwareImageDesc &desc, uint64_t allocation_size = 0);
        [[nodiscard]] ExternalMemoryHandle export_external() const;
        
    private:
        std::atomic<std::uintptr_t> image_id;
        mutable std::mutex image_mutex;
        ImageSubresourceRange range_ = ImageSubresourceRange::whole();

        friend struct HardwareImageLayerSelector;
        friend struct HardwareBuffer;
    };

    struct HardwareImageLayerSelector
    {
    public:
        HardwareImageLayerSelector(const HardwareImage &image, uint32_t layer_index) : image_(image), layer_(layer_index) {}
        [[nodiscard]] HardwareImage operator[](uint32_t mip_index) const;
    private:
        HardwareImage image_;
        uint32_t layer_ = 0;
    };

    inline HardwareImageLayerSelector HardwareImage::operator[](uint32_t layer_index) const
    {
        return HardwareImageLayerSelector(*this, layer_index);
    }*/

    
    
    // ================================================================
    // HardwarePushConstant
    // ================================================================

    /*inline constexpr uint32_t kPortablePushConstantByteSize = 128;

    template <typename T>
    concept HardwarePushConstantValue = std::is_trivially_copyable_v<std::remove_cvref_t<T>> && 
                                        !std::is_pointer_v<std::remove_cvref_t<T>> &&
                                        !std::is_same_v<std::remove_cvref_t<T>, HardwarePushConstant>;

    struct HardwarePushConstantDesc
    {
        uint64_t byte_size = 0;
        std::string debug_name;
    };

    struct HardwarePushConstant
    {
    public:
        static constexpr uint64_t max_byte_size = kPortablePushConstantByteSize;
        static constexpr uint64_t whole_size = std::numeric_limits<uint64_t>::max();

        HardwarePushConstant() = default;
        explicit HardwarePushConstant(HardwarePushConstantDesc desc);
        explicit HardwarePushConstant(uint64_t byte_size);
        ~HardwarePushConstant();

        template <HardwarePushConstantValue T>
        explicit HardwarePushConstant(const T& value)
        {
            assign(value);
        }

        template <HardwarePushConstantValue T>
        HardwarePushConstant& operator=(const T& value)
        {
            assign(value);
            return *this;
        }

        bool write_bytes(std::span<const std::byte> data, uint64_t offset = 0) noexcept;
        bool read_bytes(std::span<std::byte> output, uint64_t offset = 0) const noexcept;

        [[nodiscard]] std::vector<std::byte> snapshot_bytes(uint64_t offset = 0, uint64_t byte_size = whole_size) const;

        template <HardwarePushConstantValue T>
        bool write_value(uint64_t offset, const T &value) noexcept
        {
            return write_bytes(&value, sizeof(T), offset);
        }

        template <HardwarePushConstantValue T>
        bool assign(const T& value)
        {
            if (!reset(sizeof(T)))
                return false;

            return write_value(0, value);
        }

        template <HardwarePushConstantValue T>
        bool assign(const T &value) noexcept
        {
            if (!reset(sizeof(T)))
                return false;

            return write_value(0, value);
        }

    private:
        HardwarePushConstantDesc desc_;
        std::array<std::byte, max_byte_size> storage_{};
    };*/



    // ================================================================
    // Pipeline Descriptors
    // ================================================================

    /*struct PipelineShaderDesc
    {
        PipelineShaderStage stage;
        EmbeddedShader::ShaderCodeModule module;

        PipelineShaderDesc(PipelineShaderStage stage, EmbeddedShader::ShaderCodeModule module) : stage(stage), module(std::move(module)) {}

        static PipelineShaderDesc from_spirv(PipelineShaderStage stage, std::vector<uint32_t> spirv)
        {
            auto reflection = EmbeddedShader::ShaderLanguageConverter::spirvCrossReflectedBindInfo(spirv, EmbeddedShader::ShaderLanguage::HLSL);
            return PipelineShaderDesc(stage, EmbeddedShader::ShaderCodeModule(std::move(spirv), std::move(reflection)));
        }

        [[nodiscard]] bool is_compute() const noexcept
        {
            return stage == PipelineShaderStage::Compute;
        }

        [[nodiscard]] bool is_vertex() const noexcept
        {
            return stage == PipelineShaderStage::Vertex;
        }

        [[nodiscard]] bool is_fragment() const noexcept
        {
            return stage == PipelineShaderStage::Fragment;
        }

        [[nodiscard]] bool is_ray_tracing() const noexcept
        {
            switch (stage)
            {
            case PipelineShaderStage::RayGeneration:
            case PipelineShaderStage::Miss:
            case PipelineShaderStage::ClosestHit:
            case PipelineShaderStage::AnyHit:
            case PipelineShaderStage::Intersection:
            case PipelineShaderStage::Callable:
                return true;
            default:
                return false;
            }
        }
    };

    struct EdslPipelineOptions
    {
        EmbeddedShader::CompilerOption compiler;
        bool auto_bind = true;
    };

    struct ComputePipelineDesc
    {
        PipelineShaderDesc compute_shader;
        ktm::uvec3 thread_group_size = {1, 1, 1};
        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries;
        std::string debug_name;

        ComputePipelineDesc(PipelineShaderDesc shader, ktm::uvec3 numthreads = {1, 1, 1}) : compute_shader(std::move(shader)), thread_group_size(numthreads)
        {
            if (compute_shader.stage != PipelineShaderStage::Compute)
                throw std::invalid_argument("ComputePipelineDesc requires a compute shader.");
        }

        template <typename F>
        static ComputePipelineDesc from_edsl(F &&compute_shader_code, ktm::uvec3 numthreads = {1, 1, 1}, EdslPipelineOptions options = {}, std::source_location source_location = std::source_location::current())
        {
            auto object = EmbeddedShader::ComputePipelineObject::compile(std::forward<F>(compute_shader_code), numthreads, options.compiler, source_location);

            ComputePipelineDesc desc(
                PipelineShaderDesc
                {
                    PipelineShaderStage::Compute,
                    object.compute->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,options.compiler.enableBindless)
                },
                numthreads
            );

            desc.auto_bind_entries = std::move(object.autoBindEntries);
            return desc;
        }

        static ComputePipelineDesc from_spirv(std::vector<uint32_t> spirv)
        {
            return ComputePipelineDesc(PipelineShaderDesc::from_spirv(PipelineShaderStage::Compute, std::move(spirv)));
        }

        static ComputePipelineDesc from_source(std::string source,
                                               EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
                                               EmbeddedShader::CompilerOption compiler_option = {},
                                               std::source_location source_location = std::source_location::current())
        {
            EmbeddedShader::ShaderCodeCompiler compiler(source,
                                                        EmbeddedShader::ShaderStage::ComputeShader,
                                                        language,
                                                        compiler_option,
                                                        source_location);

            return ComputePipelineDesc(
                PipelineShaderDesc
                {
                    PipelineShaderStage::Compute,
                    compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,compiler_option.enableBindless)
                }
            );
        }

        ComputePipelineDesc& set_thread_group_size(ktm::uvec3 value) noexcept
        {
            thread_group_size = value;
            return *this;
        }

        ComputePipelineDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
        }
    };

    struct BlendStateDesc
    {
        bool logic_op_enabled = false;
        std::vector<BlendAttachmentDesc> attachments = {BlendAttachmentDesc::alpha_blend()};

        BlendStateDesc& set_attachment(uint32_t index, BlendAttachmentDesc desc)
        {
            if (attachments.size() <= index)
                attachments.resize(size_t(index) + 1, BlendAttachmentDesc::alpha_blend());

            attachments[index] = std::move(desc);
            return *this;
        }

        BlendStateDesc& set_attachment_count(uint32_t count)
        {
            attachments.resize(count, BlendAttachmentDesc::alpha_blend());
            return *this;
        }

        BlendStateDesc& set_opaque()
        {
            for (auto& attachment : attachments)
                attachment = BlendAttachmentDesc::opaque();

            return *this;
        }

        BlendStateDesc& set_alpha_blend()
        {
            for (auto& attachment : attachments)
                attachment = BlendAttachmentDesc::alpha_blend();

            return *this;
        }
    };

    struct RenderTargetLayoutDesc
    {
        std::vector<Format> color_formats;
        Format depth_stencil_format = Format::UNKNOWN;
        uint32_t multiview_count = 1;

        RenderTargetLayoutDesc& add_color_format(Format format)
        {
            color_formats.push_back(format);
            return *this;
        }

        RenderTargetLayoutDesc& set_color_format(uint32_t index, Format format)
        {
            if (color_formats.size() <= index)
                color_formats.resize(size_t(index) + 1, Format::UNKNOWN);

            color_formats[index] = format;
            return *this;
        }

        RenderTargetLayoutDesc& set_depth_stencil_format(Format format) noexcept
        {
            depth_stencil_format = format;
            return *this;
        }

        RenderTargetLayoutDesc& set_multiview_count(uint32_t value) noexcept
        {
            multiview_count = value;
            return *this;
        }
    };

    struct RasterizerPipelineDesc
    {
        PipelineShaderDesc vertex_shader;
        PipelineShaderDesc fragment_shader;

        RasterizerStateDesc rasterizer;
        DepthStencilStateDesc depth_stencil;
        BlendStateDesc blend;
        MultisampleStateDesc multisample;
        DepthAttachmentDesc depth_attachment;

        uint32_t multiview_count = 1;

        std::vector<EmbeddedShader::AutoBindEntry> auto_bind_entries;
        std::string debug_name;

        RasterizerPipelineDesc(PipelineShaderDesc vertex, PipelineShaderDesc fragment) : vertex_shader(std::move(vertex)), fragment_shader(std::move(fragment))
        {
            if (vertex_shader.stage != PipelineShaderStage::Vertex)
                throw std::invalid_argument("RasterizerPipelineDesc requires a vertex shader.");

            if (fragment_shader.stage != PipelineShaderStage::Fragment)
                throw std::invalid_argument("RasterizerPipelineDesc requires a fragment shader.");
        }

        template <typename VS, typename FS>
        static RasterizerPipelineDesc from_edsl(VS&& vertex_shader_code,
                                                FS&& fragment_shader_code,
                                                EdslPipelineOptions options = {},
                                                std::source_location source_location = std::source_location::current())
        {
            auto object = EmbeddedShader::RasterizedPipelineObject::compile(std::forward<VS>(vertex_shader_code),
                                                                            std::forward<FS>(fragment_shader_code),
                                                                            options.compiler,
                                                                            source_location);

            RasterizerPipelineDesc desc(
                PipelineShaderDesc
                {
                    PipelineShaderStage::Vertex,
                    object.vertex->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,
                                                 options.compiler.enableBindless)
                },
                PipelineShaderDesc
                {
                    PipelineShaderStage::Fragment,
                    object.fragment->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV,
                                                   options.compiler.enableBindless)
                }
            );

            if (options.auto_bind)
                desc.auto_bind_entries = std::move(object.autoBindEntries);

            return desc;
        }

        static RasterizerPipelineDesc from_spirv(std::vector<uint32_t> vertex_spirv, std::vector<uint32_t> fragment_spirv)
        {
            return RasterizerPipelineDesc(PipelineShaderDesc::from_spirv(PipelineShaderStage::Vertex, std::move(vertex_spirv)),
                                          PipelineShaderDesc::from_spirv(PipelineShaderStage::Fragment,std::move(fragment_spirv)));
        }

        static RasterizerPipelineDesc from_source(std::string vertex_source,
                                                  std::string fragment_source,
                                                  EmbeddedShader::ShaderLanguage vertex_language = EmbeddedShader::ShaderLanguage::GLSL,
                                                  EmbeddedShader::ShaderLanguage fragment_language = EmbeddedShader::ShaderLanguage::GLSL,
                                                  EmbeddedShader::CompilerOption compiler_option = {},
                                                  std::source_location source_location = std::source_location::current())
        {
            EmbeddedShader::ShaderCodeCompiler vertex_compiler(vertex_source,
                                                               EmbeddedShader::ShaderStage::VertexShader,
                                                               vertex_language,
                                                               compiler_option,
                                                               source_location);

            EmbeddedShader::ShaderCodeCompiler fragment_compiler(fragment_source,
                                                                 EmbeddedShader::ShaderStage::FragmentShader,
                                                                 fragment_language,
                                                                 compiler_option,
                                                                 source_location);

            return RasterizerPipelineDesc(
                PipelineShaderDesc
                {
                    PipelineShaderStage::Vertex,
                    vertex_compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, compiler_option.enableBindless)
                },
                PipelineShaderDesc
                {
                    PipelineShaderStage::Fragment,
                    fragment_compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, compiler_option.enableBindless)
                }
            );
        }

        RasterizerPipelineDesc& set_rasterizer(RasterizerStateDesc value) noexcept
        {
            rasterizer = value;
            return *this;
        }

        RasterizerPipelineDesc& set_depth_stencil(DepthStencilStateDesc value) noexcept
        {
            depth_stencil = value;
            if (!value.depth_test_enabled && !value.stencil_test_enabled)
                depth_attachment.enabled = false;
            return *this;
        }

        RasterizerPipelineDesc& set_blend(BlendStateDesc value)
        {
            blend = std::move(value);
            return *this;
        }

        RasterizerPipelineDesc& set_multisample(MultisampleStateDesc value) noexcept
        {
            multisample = value;
            return *this;
        }

        RasterizerPipelineDesc& set_depth_attachment(DepthAttachmentDesc value) noexcept
        {
            depth_attachment = value;
            return *this;
        }

        RasterizerPipelineDesc& set_multiview_count(uint32_t value) noexcept
        {
            multiview_count = std::max(1u, value);
            return *this;
        }

        RasterizerPipelineDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
        }
    };

    struct RayTracingHitGroupDesc
    {
        RayTracingHitGroupKind kind = RayTracingHitGroupKind::Triangles;
        int32_t closest_hit_shader = -1;
        int32_t any_hit_shader = -1;
        int32_t intersection_shader = -1;
        std::string debug_name;

        static RayTracingHitGroupDesc triangles(int32_t closest_hit_shader,
                                                int32_t any_hit_shader = -1)
        {
            RayTracingHitGroupDesc desc;
            desc.kind = RayTracingHitGroupKind::Triangles;
            desc.closest_hit_shader = closest_hit_shader;
            desc.any_hit_shader = any_hit_shader;
            return desc;
        }

        static RayTracingHitGroupDesc procedural(int32_t intersection_shader,
                                                 int32_t closest_hit_shader = -1,
                                                 int32_t any_hit_shader = -1)
        {
            RayTracingHitGroupDesc desc;
            desc.kind = RayTracingHitGroupKind::Procedural;
            desc.intersection_shader = intersection_shader;
            desc.closest_hit_shader = closest_hit_shader;
            desc.any_hit_shader = any_hit_shader;
            return desc;
        }
    };

    struct RayTracingPipelineDesc
    {
        std::vector<PipelineShaderDesc> shaders;
        std::vector<RayTracingHitGroupDesc> hit_groups;

        uint32_t max_recursion_depth = 1;
        uint32_t max_payload_size = 0;
        uint32_t max_attribute_size = 8;

        std::string debug_name;

        uint32_t add_shader(RayTracingShaderDesc shader)
        {
            if (!shader.is_ray_tracing())
                throw std::invalid_argument("RayTracingPipelineDesc only accepts ray tracing shader stages.");

            shaders.push_back(std::move(shader));
            return static_cast<uint32_t>(shaders.size() - 1);
        }

        uint32_t add_ray_generation_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::RayGeneration, std::move(spirv)));
        }

        uint32_t add_miss_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Miss, std::move(spirv)));
        }

        uint32_t add_closest_hit_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::ClosestHit, std::move(spirv)));
        }

        uint32_t add_any_hit_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::AnyHit, std::move(spirv)));
        }

        uint32_t add_intersection_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Intersection, std::move(spirv)));
        }

        uint32_t add_callable_shader(std::vector<uint32_t> spirv)
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Callable, std::move(spirv)));
        }

        RayTracingPipelineDesc& add_hit_group(RayTracingHitGroupDesc hit_group)
        {
            hit_groups.push_back(std::move(hit_group));
            return *this;
        }

        RayTracingPipelineDesc& set_max_recursion_depth(uint32_t value) noexcept
        {
            max_recursion_depth = value == 0 ? 1 : value;
            return *this;
        }

        RayTracingPipelineDesc& set_max_payload_size(uint32_t value) noexcept
        {
            max_payload_size = value;
            return *this;
        }

        RayTracingPipelineDesc& set_max_attribute_size(uint32_t value) noexcept
        {
            max_attribute_size = value;
            return *this;
        }

        RayTracingPipelineDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
        }
    };*/



    // ================================================================
    // Pipeline Binding
    // ================================================================

    //template<typename T>
    //concept ReflectedBindingKey = requires(const T& t)
    //{
    //    t.byte_offset;
    //    t.type_size;
    //    t.bind_type;
    //    t.location;
    //};

    //struct BindingSlot
    //{
    //    uint64_t byte_offset = 0;
    //    uint32_t type_size = 0;
    //    int32_t bind_type = -1;
    //    uint32_t location = 0;

    //    template<ReflectedBindingKey T>
    //    static constexpr BindingSlot from(const T& key) noexcept
    //    {
    //        return 
    //        {
    //            key.byte_offset,
    //            key.type_size,
    //            key.bind_type,
    //            key.location
    //        };
    //    }
    //};

    //struct PipelineBindingScope
    //{
    //protected:
    //    virtual ~PipelineBindingScope() = default;

    //private:
    //    friend struct ResourceProxy;

    //    virtual void bind_push_constant(const BindingSlot& slot, const void* data, size_t size) = 0;
    //    virtual void bind_buffer(const BindingSlot &slot, const HardwareBuffer &buffer) = 0;
    //    virtual void bind_image(const BindingSlot &slot, const HardwareImage &image) = 0;
    //    /*virtual void bindResource(BindingSlot &, const TopLevelAccelerationStructure &)
    //    {
    //        throw std::runtime_error("This pipeline does not support acceleration structure binding.");
    //    }*/
    //};

    //struct ResourceProxy
    //{
    //public:
    //    ResourceProxy(PipelineBindingScope& pipeline, BindingSlot slot) noexcept : pipeline_(pipeline), slot_(slot) {}

    //    ResourceProxy& operator=(const ResourceProxy&) = delete;
    //    ResourceProxy& operator=(ResourceProxy&&) = delete;

    //    template <typename T>
    //    requires(!std::same_as<std::remove_cvref_t<T>, ResourceProxy>)
    //    ResourceProxy& operator=(const T& value)
    //    {
    //        using Value = std::remove_cvref_t<T>;

    //        if constexpr (std::same_as<Value, HardwareBuffer>)
    //        {
    //            pipeline_.bind_buffer(slot_, value);
    //        }
    //        else if constexpr (std::same_as<Value, HardwareImage>)
    //        {
    //            pipeline_.bind_image(slot_, value);
    //        }
    //        /*else if constexpr (std::same_as<Value, TopLevelAccelerationStructure>)
    //        {
    //            pipeline_.bindResource(slot_, value);
    //        }*/
    //        else
    //        {
    //            static_assert(HardwareTransferable<Value>, "Pipeline push constants must be trivially copyable non-pointer values.");
    //            pipeline_.bind_push_constant(slot_, &value, sizeof(Value));
    //        }

    //        return *this;
    //    }

    //private:
    //    PipelineBindingScope& pipeline_;
    //    BindingSlot slot_;
    //};

    //template<typename Derived>
    //struct ReflectedPipelineBindings
    //{
    //    template<ReflectedBindingKey ProxyType>
    //    ResourceProxy operator[](const ProxyType& proxy)
    //    {
    //        template <ReflectedBindingKey ProxyType>
    //        [[nodiscard]] ResourceProxy operator[](const ProxyType& proxy)
    //        {
    //            return ResourceProxy(static_cast<PipelineBindingScope&>(*static_cast<Derived*>(this)), BindingSlot::from(proxy));
    //        }
    //    }
    //};

    

    // ================================================================
    // Pipeline Runtime
    // ================================================================

    /*struct ComputePipeline : PipelineBindingScope, ReflectedPipelineBindings<ComputePipeline>
    {
    public:
        ComputePipeline();
        ComputePipeline(ComputePipelineDesc& desc, const std::source_location &source_location = std::source_location::current());

        ComputePipeline(const ComputePipeline& other);
        ComputePipeline(ComputePipeline&& other) noexcept;
        ~ComputePipeline();

        ComputePipeline& operator=(const ComputePipeline& other);
        ComputePipeline& operator=(ComputePipeline&& other) noexcept;
        ComputePipeline& operator()(uint16_t x, uint16_t y, uint16_t z);

    private:
        void bind_push_constant(const BindingSlot &slot, const void *data, size_t size) override
        {
            set_push_constant_direct(slot.byte_offset, data, size, slot.bind_type);
        }

        void bind_buffer(const BindingSlot &slot, const HardwareBuffer &buffer) override
        {
            set_resource_direct(slot.byte_offset, slot.type_size, buffer, slot.bind_type);
        }

        void bind_image(const BindingSlot &slot, const HardwareImage &image) override
        {
            set_resource_direct(slot.byte_offset, slot.type_size, image, slot.bind_type);
        }

        void set_push_constant_direct(uint64_t byte_offset, const void *data, size_t size, int32_t bind_type);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer &buffer, int32_t bind_type);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage &image, int32_t bind_type);

        mutable std::mutex compute_pipeline_mutex_;
        std::atomic<std::uintptr_t> compute_pipeline_id_;
    };

    struct RasterizerPipeline : PipelineBindingScope, ReflectedPipelineBindings<RasterizerPipeline>
    {
    public:
        RasterizerPipeline();
        RasterizerPipeline(RasterizerPipelineDesc& desc, const std::source_location& source_location = std::source_location::current());

        RasterizerPipeline(const RasterizerPipeline &other);
        RasterizerPipeline(RasterizerPipeline &&other) noexcept;
        ~RasterizerPipeline();

        RasterizerPipelineBase &operator=(const RasterizerPipelineBase &other);
        RasterizerPipelineBase &operator=(RasterizerPipelineBase &&other) noexcept;

        RasterizerPipelineBase &operator()(uint16_t width, uint16_t height);
        RasterizerPipelineBase &record(const HardwareBuffer &index_buffer, const HardwareBuffer &vertex_buffer);
        RasterizerPipelineBase &record(const HardwareBuffer &index_buffer, const HardwareBuffer &vertex_buffer, const DrawIndexedParams &params);

    private:
        void bind_push_constant(const BindingSlot &slot, const void *data, size_t size) override
        {
            set_push_constant_direct(slot.byte_offset, data, size, slot.bind_type);
        }

        void bind_buffer(const BindingSlot &slot, const HardwareBuffer &buffer) override
        {
            set_resource_direct(slot.byte_offset, slot.type_size, buffer, slot.bind_type);
        }

        void bind_image(const BindingSlot &slot, const HardwareImage &image) override
        {
            set_resource_direct(slot.byte_offset, slot.type_size, image, slot.bind_type, slot.location);
        }

        void set_push_constant_direct(uint64_t byte_offset, const void *data, size_t size, int32_t bind_type);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareBuffer &buffer, int32_t bind_type);
        void set_resource_direct(uint64_t byte_offset, uint32_t type_size, const HardwareImage &image, int32_t bind_type, uint32_t location = 0);

        mutable std::mutex rasterizer_pipeline_mutex_;
        std::atomic<std::uintptr_t> rasterizer_pipeline_id_;
    };

    struct RayTracingPipelineBase : PipelineBindingScope, ReflectedPipelineBindings<RayTracingPipelineBase>
    {
    };*/



    // ================================================================
    // HardwareExecutor
    // ================================================================

    //struct HardwareExecutor
    //{
    //public:
    //    HardwareExecutor();
    //    HardwareExecutor(const HardwareExecutor &other);
    //    HardwareExecutor(HardwareExecutor &&other) noexcept;
    //    ~HardwareExecutor();

    //    HardwareExecutor &operator=(const HardwareExecutor &other);
    //    HardwareExecutor &operator=(HardwareExecutor &&other) noexcept;

    //    HardwareExecutor &operator<<(ComputePipeline &computePipeline);
    //    HardwareExecutor &operator<<(RasterizerPipelineBase &rasterizerPipeline);
    //    HardwareExecutor &operator<<(HardwareExecutor &other);
    //    HardwareExecutor &operator<<(const CopyCommand &cmd);

    //    HardwareExecutor &wait(HardwareExecutor &other);
    //    HardwareExecutor &commit();

    //    // ========== 延迟释放相关接口 ==========
    //    /// @brief 等待所有延迟释放的资源完成（阻塞）
    //    void waitForDeferredResources();

    //    /// @brief 手动触发一次清理（非阻塞）
    //    void cleanupDeferredResources();

    //private:
    //    mutable std::mutex executorMutex;
    //    std::atomic<std::uintptr_t> executorID;
    //};



    // ================================================================
    // HardwareDisplayer
    // ================================================================

    /*struct HardwareDisplayer
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

    private:
        std::atomic<std::uintptr_t> displaySurfaceID;
        mutable std::mutex displayerMutex;
    };*/
}
