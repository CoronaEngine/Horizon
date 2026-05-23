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

namespace Corona::Horizon
{
    // ================================================================
    // Forward Declarations
    // ================================================================

    struct HardwareBuffer;
    struct HardwareImage;
    struct HardwareImageLayerSelector;
    struct HardwarePushConstant;

    //struct BottomLevelAccelerationStructure;
    //struct TopLevelAccelerationStructure;

    struct PipelineState;
    struct PipelineBindingScope;
    struct ResourceProxy;

    struct ComputePipelineBase;
    struct RasterizerPipelineBase;
    struct RayTracingPipelineBase;

    struct HardwareExecutor;
    struct HardwareDisplayer;

    struct BindingSlot;



    // ================================================================
    // Validation
    // ================================================================

    // 开启校验层，后面可以修改或者删除

    struct HardwareValidationConfig
    {
        bool enabled = true;
        bool throw_on_error = false;
    };

    void set_hardware_validation_config(const HardwareValidationConfig &config);



    // ================================================================
    // HardwareBuffer
    // ================================================================

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
        CpuAccessMode cpu_access = CpuAccessMode::None;
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

        template <HardwareTransferable T>
        static HardwareBufferDesc typed(uint64_t count, BufferUsageFlags usage, std::string name = {}, HardwareBufferOptions options = {})
        {
            // 如果 count 很大，乘法先溢出了，validation 后面也看不出来。
            if (count > std::numeric_limits<uint64_t>::max() / sizeof(T))
                throw std::overflow_error("HardwareBufferDesc total byte size overflow.");

            HardwareBufferDesc desc;
            desc.element_count = count;
            desc.element_size = uint32_t(sizeof(T));
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        template <HardwareTransferable T>
        static HardwareBufferDesc vertex(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferDst | BufferUsageFlags::Vertex, std::move(name), options);
        }

        template <HardwareIndexType T>
        static HardwareBufferDesc index(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferDst | BufferUsageFlags::Index, std::move(name), options);
        }

        template <HardwareTransferable T>
        static HardwareBufferDesc uniform(std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(1, BufferUsageFlags::TransferDst | BufferUsageFlags::Uniform, std::move(name), options);
        }

        template <HardwareTransferable T>
        static HardwareBufferDesc storage(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferSrc | BufferUsageFlags::TransferDst | BufferUsageFlags::Storage, std::move(name), options);
        }
    };

    struct HardwareBuffer
    {
    public:
        HardwareBuffer();
        HardwareBuffer(const HardwareBufferDesc &desc, std::span<const std::byte> upload_data = {});

        HardwareBuffer(const HardwareBuffer& other);
        HardwareBuffer(HardwareBuffer&& other) noexcept;
        ~HardwareBuffer();

        HardwareBuffer& operator=(const HardwareBuffer& other);
        HardwareBuffer& operator=(HardwareBuffer&& other) noexcept;
        explicit operator bool() const;

        [[nodiscard]] std::uintptr_t get_buffer_id() const noexcept { return buffer_id; }
        [[nodiscard]] uint64_t get_element_size() const;
        [[nodiscard]] uint64_t get_element_count() const;
        [[nodiscard]] void* get_mapped_data() const;

        bool write_bytes(std::span<const std::byte> data, uint64_t offset = 0) const;
        bool read_bytes(std::span<std::byte> output, uint64_t offset = 0) const;

        template <HardwareTransferable T>
        bool write(std::span<const T> data, uint64_t offset = 0) const
        {
            return write_bytes(std::as_bytes(data), offset);
        }

        template <HardwareTransferable T>
        bool write_value(const T& value, uint64_t offset = 0) const
        {
            return write(std::span<const T>(&value, 1), offset);
        }

        template <HardwareTransferable T>
        requires (!std::is_const_v<T>)
        bool read(std::span<T> output, uint64_t offset = 0) const
        {
            return read_bytes(std::as_writable_bytes(output), offset);
        }

        static HardwareBuffer from_bytes(std::span<const std::byte> data, uint32_t element_size, BufferUsageFlags usage, std::string name = {}, HardwareBufferOptions options = {})
        {
            if (element_size == 0)
                throw std::invalid_argument("element_size must not be zero.");

            if (data.size_bytes() % element_size != 0)
                throw std::invalid_argument("data size must be divisible by element_size.");

            HardwareBufferDesc desc;
            desc.element_count = uint64_t(data.size_bytes() / element_size);
            desc.element_size = element_size;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            desc.apply(options);
            return HardwareBuffer(desc, data);
        }

        template <HardwareTransferable T>
        static HardwareBuffer vertex(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::vertex<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareTransferable<std::ranges::range_value_t<Range>>
        static HardwareBuffer vertex(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return vertex<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        template <HardwareIndexType T>
        static HardwareBuffer index(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::index<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareIndexType<std::ranges::range_value_t<Range>>
        static HardwareBuffer index(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return index<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        template <HardwareTransferable T>
        static HardwareBuffer uniform(const T& value, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::uniform<T>(std::move(name), options), std::as_bytes(std::span<const T>(&value, 1)));
        }

        template <HardwareTransferable T>
        static HardwareBuffer storage(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::storage<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareTransferable<std::ranges::range_value_t<Range>>
        static HardwareBuffer storage(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return storage<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        [[nodiscard]] BufferCopyCommand copy_to(const HardwareBuffer& dst, BufferRange src = BufferRange::entire(), uint64_t dst_offset = 0) const;
        [[nodiscard]] BufferToImageCommand copy_to(const HardwareImage& dst, uint64_t buffer_offset = 0, uint32_t image_layer = 0, uint32_t image_mip = 0) const;
        [[nodiscard]] uint32_t store_descriptor() const;
        static HardwareBuffer import_external(const ExternalMemoryHandle &handle, const HardwareBufferDesc &desc);
        [[nodiscard]] ExternalMemoryHandle export_external() const;

    private:
        std::atomic<std::uintptr_t> buffer_id;
        mutable std::mutex buffer_mutex;

        friend class HardwareImage;
    };



    // ================================================================
    // HardwareImage
    // ================================================================

    struct HardwareImageDesc
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
    }

    
    
    // ================================================================
    // HardwarePushConstant
    // ================================================================

    inline constexpr uint32_t kPortablePushConstantByteSize = 128;

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
    };



    // ================================================================
    // Pipeline Descriptors
    // ================================================================

    struct PipelineShaderDesc
    {
        PipelineShaderStage stage = PipelineShaderStage::Compute;
        std::vector<uint32_t> spirv;
        std::string source;
        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL;
        std::string entry_point = "main";
        std::string debug_name;

        static PipelineShaderDesc from_spirv(PipelineShaderStage stage,
                                             std::vector<uint32_t> code,
                                             std::string entry_point = "main")
        {
            PipelineShaderDesc desc;
            desc.stage = stage;
            desc.spirv = std::move(code);
            desc.language = EmbeddedShader::ShaderLanguage::SpirV;
            desc.entry_point = std::move(entry_point);
            return desc;
        }

        static PipelineShaderDesc from_source(PipelineShaderStage stage,
                                              std::string code,
                                              EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
                                              std::string entry_point = "main")
        {
            PipelineShaderDesc desc;
            desc.stage = stage;
            desc.source = std::move(code);
            desc.language = language;
            desc.entry_point = std::move(entry_point);
            return desc;
        }
    };

    struct ComputePipelineDesc
    {
        PipelineShaderDesc compute_shader;
        PipelineReflectionDesc reflection;
        std::string debug_name;

        ComputePipelineDesc& set_shader(PipelineShaderDesc shader)
        {
            compute_shader = std::move(shader);
            compute_shader.stage = PipelineShaderStage::Compute;
            return *this;
        }

        ComputePipelineDesc& set_spirv(std::vector<uint32_t> spirv, std::string entry_point = "main")
        {
            return set_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Compute,
                                                             std::move(spirv),
                                                             std::move(entry_point)));
        }

        ComputePipelineDesc& set_source(std::string source,
                                        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
                                        std::string entry_point = "main")
        {
            return set_shader(PipelineShaderDesc::from_source(PipelineShaderStage::Compute,
                                                              std::move(source),
                                                              language,
                                                              std::move(entry_point)));
        }

        ComputePipelineDesc& set_reflection(PipelineReflectionDesc value) noexcept
        {
            reflection = value;
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
        RenderTargetLayoutDesc render_target_layout;
        PipelineReflectionDesc reflection;

        std::string debug_name;

        RasterizerPipelineDesc& set_vertex_shader(PipelineShaderDesc shader)
        {
            vertex_shader = std::move(shader);
            vertex_shader.stage = PipelineShaderStage::Vertex;
            return *this;
        }

        RasterizerPipelineDesc& set_fragment_shader(PipelineShaderDesc shader)
        {
            fragment_shader = std::move(shader);
            fragment_shader.stage = PipelineShaderStage::Fragment;
            return *this;
        }

        RasterizerPipelineDesc& set_vertex_spirv(std::vector<uint32_t> spirv,
                                                 std::string entry_point = "main")
        {
            return set_vertex_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Vertex,
                                                                    std::move(spirv),
                                                                    std::move(entry_point)));
        }

        RasterizerPipelineDesc& set_fragment_spirv(std::vector<uint32_t> spirv,
                                                   std::string entry_point = "main")
        {
            return set_fragment_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Fragment,
                                                                      std::move(spirv),
                                                                      std::move(entry_point)));
        }

        RasterizerPipelineDesc& set_vertex_source(std::string source,
                                                  EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
                                                  std::string entry_point = "main")
        {
            return set_vertex_shader(PipelineShaderDesc::from_source(PipelineShaderStage::Vertex,
                                                                     std::move(source),
                                                                     language,
                                                                     std::move(entry_point)));
        }

        RasterizerPipelineDesc& set_fragment_source(std::string source,
                                                    EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
                                                    std::string entry_point = "main")
        {
            return set_fragment_shader(PipelineShaderDesc::from_source(PipelineShaderStage::Fragment,
                                                                       std::move(source),
                                                                       language,
                                                                       std::move(entry_point)));
        }

        RasterizerPipelineDesc& set_rasterizer(RasterizerStateDesc value) noexcept
        {
            rasterizer = value;
            return *this;
        }

        RasterizerPipelineDesc& set_depth_stencil(DepthStencilStateDesc value) noexcept
        {
            depth_stencil = value;
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

        RasterizerPipelineDesc& set_render_target_layout(RenderTargetLayoutDesc value)
        {
            render_target_layout = std::move(value);
            return *this;
        }

        RasterizerPipelineDesc& set_multiview_count(uint32_t value) noexcept
        {
            render_target_layout.set_multiview_count(value);
            return *this;
        }

        RasterizerPipelineDesc& set_reflection(PipelineReflectionDesc value) noexcept
        {
            reflection = value;
            return *this;
        }

        RasterizerPipelineDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
        }
    };

    struct RayTracingShaderDesc
    {
        RayTracingShaderStage stage = RayTracingShaderStage::RayGeneration;
        std::vector<uint32_t> spirv;
        std::string source;
        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL;
        std::string entry_point = "main";
        std::string debug_name;

        static RayTracingShaderDesc from_spirv(RayTracingShaderStage stage,
                                               std::vector<uint32_t> code,
                                               std::string entry_point = "main")
        {
            RayTracingShaderDesc desc;
            desc.stage = stage;
            desc.spirv = std::move(code);
            desc.language = EmbeddedShader::ShaderLanguage::SpirV;
            desc.entry_point = std::move(entry_point);
            return desc;
        }

        static RayTracingShaderDesc from_source(RayTracingShaderStage stage,
                                                std::string code,
                                                EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
                                                std::string entry_point = "main")
        {
            RayTracingShaderDesc desc;
            desc.stage = stage;
            desc.source = std::move(code);
            desc.language = language;
            desc.entry_point = std::move(entry_point);
            return desc;
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
        std::vector<RayTracingShaderDesc> shaders;
        std::vector<RayTracingHitGroupDesc> hit_groups;

        uint32_t max_recursion_depth = 1;
        uint32_t max_payload_size = 0;
        uint32_t max_attribute_size = 8;

        PipelineReflectionDesc reflection;
        std::string debug_name;

        uint32_t add_shader(RayTracingShaderDesc shader)
        {
            shaders.push_back(std::move(shader));
            return static_cast<uint32_t>(shaders.size() - 1);
        }

        uint32_t add_ray_generation_shader(std::vector<uint32_t> spirv, std::string entry_point = "main")
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::RayGeneration,
                                                             std::move(spirv),
                                                             std::move(entry_point)));
        }

        uint32_t add_miss_shader(std::vector<uint32_t> spirv, std::string entry_point = "main")
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Miss,
                                                             std::move(spirv),
                                                             std::move(entry_point)));
        }

        uint32_t add_closest_hit_shader(std::vector<uint32_t> spirv, std::string entry_point = "main")
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::ClosestHit,
                                                             std::move(spirv),
                                                             std::move(entry_point)));
        }

        uint32_t add_any_hit_shader(std::vector<uint32_t> spirv, std::string entry_point = "main")
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::AnyHit,
                                                             std::move(spirv),
                                                             std::move(entry_point)));
        }

        uint32_t add_intersection_shader(std::vector<uint32_t> spirv, std::string entry_point = "main")
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Intersection,
                                                             std::move(spirv),
                                                             std::move(entry_point)));
        }

        uint32_t add_callable_shader(std::vector<uint32_t> spirv,std::string entry_point = "main")
        {
            return add_shader(PipelineShaderDesc::from_spirv(PipelineShaderStage::Callable,
                                                             std::move(spirv),
                                                             std::move(entry_point)));
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

        RayTracingPipelineDesc& set_reflection(PipelineReflectionDesc value) noexcept
        {
            reflection = value;
            return *this;
        }
    };



// ================================================================
// Pipeline Binding Core
// ================================================================

namespace detail
{
template<typename T>
concept ReflectedBindingKey = requires(const T& t)
{
    t.byteOffset;
    t.typeSize;
    t.bindType;
    t.location;
};

struct BindingSlot
{
    uint64_t byteOffset = 0;
    uint32_t typeSize = 0;
    int32_t bindType = -1;
    uint32_t location = 0;

    template<ReflectedBindingKey T>
    static constexpr BindingSlot from(const T& key) noexcept
    {
        return {
            key.byteOffset,
            key.typeSize,
            key.bindType,
            key.location
        };
    }
};
}

struct ResourceProxy;

struct PipelineBindingScope
{
protected:
    virtual ~PipelineBindingScope() = default;

private:
    friend struct ResourceProxy;

    virtual void bindPushConstant(
        const detail::BindingSlot& slot,
        const void* data,
        size_t size) = 0;

    virtual void bindResource(
        const detail::BindingSlot& slot,
        const HardwareBuffer& buffer) = 0;

    virtual void bindResource(
        const detail::BindingSlot& slot,
        const HardwareImage& image) = 0;

    virtual void bindResource(
        const detail::BindingSlot&,
        const TopLevelAccelerationStructure&)
    {
        throw std::runtime_error("This pipeline does not support acceleration structure binding.");
    }
};

template<typename Derived>
struct ReflectedPipelineBindings
{
    template<detail::ReflectedBindingKey ProxyType>
    ResourceProxy operator[](const ProxyType& proxy)
    {
        return ResourceProxy(
            static_cast<PipelineBindingScope*>(static_cast<Derived*>(this)),
            detail::BindingSlot::from(proxy)
        );
    }
};

struct ResourceProxy
{
public:
    ResourceProxy(PipelineBindingScope* pipeline, detail::BindingSlot slot)
        : pipeline_(pipeline)
        , slot_(slot)
    {
    }

    template<typename T>
    ResourceProxy& operator=(const T& value)
    {
        using Value = std::remove_cvref_t<T>;

        if constexpr (std::same_as<Value, HardwareBuffer>)
        {
            pipeline_->bindResource(slot_, value);
        }
        else if constexpr (std::same_as<Value, HardwareImage>)
        {
            pipeline_->bindResource(slot_, value);
        }
        else if constexpr (std::same_as<Value, TopLevelAccelerationStructure>)
        {
            pipeline_->bindResource(slot_, value);
        }
        else if constexpr (!std::same_as<Value, ResourceProxy>)
        {
            pipeline_->bindPushConstant(slot_, &value, sizeof(T));
        }

        return *this;
    }

private:
    PipelineBindingScope* pipeline_ = nullptr;
    detail::BindingSlot slot_{};
};

struct PipelineState
{
public:
    [[nodiscard]] uintptr_t getPipelineID() const noexcept
    {
        return pipelineID_.load(std::memory_order_acquire);
    }

protected:
    mutable std::mutex pipelineMutex_;
    std::atomic<std::uintptr_t> pipelineID_{0};
    std::vector<EmbeddedShader::AutoBindEntry> autoBindEntries_;
};

// ================================================================
// Compute Pipeline Base
// ================================================================

struct ComputePipelineBase
    : PipelineState
    , PipelineBindingScope
    , ReflectedPipelineBindings<ComputePipelineBase>
{
public:
    ComputePipelineBase();

    ComputePipelineBase(
        const std::string& shaderCode,
        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
        const std::source_location& sourceLocation = std::source_location::current());

    ComputePipelineBase(
        const std::vector<uint32_t>& spirV,
        const std::source_location& sourceLocation = std::source_location::current());

    template<typename F>
        requires std::invocable<F> && (!std::is_convertible_v<F, std::string>)
    ComputePipelineBase(
        F&& computeShaderCode,
        ktm::uvec3 numthreads = ktm::uvec3(1),
        EmbeddedShader::CompilerOption compilerOption = {},
        std::source_location sourceLocation = std::source_location::current());

    ComputePipelineBase(const ComputePipelineBase& other);
    ComputePipelineBase(ComputePipelineBase&& other) noexcept;
    ~ComputePipelineBase();

    ComputePipelineBase& operator=(const ComputePipelineBase& other);
    ComputePipelineBase& operator=(ComputePipelineBase&& other) noexcept;

    ComputePipelineBase& operator()(uint16_t x, uint16_t y, uint16_t z);

    [[nodiscard]] uintptr_t getComputePipelineID() const noexcept
    {
        return getPipelineID();
    }

private:
    void bindPushConstant(const detail::BindingSlot& slot, const void* data, size_t size) override
    {
        setPushConstantDirect(slot.byteOffset, data, size, slot.bindType);
    }

    void bindResource(const detail::BindingSlot& slot, const HardwareBuffer& buffer) override
    {
        setResourceDirect(slot.byteOffset, slot.typeSize, buffer, slot.bindType);
    }

    void bindResource(const detail::BindingSlot& slot, const HardwareImage& image) override
    {
        setResourceDirect(slot.byteOffset, slot.typeSize, image, slot.bindType);
    }

    void setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer& buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage& image, int32_t bindType);
};


// ================================================================
// Rasterizer Pipeline Base
// ================================================================

struct RasterizerPipelineBase
    : PipelineState
    , PipelineBindingScope
    , ReflectedPipelineBindings<RasterizerPipelineBase>
{
public:
    RasterizerPipelineBase();

    RasterizerPipelineBase(
        std::string vertexShaderCode,
        std::string fragmentShaderCode,
        uint32_t multiviewCount = 1,
        EmbeddedShader::ShaderLanguage vertexShaderLanguage = EmbeddedShader::ShaderLanguage::GLSL,
        EmbeddedShader::ShaderLanguage fragmentShaderLanguage = EmbeddedShader::ShaderLanguage::GLSL,
        const std::source_location& sourceLocation = std::source_location::current());

    RasterizerPipelineBase(
        const std::vector<uint32_t>& vertexSpirV,
        const std::vector<uint32_t>& fragmentSpirV,
        uint32_t multiviewCount = 1,
        const std::source_location& sourceLocation = std::source_location::current());

    template<typename VF, typename FF>
        requires (!std::is_convertible_v<VF, std::string>)
              && (!std::is_convertible_v<FF, std::string>)
              && (!std::is_same_v<std::remove_cvref_t<VF>, std::vector<uint32_t>>)
              && (!std::is_same_v<std::remove_cvref_t<FF>, std::vector<uint32_t>>)
    RasterizerPipelineBase(
        VF&& vertexShaderCode,
        FF&& fragmentShaderCode,
        uint32_t multiviewCount = 1,
        EmbeddedShader::CompilerOption compilerOption = {},
        std::source_location sourceLocation = std::source_location::current());

    RasterizerPipelineBase(const RasterizerPipelineBase& other);
    RasterizerPipelineBase(RasterizerPipelineBase&& other) noexcept;
    ~RasterizerPipelineBase();

    RasterizerPipelineBase& operator=(const RasterizerPipelineBase& other);
    RasterizerPipelineBase& operator=(RasterizerPipelineBase&& other) noexcept;

    void setDepthEnabled(bool enabled);
    void setDepthImage(HardwareImage& depthImage);
    [[nodiscard]] HardwareImage getDepthImage();

    RasterizerPipelineBase& operator()(uint16_t width, uint16_t height);
    RasterizerPipelineBase& record(const HardwareBuffer& indexBuffer, const HardwareBuffer& vertexBuffer);
    RasterizerPipelineBase& record(const HardwareBuffer& indexBuffer, const HardwareBuffer& vertexBuffer, const DrawIndexedParams& params);

    template<typename T>
    RasterizerPipelineBase& bindRenderTarget(uint32_t location, EmbeddedShader::Texture2DProxy<T>& proxy)
    {
        autoBindEntries_.push_back({
            &proxy.boundResource_,
            0,
            0,
            static_cast<int32_t>(EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs),
            location
        });
        return *this;
    }

    template<typename... Ts>
    RasterizerPipelineBase& bindOutputTargets(EmbeddedShader::Texture2DProxy<Ts>&... targets)
    {
        uint32_t location = 0;
        (bindRenderTarget(location++, targets), ...);
        return *this;
    }

    [[nodiscard]] uintptr_t getRasterizerPipelineID() const noexcept
    {
        return getPipelineID();
    }

    [[nodiscard]] uintptr_t getGraphicsPipelineID() const noexcept
    {
        return getPipelineID();
    }

private:
    void bindPushConstant(const detail::BindingSlot& slot, const void* data, size_t size) override
    {
        setPushConstantDirect(slot.byteOffset, data, size, slot.bindType);
    }

    void bindResource(const detail::BindingSlot& slot, const HardwareBuffer& buffer) override
    {
        setResourceDirect(slot.byteOffset, slot.typeSize, buffer, slot.bindType);
    }

    void bindResource(const detail::BindingSlot& slot, const HardwareImage& image) override
    {
        setResourceDirect(slot.byteOffset, slot.typeSize, image, slot.bindType, slot.location);
    }

    void setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer& buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage& image, int32_t bindType, uint32_t location = 0);
};

// ================================================================
// Ray Tracing Pipeline Base
// ================================================================

struct RayTracingPipelineBase
    : PipelineState
    , PipelineBindingScope
    , ReflectedPipelineBindings<RayTracingPipelineBase>
{
public:
    RayTracingPipelineBase();

    explicit RayTracingPipelineBase(
        RayTracingPipelineDesc desc,
        const std::source_location& sourceLocation = std::source_location::current());

    RayTracingPipelineBase(
        const std::vector<uint32_t>& rayGenerationSpirV,
        const std::vector<uint32_t>& missSpirV,
        const std::vector<uint32_t>& closestHitSpirV,
        uint32_t maxRecursionDepth = 1,
        const std::source_location& sourceLocation = std::source_location::current());

    RayTracingPipelineBase(const RayTracingPipelineBase& other);
    RayTracingPipelineBase(RayTracingPipelineBase&& other) noexcept;
    ~RayTracingPipelineBase();

    RayTracingPipelineBase& operator=(const RayTracingPipelineBase& other);
    RayTracingPipelineBase& operator=(RayTracingPipelineBase&& other) noexcept;

    RayTracingPipelineBase& operator()(uint32_t width, uint32_t height = 1, uint32_t depth = 1);
    RayTracingPipelineBase& record(const RayDispatchDesc& dispatch);

    [[nodiscard]] uintptr_t getRayTracingPipelineID() const noexcept
    {
        return getPipelineID();
    }

private:
    void bindPushConstant(const detail::BindingSlot& slot, const void* data, size_t size) override
    {
        setPushConstantDirect(slot.byteOffset, data, size, slot.bindType);
    }

    void bindResource(const detail::BindingSlot& slot, const HardwareBuffer& buffer) override
    {
        setResourceDirect(slot.byteOffset, slot.typeSize, buffer, slot.bindType);
    }

    void bindResource(const detail::BindingSlot& slot, const HardwareImage& image) override
    {
        setResourceDirect(slot.byteOffset, slot.typeSize, image, slot.bindType, slot.location);
    }

    void bindResource(const detail::BindingSlot& slot, const TopLevelAccelerationStructure& accelerationStructure) override
    {
        setAccelerationStructureDirect(slot.byteOffset, slot.typeSize, accelerationStructure, slot.bindType);
    }

    void setPushConstantDirect(uint64_t byteOffset, const void* data, size_t size, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareBuffer& buffer, int32_t bindType);
    void setResourceDirect(uint64_t byteOffset, uint32_t typeSize, const HardwareImage& image, int32_t bindType, uint32_t location = 0);
    void setAccelerationStructureDirect(uint64_t byteOffset, uint32_t typeSize, const TopLevelAccelerationStructure& accelerationStructure, int32_t bindType);
};

// ================================================================
// BoundField Implementation
// ================================================================

} // namespace Corona::Horizon

namespace EmbeddedShader
{
template<typename PipelineType>
template<typename T>
BoundField<PipelineType>& BoundField<PipelineType>::operator=(const T& value)
{
    ::Corona::Horizon::ResourceProxy proxy(
        static_cast<::Corona::Horizon::PipelineBindingScope*>(pipeline_),
        ::Corona::Horizon::detail::BindingSlot{byteOffset, typeSize, bindType, location}
    );

    proxy = value;
    return *this;
}
}

namespace Corona::Horizon
{


// ================================================================
// Pipeline Compiler Hooks
// ================================================================

void computePipelineInitFromCompiler(
    std::atomic<std::uintptr_t>& pipelineID,
    const EmbeddedShader::ShaderCodeCompiler& compiler,
    const std::source_location& src);

void rasterizerPipelineInitFromCompiler(
    std::atomic<std::uintptr_t>& pipelineID,
    const EmbeddedShader::ShaderCodeCompiler& vertexCompiler,
    const EmbeddedShader::ShaderCodeCompiler& fragmentCompiler,
    uint32_t multiviewCount,
    const std::source_location& src);


// ================================================================
// Compute Pipeline Template Constructor
// ================================================================

template<typename F>
    requires std::invocable<F> && (!std::is_convertible_v<F, std::string>)
ComputePipelineBase::ComputePipelineBase(
    F&& computeShaderCode,
    ktm::uvec3 numthreads,
    EmbeddedShader::CompilerOption compilerOption,
    std::source_location sourceLocation)
{
    auto pipelineObj = EmbeddedShader::ComputePipelineObject::compile(
        std::forward<F>(computeShaderCode),
        numthreads,
        compilerOption,
        sourceLocation
    );

    autoBindEntries_ = std::move(pipelineObj.autoBindEntries);

    computePipelineInitFromCompiler(
        pipelineID_,
        *pipelineObj.compute,
        sourceLocation
    );
}


// ================================================================
// Rasterizer Pipeline Template Constructor
// ================================================================

template<typename VF, typename FF>
    requires (!std::is_convertible_v<VF, std::string>)
          && (!std::is_convertible_v<FF, std::string>)
          && (!std::is_same_v<std::remove_cvref_t<VF>, std::vector<uint32_t>>)
          && (!std::is_same_v<std::remove_cvref_t<FF>, std::vector<uint32_t>>)
RasterizerPipelineBase::RasterizerPipelineBase(
    VF&& vertexShaderCode,
    FF&& fragmentShaderCode,
    uint32_t multiviewCount,
    EmbeddedShader::CompilerOption compilerOption,
    std::source_location sourceLocation)
{
    auto pipelineObj = EmbeddedShader::RasterizedPipelineObject::compile(
        std::forward<VF>(vertexShaderCode),
        std::forward<FF>(fragmentShaderCode),
        compilerOption,
        sourceLocation
    );

    autoBindEntries_ = std::move(pipelineObj.autoBindEntries);

    rasterizerPipelineInitFromCompiler(
        pipelineID_,
        *pipelineObj.vertex,
        *pipelineObj.fragment,
        multiviewCount,
        sourceLocation
    );
}


// ================================================================
// ComputePipeline Facade
// ================================================================

template<typename CS = void>
struct ComputePipeline;

template<>
struct ComputePipeline<void> : ComputePipelineBase
{
    using ComputePipelineBase::ComputePipelineBase;
};

template<typename CS>
struct ComputePipeline
    : ComputePipelineBase
    , CS::template Bindings<ComputePipelineBase>
{
    using CSBindings = typename CS::template Bindings<ComputePipelineBase>;

    explicit ComputePipeline(
        const std::source_location& sourceLocation = std::source_location::current())
        : ComputePipelineBase(CS::spirv, sourceLocation)
        , CSBindings(static_cast<ComputePipelineBase*>(this))
    {
    }

    ComputePipeline(const ComputePipeline& other)
        : ComputePipelineBase(other)
        , CSBindings(static_cast<ComputePipelineBase*>(this))
    {
    }

    ComputePipeline(ComputePipeline&& other) noexcept
        : ComputePipelineBase(std::move(other))
        , CSBindings(static_cast<ComputePipelineBase*>(this))
    {
    }

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

template<typename F>
ComputePipeline(
    F&&,
    ktm::uvec3,
    EmbeddedShader::CompilerOption = {},
    std::source_location = std::source_location::current())
    -> ComputePipeline<>;

ComputePipeline(
    const std::string&,
    EmbeddedShader::ShaderLanguage,
    const std::source_location&)
    -> ComputePipeline<>;

ComputePipeline(
    const std::vector<uint32_t>&,
    const std::source_location&)
    -> ComputePipeline<>;


// ================================================================
// RasterizerPipeline Facade
// ================================================================

template<typename VS = void, typename FS = void>
struct RasterizerPipeline;

template<>
struct RasterizerPipeline<void, void> : RasterizerPipelineBase
{
    using RasterizerPipelineBase::RasterizerPipelineBase;
};

template<typename VS, typename FS>
struct RasterizerPipeline
    : RasterizerPipelineBase
    , VS::template ResourceBindings<RasterizerPipelineBase>
    , FS::template OutputBindings<RasterizerPipelineBase>
{
    static_assert(
        VS::pushConstantBlockSize == FS::pushConstantBlockSize,
        "VS and FS push constant block sizes must match");

    static_assert(
        VS::uniformBufferBlockSize == FS::uniformBufferBlockSize,
        "VS and FS uniform buffer block sizes must match");

    using VSResources = typename VS::template ResourceBindings<RasterizerPipelineBase>;
    using FSOutputs = typename FS::template OutputBindings<RasterizerPipelineBase>;

    explicit RasterizerPipeline(
        uint32_t multiviewCount = 1,
        const std::source_location& sourceLocation = std::source_location::current())
        : RasterizerPipelineBase(VS::spirv, FS::spirv, multiviewCount, sourceLocation)
        , VSResources(static_cast<RasterizerPipelineBase*>(this))
        , FSOutputs(static_cast<RasterizerPipelineBase*>(this))
    {
    }

    RasterizerPipeline(const RasterizerPipeline& other)
        : RasterizerPipelineBase(other)
        , VSResources(static_cast<RasterizerPipelineBase*>(this))
        , FSOutputs(static_cast<RasterizerPipelineBase*>(this))
    {
    }

    RasterizerPipeline(RasterizerPipeline&& other) noexcept
        : RasterizerPipelineBase(std::move(other))
        , VSResources(static_cast<RasterizerPipelineBase*>(this))
        , FSOutputs(static_cast<RasterizerPipelineBase*>(this))
    {
    }

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

template<typename VF, typename FF>
RasterizerPipeline(
    VF&&,
    FF&&,
    uint32_t = 1,
    EmbeddedShader::CompilerOption = {},
    std::source_location = std::source_location::current())
    -> RasterizerPipeline<>;

RasterizerPipeline(
    std::string,
    std::string,
    uint32_t,
    EmbeddedShader::ShaderLanguage,
    EmbeddedShader::ShaderLanguage,
    const std::source_location&)
    -> RasterizerPipeline<>;

RasterizerPipeline(
    const std::vector<uint32_t>&,
    const std::vector<uint32_t>&,
    uint32_t,
    const std::source_location&)
    -> RasterizerPipeline<>;


// ================================================================
// RayTracingPipeline Facade
// ================================================================

template<typename Shader, RayTracingShaderStage Stage>
struct RayTracingShaderModule
{
    static constexpr RayTracingShaderStage rayTracingStage = Stage;
    static constexpr const char* entryPoint = "main";
    static inline const auto& spirv = Shader::spirv;

    template<typename P>
    using ResourceBindings = typename Shader::template ResourceBindings<P>;
};

template<typename Shader>
using RayGenerationShader = RayTracingShaderModule<Shader, RayTracingShaderStage::RayGeneration>;

template<typename Shader>
using MissShader = RayTracingShaderModule<Shader, RayTracingShaderStage::Miss>;

template<typename Shader>
using ClosestHitShader = RayTracingShaderModule<Shader, RayTracingShaderStage::ClosestHit>;

template<typename Shader>
using AnyHitShader = RayTracingShaderModule<Shader, RayTracingShaderStage::AnyHit>;

template<typename Shader>
using IntersectionShader = RayTracingShaderModule<Shader, RayTracingShaderStage::Intersection>;

template<typename Shader>
using CallableShader = RayTracingShaderModule<Shader, RayTracingShaderStage::Callable>;

namespace detail
{
template<typename Shader>
concept RayTracingShaderModuleLike = requires
{
    Shader::rayTracingStage;
    Shader::spirv;
};

template<typename Shader>
std::string rayTracingEntryPoint()
{
    if constexpr (requires { Shader::entryPoint; })
    {
        return Shader::entryPoint;
    }
    else
    {
        return "main";
    }
}

template<RayTracingShaderModuleLike Shader>
uint32_t appendRayTracingShader(RayTracingPipelineDesc& desc)
{
    return desc.addShader(RayTracingShaderDesc::fromSpirV(
        Shader::rayTracingStage,
        Shader::spirv,
        rayTracingEntryPoint<Shader>()
    ));
}

template<RayTracingShaderModuleLike Shader>
void appendAutoHitGroup(RayTracingPipelineDesc& desc, uint32_t shaderIndex)
{
    constexpr auto stage = Shader::rayTracingStage;

    if constexpr (stage == RayTracingShaderStage::ClosestHit)
    {
        desc.addHitGroup(RayTracingHitGroupDesc::triangles(
            static_cast<int32_t>(shaderIndex)
        ));
    }
    else if constexpr (stage == RayTracingShaderStage::AnyHit)
    {
        RayTracingHitGroupDesc group;
        group.kind = RayTracingHitGroupKind::Triangles;
        group.anyHitShader = static_cast<int32_t>(shaderIndex);
        desc.addHitGroup(std::move(group));
    }
    else if constexpr (stage == RayTracingShaderStage::Intersection)
    {
        desc.addHitGroup(RayTracingHitGroupDesc::procedural(
            static_cast<int32_t>(shaderIndex)
        ));
    }
}

template<typename RayGen, typename... Shaders>
RayTracingPipelineDesc makeRayTracingPipelineDesc(
    uint32_t maxRecursionDepth,
    uint32_t maxPayloadSize,
    uint32_t maxAttributeSize)
{
    static_assert(
        RayTracingShaderModuleLike<RayGen>,
        "RayGen must be RayGenerationShader<Shader> or expose rayTracingStage and spirv.");

    static_assert(
        RayGen::rayTracingStage == RayTracingShaderStage::RayGeneration,
        "First ray tracing shader must be a ray generation shader.");

    RayTracingPipelineDesc desc;
    desc.maxRecursionDepth = maxRecursionDepth;
    desc.maxPayloadSize = maxPayloadSize;
    desc.maxAttributeSize = maxAttributeSize;

    appendRayTracingShader<RayGen>(desc);

    uint32_t shaderIndex = 1;
    ((appendRayTracingShader<Shaders>(desc),
      appendAutoHitGroup<Shaders>(desc, shaderIndex++)), ...);

    return desc;
}
}

template<typename RayGen = void, typename... Shaders>
struct RayTracingPipeline;

template<>
struct RayTracingPipeline<void> : RayTracingPipelineBase
{
    using RayTracingPipelineBase::RayTracingPipelineBase;
};

template<typename RayGen, typename... Shaders>
struct RayTracingPipeline
    : RayTracingPipelineBase
    , RayGen::template ResourceBindings<RayTracingPipelineBase>
    , Shaders::template ResourceBindings<RayTracingPipelineBase>...
{
    using RayGenBindings = typename RayGen::template ResourceBindings<RayTracingPipelineBase>;

    RayTracingPipeline(
        uint32_t maxRecursionDepth = 1,
        uint32_t maxPayloadSize = 0,
        uint32_t maxAttributeSize = 8,
        const std::source_location& sourceLocation = std::source_location::current())
        : RayTracingPipelineBase(
            detail::makeRayTracingPipelineDesc<RayGen, Shaders...>(
                maxRecursionDepth,
                maxPayloadSize,
                maxAttributeSize),
            sourceLocation)
        , RayGenBindings(static_cast<RayTracingPipelineBase*>(this))
        , Shaders::template ResourceBindings<RayTracingPipelineBase>(
            static_cast<RayTracingPipelineBase*>(this))...
    {
    }

    RayTracingPipeline(const RayTracingPipeline& other)
        : RayTracingPipelineBase(other)
        , RayGenBindings(static_cast<RayTracingPipelineBase*>(this))
        , Shaders::template ResourceBindings<RayTracingPipelineBase>(
            static_cast<RayTracingPipelineBase*>(this))...
    {
    }

    RayTracingPipeline(RayTracingPipeline&& other) noexcept
        : RayTracingPipelineBase(std::move(other))
        , RayGenBindings(static_cast<RayTracingPipelineBase*>(this))
        , Shaders::template ResourceBindings<RayTracingPipelineBase>(
            static_cast<RayTracingPipelineBase*>(this))...
    {
    }

    RayTracingPipeline& operator=(const RayTracingPipeline& other)
    {
        RayTracingPipelineBase::operator=(other);
        return *this;
    }

    RayTracingPipeline& operator=(RayTracingPipeline&& other) noexcept
    {
        RayTracingPipelineBase::operator=(std::move(other));
        return *this;
    }
};

RayTracingPipeline(
    RayTracingPipelineDesc,
    const std::source_location& = std::source_location::current())
    -> RayTracingPipeline<>;

RayTracingPipeline(
    const std::vector<uint32_t>&,
    const std::vector<uint32_t>&,
    const std::vector<uint32_t>&,
    uint32_t = 1,
    const std::source_location& = std::source_location::current())
    -> RayTracingPipeline<>;

//////////////////////////////////////////////////////////////////////////
// HardwareExecutor
//////////////////////////////////////////////////////////////////////////

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

//////////////////////////////////////////////////////////////////////////
// HardwareDisplayer
//////////////////////////////////////////////////////////////////////////

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
}






