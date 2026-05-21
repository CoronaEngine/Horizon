#pragma once

#include "format.h"

namespace Corona::Horizon
{
    // ================================================================
    // Forward Declarations
    // ================================================================

    struct ExternalHandle;

    struct HardwareBuffer;
    struct HardwareImage;
    struct HardwareImageLayerSelector;
    struct HardwarePushConstant;

    struct BottomLevelAccelerationStructure;
    struct TopLevelAccelerationStructure;

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

    struct HardwareValidationConfig
    {
        bool enabled = true;
        bool throw_on_error = false;
    };

    void set_hardware_validation_config(const HardwareValidationConfig &config);



    // ================================================================
    // Buffers
    // ================================================================

    // TODO: 后面增加 HardwareBuffer 测试和验证功能，目前先保证接口正确，后续再完善实现细节和错误处理

    template <typename T>
    concept HardwareBufferElement = std::is_trivially_copyable_v<std::remove_cvref_t<T>> && !std::is_pointer_v<std::remove_cvref_t<T>>;

    // index 限制 uint16_t/uint32_t。
    template <typename T>
    concept HardwareIndexElement = std::same_as<std::remove_cvref_t<T>, uint16_t> || std::same_as<std::remove_cvref_t<T>, uint32_t>;

    struct HardwareBufferDesc
    {
        uint64_t byte_size = 0;
        uint32_t element_stride = 0;
        BufferUsageFlags usage = BufferUsageFlags::None;
        CpuAccessMode cpu_access = CpuAccessMode::None;
        bool dedicated = false;
        bool exportable = false;
        std::string debug_name;

        HardwareBufferDesc& set_byte_size(uint64_t value)       noexcept { byte_size = value; return *this; }
        HardwareBufferDesc& set_element_stride(uint32_t value)  noexcept { element_stride = value; return *this; }
        HardwareBufferDesc& set_usage(BufferUsageFlags value)   noexcept { usage = value; return *this; }
        HardwareBufferDesc& set_cpu_access(CpuAccessMode value) noexcept { cpu_access = value; return *this; }
        HardwareBufferDesc& set_dedicated(bool value = true)    noexcept { dedicated = value; return *this; }
        HardwareBufferDesc& set_exportable(bool value = true)   noexcept { exportable = value; return *this; }
        HardwareBufferDesc& set_debug_name(std::string value)            { debug_name = std::move(value); return *this; }

        HardwareBufferDesc& apply(const HardwareBufferOptions& options) noexcept
        {
            cpu_access = options.cpu_access;
            dedicated = options.dedicated;
            exportable = options.exportable;
            return *this;
        }

        template <HardwareBufferElement T>
        static HardwareBufferDesc typed(uint64_t count, BufferUsageFlags usage, std::string name = {}, HardwareBufferOptions options = {})
        {
            // 如果 count 很大，乘法先溢出了，validation 后面也看不出来。
            if (count > std::numeric_limits<uint64_t>::max() / sizeof(T))
                throw std::overflow_error("HardwareBufferDesc byte_size overflow.");

            HardwareBufferDesc desc;
            desc.byte_size = uint64_t(sizeof(T)) * count;
            desc.element_stride = uint32_t(sizeof(T));
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        template <HardwareBufferElement T>
        static HardwareBufferDesc vertex(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferDst | BufferUsageFlags::Vertex, std::move(name), options);
        }

        template <HardwareIndexElement T>
        static HardwareBufferDesc index(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferDst | BufferUsageFlags::Index, std::move(name), options);
        }

        template <HardwareBufferElement T>
        static HardwareBufferDesc uniform(std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(1, BufferUsageFlags::TransferDst | BufferUsageFlags::Uniform, std::move(name), options);
        }

        template <HardwareBufferElement T>
        static HardwareBufferDesc storage(uint64_t count, std::string name = {}, HardwareBufferOptions options = {})
        {
            return typed<T>(count, BufferUsageFlags::TransferSrc | BufferUsageFlags::TransferDst | BufferUsageFlags::Storage, std::move(name), options);
        }
    };

    

    struct HardwareBuffer
    {
    public:
        HardwareBuffer() noexcept = default;
        explicit HardwareBuffer(const HardwareBufferDesc& desc);
        HardwareBuffer(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data);

        HardwareBuffer(const HardwareBuffer& other);
        HardwareBuffer(HardwareBuffer&& other) noexcept;
        ~HardwareBuffer();

        HardwareBuffer& operator=(const HardwareBuffer& other);
        HardwareBuffer& operator=(HardwareBuffer&& other) noexcept;

        void reset() noexcept;
        void swap(HardwareBuffer& other) noexcept;

        [[nodiscard]] bool valid() const noexcept;
        explicit operator bool() const noexcept { return valid(); }

        [[nodiscard]] std::uintptr_t get_buffer_id() const noexcept { return buffer_id; }
        [[nodiscard]] uint64_t get_byte_size() const;
        [[nodiscard]] uint64_t get_element_stride() const;
        [[nodiscard]] uint64_t get_element_count() const;
        [[nodiscard]] void* get_mapped_data() const;

        // TODO: 需要写测试案例
        bool write_bytes(std::span<const std::byte> data, uint64_t offset = 0) const;
        bool read_bytes(std::span<std::byte> output, uint64_t offset = 0) const;

        template <HardwareBufferElement T>
        bool write(std::span<const T> data, uint64_t offset = 0) const
        {
            return write_bytes(std::as_bytes(data), offset);
        }

        template <HardwareBufferElement T>
        bool write_value(const T& value, uint64_t offset = 0) const
        {
            return write(std::span<const T>(&value, 1), offset);
        }

        // read 禁止 const 输出。
        template <HardwareBufferElement T>
        requires (!std::is_const_v<T>)
        bool read(std::span<T> output, uint64_t offset = 0) const
        {
            return read_bytes(std::as_writable_bytes(output), offset);
        }

        static HardwareBuffer from_bytes(std::span<const std::byte> data, uint32_t element_stride, BufferUsageFlags usage, std::string name = {}, HardwareBufferOptions options = {})
        {
            HardwareBufferDesc desc;
            desc.byte_size = uint64_t(data.size_bytes());
            desc.element_stride = element_stride;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            desc.apply(options);
            return HardwareBuffer(desc, data);
        }

        template <HardwareBufferElement T>
        static HardwareBuffer vertex(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::vertex<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareBufferElement<std::ranges::range_value_t<Range>>
        static HardwareBuffer vertex(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return vertex<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        template <HardwareIndexElement T>
        static HardwareBuffer index(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::index<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareIndexElement<std::ranges::range_value_t<Range>>
        static HardwareBuffer index(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return index<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        template <HardwareBufferElement T>
        static HardwareBuffer uniform(const T& value, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::uniform<T>(std::move(name), options), std::as_bytes(std::span<const T>(&value, 1)));
        }

        template <HardwareBufferElement T>
        static HardwareBuffer storage(std::span<const T> data, std::string name = {}, HardwareBufferOptions options = {})
        {
            return HardwareBuffer(HardwareBufferDesc::storage<T>(data.size(), std::move(name), options), std::as_bytes(data));
        }

        template <std::ranges::contiguous_range Range>
        requires std::ranges::sized_range<Range> && HardwareBufferElement<std::ranges::range_value_t<Range>>
        static HardwareBuffer storage(const Range& data, std::string name = {}, HardwareBufferOptions options = {})
        {
            using T = std::ranges::range_value_t<Range>;
            return storage<T>(std::span<const T>(std::ranges::data(data), std::ranges::size(data)), std::move(name), options);
        }

        [[nodiscard]] BufferCopyCommand copy_to(const HardwareBuffer& dst, BufferRange src = BufferRange::entire(), uint64_t dst_offset = 0) const;
        [[nodiscard]] BufferToImageCommand copy_to(const HardwareImage& dst, uint64_t buffer_offset = 0, uint32_t image_layer = 0, uint32_t image_mip = 0) const;
        [[nodiscard]] uint32_t store_descriptor() const;

        static HardwareBuffer import_external_memory(const ExternalHandle& handle, const HardwareBufferDesc& desc, uint64_t allocation_size = 0);
        ExternalHandle export_memory() const;

    private:
        explicit HardwareBuffer(std::uintptr_t id) noexcept : buffer_id(id) {}
        std::uintptr_t buffer_id = 0;

        friend class HardwareImage;
    };



    //////////////////////////////////////////////////////////////////////////
    // HardwareImage
    //////////////////////////////////////////////////////////////////////////

    template <typename T>
    concept HardwareImageElement = std::is_trivially_copyable_v<std::remove_cvref_t<T>> && !std::is_pointer_v<std::remove_cvref_t<T>>;

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

        HardwareImageDesc& set_dimension(ImageDimension value) noexcept { dimension = value; return *this; }
        HardwareImageDesc& set_extent(ImageExtent value) noexcept { extent = value; return *this; }
        HardwareImageDesc& set_format(Format value) noexcept { format = value; return *this; }
        HardwareImageDesc& set_usage(ImageUsageFlags value) noexcept { usage = value; return *this; }
        HardwareImageDesc& add_usage(ImageUsageFlags value) noexcept { usage |= value; return *this; }
    HardwareImageDesc& set_cpu_access(CpuAccessMode value) noexcept { cpu_access = value; return *this; }
    HardwareImageDesc& set_array_layers(uint32_t value) noexcept { array_layers = value; return *this; }
    HardwareImageDesc& set_mip_levels(uint32_t value) noexcept { mip_levels = value; return *this; }
    HardwareImageDesc& set_sample_count(uint32_t value) noexcept { sample_count = value; return *this; }
    HardwareImageDesc& set_dedicated(bool value = true) noexcept { dedicated = value; return *this; }
    HardwareImageDesc& set_exportable(bool value = true) noexcept { exportable = value; return *this; }
    HardwareImageDesc& set_debug_name(std::string value) { debug_name = std::move(value); return *this; }

        
        HardwareImageDesc& apply(const HardwareImageOptions& options) noexcept
        {
            cpu_access = options.cpu_access;
            dedicated = options.dedicated;
            exportable = options.exportable;
            return *this;
        }

        static HardwareImageDesc texture_2d(
            uint32_t width,
            uint32_t height,
            Format format,
            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
            std::string name = {},
            HardwareImageOptions options = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Image2D;
            desc.extent = { width, height, 1 };
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        static HardwareImageDesc texture_2d_array(
            uint32_t width,
            uint32_t height,
            uint32_t layers,
            Format format,
            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
            std::string name = {},
            HardwareImageOptions options = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Image2DArray;
            desc.extent = { width, height, 1 };
            desc.array_layers = layers;
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        static HardwareImageDesc texture_3d(
        uint32_t width,
        uint32_t height,
        uint32_t depth,
        Format format,
        ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
        std::string name = {},
        HardwareImageOptions options = {})
    {
        HardwareImageDesc desc;
        desc.dimension = ImageDimension::Image3D;
        desc.extent = { width, height, depth };
        desc.format = format;
        desc.usage = usage;
        desc.debug_name = std::move(name);
        return desc.apply(options);
    }

        static HardwareImageDesc cube(
            uint32_t size,
            Format format,
            ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
            std::string name = {},
            HardwareImageOptions options = {})
        {
            HardwareImageDesc desc;
            desc.dimension = ImageDimension::Cube;
            desc.extent = { size, size, 1 };
            desc.array_layers = 6;
            desc.format = format;
            desc.usage = usage;
            desc.debug_name = std::move(name);
            return desc.apply(options);
        }

        static HardwareImageDesc cube_array(
        uint32_t size,
        uint32_t cube_count,
        Format format,
        ImageUsageFlags usage = ImageUsageFlags::Sampled | ImageUsageFlags::TransferDst,
        std::string name = {},
        HardwareImageOptions options = {})
    {
        HardwareImageDesc desc;
        desc.dimension = ImageDimension::CubeArray;
        desc.extent = { size, size, 1 };
        desc.array_layers = cube_count * 6;
        desc.format = format;
        desc.usage = usage;
        desc.debug_name = std::move(name);
        return desc.apply(options);
    }

    static HardwareImageDesc color_attachment(
        uint32_t width,
        uint32_t height,
        Format format,
        std::string name = {},
        HardwareImageOptions options = {})
    {
        return texture_2d(
            width,
            height,
            format,
            ImageUsageFlags::ColorAttachment |
                ImageUsageFlags::Sampled |
                ImageUsageFlags::TransferSrc |
                ImageUsageFlags::TransferDst,
            std::move(name),
            options);
    }

    static HardwareImageDesc depth_attachment(
        uint32_t width,
        uint32_t height,
        Format format,
        std::string name = {},
        HardwareImageOptions options = {})
    {
        return texture_2d(
            width,
            height,
            format,
            ImageUsageFlags::DepthStencilAttachment |
                ImageUsageFlags::Sampled |
                ImageUsageFlags::TransferSrc |
                ImageUsageFlags::TransferDst,
            std::move(name),
            options);
    }
    };

    struct HardwareImage
    {
    public:
        HardwareImage() noexcept = default;
        explicit HardwareImage(const HardwareImageDesc& desc);
        HardwareImage(const HardwareImageDesc& desc, std::span<const std::byte> upload_data);

        HardwareImage(const HardwareImage& other);
        HardwareImage(HardwareImage&& other) noexcept;
        ~HardwareImage();

        HardwareImage& operator=(const HardwareImage& other);
        HardwareImage& operator=(HardwareImage&& other) noexcept;

        void reset() noexcept;
        void swap(HardwareImage& other) noexcept;

        [[nodiscard]] bool valid() const noexcept;
        explicit operator bool() const noexcept { return valid(); }

        [[nodiscard]] std::uintptr_t get_image_id() const noexcept { return image_id; }
        [[nodiscard]] const HardwareImageDesc& desc() const noexcept { return desc_; }

        [[nodiscard]] uint32_t width() const noexcept { return desc_.extent.width; }
        [[nodiscard]] uint32_t height() const noexcept { return desc_.extent.height; }
        [[nodiscard]] uint32_t depth() const noexcept { return desc_.extent.depth; }
        [[nodiscard]] uint32_t array_layers() const noexcept { return desc_.array_layers; }
        [[nodiscard]] uint32_t mip_levels() const noexcept { return desc_.mip_levels; }
        [[nodiscard]] uint32_t sample_count() const noexcept { return desc_.sample_count; }
        [[nodiscard]] Format format() const noexcept { return desc_.format; }
        [[nodiscard]] ImageDimension dimension() const noexcept { return desc_.dimension; }
        [[nodiscard]] ImageUsageFlags usage() const noexcept { return desc_.usage; }
        [[nodiscard]] CpuAccessMode cpu_access() const noexcept { return desc_.cpu_access; }

        // Required access style: image[layer][mip].
        [[nodiscard]] HardwareImageLayerSelector operator[](uint32_t layer) const;

        [[nodiscard]] HardwareImage whole() const
        {
            HardwareImage result = *this;
            result.range_ = ImageSubresourceRange::whole();
            return result;
        }

        [[nodiscard]] HardwareImage layer(uint32_t layer_index) const
        {
            validate_layer(layer_index);

            HardwareImage result = *this;
            result.range_ = {
                .base_layer = layer_index,
                .layer_count = 1,
                .base_mip = 0,
                .mip_count = ImageSubresourceRange::remaining,
            };
            return result;
        }

        [[nodiscard]] HardwareImage mip(uint32_t mip_index) const
        {
            validate_mip(mip_index);

            HardwareImage result = *this;
            result.range_ = {
                .base_layer = 0,
                .layer_count = ImageSubresourceRange::remaining,
                .base_mip = mip_index,
                .mip_count = 1,
            };
            return result;
        }

        [[nodiscard]] HardwareImage subresource(uint32_t layer_index, uint32_t mip_index) const
        {
            validate_layer(layer_index);
            validate_mip(mip_index);

            HardwareImage result = *this;
            result.range_ = ImageSubresourceRange::single(layer_index, mip_index);
            return result;
        }

        [[nodiscard]] ImageSubresourceRange range() const
        {
            return resolve_range(range_, desc_.array_layers, desc_.mip_levels);
        }

        [[nodiscard]] bool is_whole() const
        {
            const ImageSubresourceRange resolved = range();
            return resolved.base_layer == 0 &&
                   resolved.layer_count == desc_.array_layers &&
                   resolved.base_mip == 0 &&
                   resolved.mip_count == desc_.mip_levels;
        }

        [[nodiscard]] bool is_single_subresource() const
        {
            return range().is_single();
        }

        [[nodiscard]] uint32_t base_layer() const { return range().base_layer; }
        [[nodiscard]] uint32_t layer_count() const { return range().layer_count; }
        [[nodiscard]] uint32_t base_mip() const { return range().base_mip; }
        [[nodiscard]] uint32_t mip_count() const { return range().mip_count; }

        [[nodiscard]] ImageSubresource selected_subresource() const
        {
            const ImageSubresourceRange resolved = range();

            if (!resolved.is_single())
                throw std::logic_error("HardwareImage does not represent a single subresource.");

            return {
                .layer = resolved.base_layer,
                .mip = resolved.base_mip,
            };
        }

        [[nodiscard]] uint32_t subresource_index(uint32_t layer_index, uint32_t mip_index) const
        {
            validate_layer(layer_index);
            validate_mip(mip_index);
            return layer_index * desc_.mip_levels + mip_index;
        }

        [[nodiscard]] uint32_t subresource_index() const
        {
            const ImageSubresource sub = selected_subresource();
            return subresource_index(sub.layer, sub.mip);
        }

        [[nodiscard]] uint32_t subresource_count() const noexcept
        {
            return desc_.array_layers * desc_.mip_levels;
        }

        [[nodiscard]] ImageExtent mip_extent(uint32_t mip_index) const
        {
            validate_mip(mip_index);

            auto reduce = [mip_index](uint32_t value) noexcept {
                const uint32_t reduced = value >> mip_index;
                return reduced == 0 ? 1u : reduced;
            };

            return {
                .width = reduce(desc_.extent.width),
                .height = reduce(desc_.extent.height),
                .depth = reduce(desc_.extent.depth),
            };
        }

        [[nodiscard]] ImageExtent extent() const
        {
            if (is_single_subresource())
                return mip_extent(selected_subresource().mip);

            return desc_.extent;
        }

        [[nodiscard]] ImageSubresourceLayout get_subresource_layout(uint32_t layer_index, uint32_t mip_index) const;
        [[nodiscard]] void* get_mapped_subresource(uint32_t layer_index, uint32_t mip_index) const;

        [[nodiscard]] ImageSubresourceLayout get_subresource_layout() const
        {
            const ImageSubresource sub = selected_subresource();
            return get_subresource_layout(sub.layer, sub.mip);
        }

        [[nodiscard]] void* get_mapped_subresource() const
        {
            const ImageSubresource sub = selected_subresource();
            return get_mapped_subresource(sub.layer, sub.mip);
        }

        bool write_subresource_bytes(
            uint32_t layer_index,
            uint32_t mip_index,
            std::span<const std::byte> data,
            uint64_t row_pitch = 0,
            uint64_t slice_pitch = 0) const;

        bool read_subresource_bytes(
            uint32_t layer_index,
            uint32_t mip_index,
            std::span<std::byte> output,
            uint64_t row_pitch = 0,
            uint64_t slice_pitch = 0) const;

        bool write_bytes(
            std::span<const std::byte> data,
            uint64_t row_pitch = 0,
            uint64_t slice_pitch = 0) const
        {
            const ImageSubresource sub = selected_subresource();
            return write_subresource_bytes(sub.layer, sub.mip, data, row_pitch, slice_pitch);
        }

        bool read_bytes(
            std::span<std::byte> output,
            uint64_t row_pitch = 0,
            uint64_t slice_pitch = 0) const
        {
            const ImageSubresource sub = selected_subresource();
            return read_subresource_bytes(sub.layer, sub.mip, output, row_pitch, slice_pitch);
        }

        template <HardwareImageElement T>
        bool write_subresource(
            uint32_t layer_index,
            uint32_t mip_index,
            std::span<const T> data,
            uint64_t row_pitch = 0,
            uint64_t slice_pitch = 0) const
        {
            return write_subresource_bytes(
                layer_index,
                mip_index,
                std::as_bytes(data),
                row_pitch,
                slice_pitch);
        }

        template <HardwareImageElement T>
        requires (!std::is_const_v<T>)
        bool read_subresource(
            uint32_t layer_index,
            uint32_t mip_index,
            std::span<T> output,
            uint64_t row_pitch = 0,
            uint64_t slice_pitch = 0) const
        {
            return read_subresource_bytes(
                layer_index,
                mip_index,
                std::as_writable_bytes(output),
                row_pitch,
                slice_pitch);
        }

        template <HardwareImageElement T>
        bool write(
            std::span<const T> data,
            uint64_t row_pitch = 0,
            uint64_t slice_pitch = 0) const
        {
            return write_bytes(std::as_bytes(data), row_pitch, slice_pitch);
        }

        template <HardwareImageElement T>
        requires (!std::is_const_v<T>)
        bool read(
            std::span<T> output,
            uint64_t row_pitch = 0,
            uint64_t slice_pitch = 0) const
        {
            return read_bytes(std::as_writable_bytes(output), row_pitch, slice_pitch);
        }

        void set_clear_color(float r, float g, float b, float a);

        [[nodiscard]] ImageCopyCommand copy_to(
            const HardwareImage& dst,
            uint32_t src_layer = 0,
            uint32_t dst_layer = 0,
            uint32_t src_mip = 0,
            uint32_t dst_mip = 0) const;

        [[nodiscard]] ImageToBufferCommand copy_to(
            const HardwareBuffer& dst,
            uint32_t image_layer = 0,
            uint32_t image_mip = 0,
            uint64_t buffer_offset = 0) const;

        [[nodiscard]] BufferToImageCommand copy_from(
            const HardwareBuffer& src,
            uint64_t buffer_offset = 0,
            uint32_t image_layer = 0,
            uint32_t image_mip = 0) const;

        [[nodiscard]] uint32_t store_descriptor() const;

        static HardwareImage import_external_memory(
            const ExternalHandle& handle,
            const HardwareImageDesc& desc,
            uint64_t allocation_size = 0);

        ExternalHandle export_memory() const;

    private:
        explicit HardwareImage(std::uintptr_t id, HardwareImageDesc desc) noexcept
            : image_id(id)
            , desc_(std::move(desc))
        {
        }

        static ImageSubresourceRange resolve_range(
            ImageSubresourceRange image_range,
            uint32_t total_layers,
            uint32_t total_mips)
        {
            if (image_range.base_layer > total_layers || image_range.base_mip > total_mips)
                throw std::out_of_range("HardwareImage subresource range base is out of bounds.");

            if (image_range.layer_count == ImageSubresourceRange::remaining)
                image_range.layer_count = total_layers - image_range.base_layer;

            if (image_range.mip_count == ImageSubresourceRange::remaining)
                image_range.mip_count = total_mips - image_range.base_mip;

            if (image_range.layer_count == 0 || image_range.mip_count == 0)
                throw std::invalid_argument("HardwareImage subresource range must be non-empty.");

            if (image_range.layer_count > total_layers - image_range.base_layer)
                throw std::out_of_range("HardwareImage layer range is out of bounds.");

            if (image_range.mip_count > total_mips - image_range.base_mip)
                throw std::out_of_range("HardwareImage mip range is out of bounds.");

            return image_range;
        }

        void validate_layer(uint32_t layer_index) const
        {
            if (layer_index >= desc_.array_layers)
                throw std::out_of_range("HardwareImage layer is out of bounds.");
        }

        void validate_mip(uint32_t mip_index) const
        {
            if (mip_index >= desc_.mip_levels)
                throw std::out_of_range("HardwareImage mip is out of bounds.");
        }

    private:
        std::uintptr_t image_id = 0;
        HardwareImageDesc desc_ {};
        ImageSubresourceRange range_ = ImageSubresourceRange::whole();

        friend class HardwareImageLayerSelector;
        friend struct HardwareBuffer;
    };

    class HardwareImageLayerSelector
    {
    public:
        HardwareImageLayerSelector(const HardwareImage& image, uint32_t layer_index)
            : image_(image)
            , layer_(layer_index)
        {
            image_.validate_layer(layer_);
        }

        // Required access style: image[layer][mip].
        [[nodiscard]] HardwareImage operator[](uint32_t mip_index) const
        {
            return image_.subresource(layer_, mip_index);
        }

        [[nodiscard]] HardwareImage mip(uint32_t mip_index) const
        {
            return image_.subresource(layer_, mip_index);
        }

        [[nodiscard]] HardwareImage all_mips() const
        {
            return image_.layer(layer_);
        }

    private:
        HardwareImage image_;
        uint32_t layer_ = 0;
    };

    inline HardwareImageLayerSelector HardwareImage::operator[](uint32_t layer_index) const
    {
        return HardwareImageLayerSelector(*this, layer_index);
    }

    //////////////////////////////////////////////////////////////////////////
    // HardwarePushConstant
    //////////////////////////////////////////////////////////////////////////

    inline constexpr uint32_t kPortablePushConstantByteSize = 128;

    template <typename T>
    concept HardwarePushConstantValue =
        std::is_trivially_copyable_v<std::remove_cvref_t<T>> &&
        !std::is_pointer_v<std::remove_cvref_t<T>>;

    struct HardwarePushConstantDesc
    {
        uint64_t byte_size = 0;
        std::string debug_name;

        HardwarePushConstantDesc& set_byte_size(uint64_t value) noexcept
        {
            byte_size = value;
            return *this;
        }

        HardwarePushConstantDesc& set_debug_name(std::string value)
        {
            debug_name = std::move(value);
            return *this;
        }
    };

    struct HardwarePushConstant
    {
      public:
        using Size = uint64_t;
        static constexpr Size whole_size = std::numeric_limits<Size>::max();

        HardwarePushConstant() = default;
        explicit HardwarePushConstant(HardwarePushConstantDesc desc);
        explicit HardwarePushConstant(Size byte_size);

        // Compatibility constructor. If whole is provided, this deep-copies that range.
        HardwarePushConstant(Size byte_size, Size offset, const HardwarePushConstant* whole = nullptr);

        HardwarePushConstant(const HardwarePushConstant& other);
        HardwarePushConstant(HardwarePushConstant&& other) noexcept;
        ~HardwarePushConstant() = default;

        HardwarePushConstant& operator=(const HardwarePushConstant& other);
        HardwarePushConstant& operator=(HardwarePushConstant&& other) noexcept;

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

        [[nodiscard]] bool valid() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] Size byte_size() const noexcept;
        [[nodiscard]] HardwarePushConstantDesc desc() const;

        bool reset(HardwarePushConstantDesc desc);
        bool reset(Size byte_size);
        void clear(std::byte value = std::byte{0});

        bool write_bytes(const void* data, Size byte_size, Size offset = 0);
        bool write_bytes(std::span<const std::byte> data, Size offset = 0);

        bool read_bytes(void* output, Size byte_size, Size offset = 0) const;
        bool read_bytes(std::span<std::byte> output, Size offset = 0) const;

        [[nodiscard]] std::vector<std::byte> snapshot_bytes(
            Size offset = 0,
            Size byte_size = whole_size) const;

        template <typename F>
        decltype(auto) with_bytes(F&& callback) const
        {
            std::shared_lock lock(mutex_);
            return std::forward<F>(callback)(
                std::span<const std::byte>(bytes_.data(), bytes_.size()));
        }

        template <HardwarePushConstantValue T>
        bool write_value(Size offset, const T& value)
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

      private:
        mutable std::shared_mutex mutex_;
        HardwarePushConstantDesc desc_;
        std::vector<std::byte> bytes_;
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
    std::string entryPoint = "main";
    std::string debugName;

    static PipelineShaderDesc from_spirv(
        PipelineShaderStage stage,
        std::vector<uint32_t> code,
        std::string entryPoint = "main")
    {
        PipelineShaderDesc desc;
        desc.stage = stage;
        desc.spirv = std::move(code);
        desc.entryPoint = std::move(entryPoint);
        return desc;
    }

    static PipelineShaderDesc from_source(
        PipelineShaderStage stage,
        std::string code,
        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
        std::string entryPoint = "main")
    {
        PipelineShaderDesc desc;
        desc.stage = stage;
        desc.source = std::move(code);
        desc.language = language;
        desc.entryPoint = std::move(entryPoint);
        return desc;
    }

    [[nodiscard]] bool has_spirv() const noexcept
    {
        return !spirv.empty();
    }

    [[nodiscard]] bool has_source() const noexcept
    {
        return !source.empty();
    }

    PipelineShaderDesc& set_debug_name(std::string value)
    {
        debugName = std::move(value);
        return *this;
    }
};



struct ComputePipelineDesc
{
    PipelineShaderDesc computeShader;
    PipelineReflectionDesc reflection;
    std::string debugName;

    ComputePipelineDesc& set_shader(PipelineShaderDesc shader)
    {
        computeShader = std::move(shader);
        computeShader.stage = PipelineShaderStage::Compute;
        return *this;
    }

    ComputePipelineDesc& set_spirv(std::vector<uint32_t> spirv)
    {
        return set_shader(PipelineShaderDesc::from_spirv(
            PipelineShaderStage::Compute,
            std::move(spirv)));
    }

    ComputePipelineDesc& set_source(
        std::string source,
        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL)
    {
        return set_shader(PipelineShaderDesc::from_source(
            PipelineShaderStage::Compute,
            std::move(source),
            language));
    }

    ComputePipelineDesc& set_reflection(PipelineReflectionDesc value) noexcept
    {
        reflection = value;
        return *this;
    }

    ComputePipelineDesc& set_debug_name(std::string value)
    {
        debugName = std::move(value);
        return *this;
    }
};







struct BlendStateDesc
{
    bool logicOpEnabled = false;
    std::vector<BlendAttachmentDesc> attachments = { BlendAttachmentDesc::alpha_blend() };

    BlendStateDesc& set_logic_op_enabled(bool value = true) noexcept
    {
        logicOpEnabled = value;
        return *this;
    }

    BlendStateDesc& set_attachment(uint32_t index, BlendAttachmentDesc desc)
    {
        if (attachments.size() <= index)
            attachments.resize(size_t(index) + 1, BlendAttachmentDesc::alpha_blend());

        attachments[index] = desc;
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
    std::vector<Format> colorFormats;
    Format depthStencilFormat = Format::UNKNOWN;
    uint32_t multiviewCount = 1;

    RenderTargetLayoutDesc& add_color_format(Format format)
    {
        colorFormats.push_back(format);
        return *this;
    }

    RenderTargetLayoutDesc& set_color_format(uint32_t index, Format format)
    {
        if (colorFormats.size() <= index)
            colorFormats.resize(size_t(index) + 1, Format::UNKNOWN);

        colorFormats[index] = format;
        return *this;
    }

    RenderTargetLayoutDesc& set_depth_stencil_format(Format format) noexcept
    {
        depthStencilFormat = format;
        return *this;
    }

    RenderTargetLayoutDesc& set_multiview_count(uint32_t value) noexcept
    {
        multiviewCount = value;
        return *this;
    }
};

struct RasterizerPipelineDesc
{
    PipelineShaderDesc vertexShader;
    PipelineShaderDesc fragmentShader;

    RasterizerStateDesc rasterizer;
    DepthStencilStateDesc depthStencil;
    BlendStateDesc blend;
    MultisampleStateDesc multisample;
    RenderTargetLayoutDesc renderTargetLayout;
    PipelineReflectionDesc reflection;

    std::string debugName;

    RasterizerPipelineDesc& set_vertex_shader(PipelineShaderDesc shader)
    {
        vertexShader = std::move(shader);
        vertexShader.stage = PipelineShaderStage::Vertex;
        return *this;
    }

    RasterizerPipelineDesc& set_fragment_shader(PipelineShaderDesc shader)
    {
        fragmentShader = std::move(shader);
        fragmentShader.stage = PipelineShaderStage::Fragment;
        return *this;
    }

    RasterizerPipelineDesc& set_vertex_spirv(std::vector<uint32_t> spirv)
    {
        return set_vertex_shader(PipelineShaderDesc::from_spirv(
            PipelineShaderStage::Vertex,
            std::move(spirv)));
    }

    RasterizerPipelineDesc& set_fragment_spirv(std::vector<uint32_t> spirv)
    {
        return set_fragment_shader(PipelineShaderDesc::from_spirv(
            PipelineShaderStage::Fragment,
            std::move(spirv)));
    }

    RasterizerPipelineDesc& set_vertex_source(
        std::string source,
        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL)
    {
        return set_vertex_shader(PipelineShaderDesc::from_source(
            PipelineShaderStage::Vertex,
            std::move(source),
            language));
    }

    RasterizerPipelineDesc& set_fragment_source(
        std::string source,
        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL)
    {
        return set_fragment_shader(PipelineShaderDesc::from_source(
            PipelineShaderStage::Fragment,
            std::move(source),
            language));
    }

    RasterizerPipelineDesc& set_rasterizer(RasterizerStateDesc value) noexcept
    {
        rasterizer = value;
        return *this;
    }

    RasterizerPipelineDesc& set_depth_stencil(DepthStencilStateDesc value) noexcept
    {
        depthStencil = value;
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
        renderTargetLayout = std::move(value);
        return *this;
    }

    RasterizerPipelineDesc& set_multiview_count(uint32_t value) noexcept
    {
        renderTargetLayout.multiviewCount = value;
        return *this;
    }

    RasterizerPipelineDesc& set_reflection(PipelineReflectionDesc value) noexcept
    {
        reflection = value;
        return *this;
    }

    RasterizerPipelineDesc& set_debug_name(std::string value)
    {
        debugName = std::move(value);
        return *this;
    }
};

struct RayTracingShaderDesc
{
    RayTracingShaderStage stage = RayTracingShaderStage::RayGeneration;
    std::vector<uint32_t> spirv;
    std::string source;
    EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL;
    std::string entryPoint = "main";
    std::string debugName;

    static RayTracingShaderDesc from_spirv(
        RayTracingShaderStage stage,
        std::vector<uint32_t> code,
        std::string entryPoint = "main")
    {
        RayTracingShaderDesc desc;
        desc.stage = stage;
        desc.spirv = std::move(code);
        desc.entryPoint = std::move(entryPoint);
        return desc;
    }

    static RayTracingShaderDesc from_source(
        RayTracingShaderStage stage,
        std::string code,
        EmbeddedShader::ShaderLanguage language = EmbeddedShader::ShaderLanguage::GLSL,
        std::string entryPoint = "main")
    {
        RayTracingShaderDesc desc;
        desc.stage = stage;
        desc.source = std::move(code);
        desc.language = language;
        desc.entryPoint = std::move(entryPoint);
        return desc;
    }

    [[nodiscard]] bool has_spirv() const noexcept
    {
        return !spirv.empty();
    }

    [[nodiscard]] bool has_source() const noexcept
    {
        return !source.empty();
    }

    RayTracingShaderDesc& set_debug_name(std::string value)
    {
        debugName = std::move(value);
        return *this;
    }
};

struct RayTracingHitGroupDesc
{
    RayTracingHitGroupKind kind = RayTracingHitGroupKind::Triangles;
    int32_t closestHitShader = -1;
    int32_t anyHitShader = -1;
    int32_t intersectionShader = -1;
    std::string debugName;

    static RayTracingHitGroupDesc triangles(
        int32_t closestHitShader,
        int32_t anyHitShader = -1)
    {
        RayTracingHitGroupDesc desc;
        desc.kind = RayTracingHitGroupKind::Triangles;
        desc.closestHitShader = closestHitShader;
        desc.anyHitShader = anyHitShader;
        return desc;
    }

    static RayTracingHitGroupDesc procedural(
        int32_t intersectionShader,
        int32_t closestHitShader = -1,
        int32_t anyHitShader = -1)
    {
        RayTracingHitGroupDesc desc;
        desc.kind = RayTracingHitGroupKind::Procedural;
        desc.intersectionShader = intersectionShader;
        desc.closestHitShader = closestHitShader;
        desc.anyHitShader = anyHitShader;
        return desc;
    }

    RayTracingHitGroupDesc& set_debug_name(std::string value)
    {
        debugName = std::move(value);
        return *this;
    }
};

struct RayTracingPipelineDesc
{
    std::vector<RayTracingShaderDesc> shaders;
    std::vector<RayTracingHitGroupDesc> hitGroups;

    uint32_t maxRecursionDepth = 1;
    uint32_t maxPayloadSize = 0;
    uint32_t maxAttributeSize = 8;

    PipelineReflectionDesc reflection;
    std::string debugName;

    uint32_t add_shader(RayTracingShaderDesc shader)
    {
        shaders.push_back(std::move(shader));
        return static_cast<uint32_t>(shaders.size() - 1);
    }

    uint32_t add_ray_generation_shader(std::vector<uint32_t> spirv, std::string entryPoint = "main")
    {
        return add_shader(RayTracingShaderDesc::from_spirv(
            RayTracingShaderStage::RayGeneration,
            std::move(spirv),
            std::move(entryPoint)));
    }

    uint32_t add_miss_shader(std::vector<uint32_t> spirv, std::string entryPoint = "main")
    {
        return add_shader(RayTracingShaderDesc::from_spirv(
            RayTracingShaderStage::Miss,
            std::move(spirv),
            std::move(entryPoint)));
    }

    uint32_t add_closest_hit_shader(std::vector<uint32_t> spirv, std::string entryPoint = "main")
    {
        return add_shader(RayTracingShaderDesc::from_spirv(
            RayTracingShaderStage::ClosestHit,
            std::move(spirv),
            std::move(entryPoint)));
    }

    uint32_t add_any_hit_shader(std::vector<uint32_t> spirv, std::string entryPoint = "main")
    {
        return add_shader(RayTracingShaderDesc::from_spirv(
            RayTracingShaderStage::AnyHit,
            std::move(spirv),
            std::move(entryPoint)));
    }

    uint32_t add_intersection_shader(std::vector<uint32_t> spirv, std::string entryPoint = "main")
    {
        return add_shader(RayTracingShaderDesc::from_spirv(
            RayTracingShaderStage::Intersection,
            std::move(spirv),
            std::move(entryPoint)));
    }

    uint32_t add_callable_shader(std::vector<uint32_t> spirv, std::string entryPoint = "main")
    {
        return add_shader(RayTracingShaderDesc::from_spirv(
            RayTracingShaderStage::Callable,
            std::move(spirv),
            std::move(entryPoint)));
    }

    RayTracingPipelineDesc& add_hit_group(RayTracingHitGroupDesc hitGroup)
    {
        hitGroups.push_back(std::move(hitGroup));
        return *this;
    }

    RayTracingPipelineDesc& set_max_recursion_depth(uint32_t value) noexcept
    {
        maxRecursionDepth = value;
        return *this;
    }

    RayTracingPipelineDesc& set_max_payload_size(uint32_t value) noexcept
    {
        maxPayloadSize = value;
        return *this;
    }

    RayTracingPipelineDesc& set_max_attribute_size(uint32_t value) noexcept
    {
        maxAttributeSize = value;
        return *this;
    }

    RayTracingPipelineDesc& set_reflection(PipelineReflectionDesc value) noexcept
    {
        reflection = value;
        return *this;
    }

    RayTracingPipelineDesc& set_debug_name(std::string value)
    {
        debugName = std::move(value);
        return *this;
    }
};



// ================================================================
// Acceleration Structures
// ================================================================

struct BottomLevelAccelerationStructure;
struct TopLevelAccelerationStructure;

struct BottomLevelGeometryDesc
{
    RayTracingGeometryKind kind = RayTracingGeometryKind::Triangles;

    HardwareBuffer vertexBuffer;
    uint64_t vertexOffset = 0;
    uint32_t vertexStride = 0;
    uint32_t vertexCount = 0;

    HardwareBuffer indexBuffer;
    uint64_t indexOffset = 0;
    uint32_t indexCount = 0;
    IndexType indexType = IndexType::Auto;

    HardwareBuffer transformBuffer;
    uint64_t transformOffset = 0;

    HardwareBuffer aabbBuffer;
    uint64_t aabbOffset = 0;
    uint32_t aabbStride = 0;
    uint32_t aabbCount = 0;

    bool opaque = true;

    static BottomLevelGeometryDesc triangles(
        const HardwareBuffer& vertexBuffer,
        uint32_t vertexCount,
        uint32_t vertexStride,
        const HardwareBuffer& indexBuffer = {},
        uint32_t indexCount = 0,
        IndexType indexType = IndexType::Auto)
    {
        BottomLevelGeometryDesc desc;
        desc.kind = RayTracingGeometryKind::Triangles;
        desc.vertexBuffer = vertexBuffer;
        desc.vertexCount = vertexCount;
        desc.vertexStride = vertexStride;
        desc.indexBuffer = indexBuffer;
        desc.indexCount = indexCount;
        desc.indexType = indexType;
        return desc;
    }

    static BottomLevelGeometryDesc aabbs(
        const HardwareBuffer& aabbBuffer,
        uint32_t aabbCount,
        uint32_t aabbStride)
    {
        BottomLevelGeometryDesc desc;
        desc.kind = RayTracingGeometryKind::Aabbs;
        desc.aabbBuffer = aabbBuffer;
        desc.aabbCount = aabbCount;
        desc.aabbStride = aabbStride;
        return desc;
    }
};

struct BottomLevelAccelerationStructureDesc
{
    std::vector<BottomLevelGeometryDesc> geometries;
    bool allowUpdate = false;
    bool preferFastTrace = true;
    bool preferFastBuild = false;
    std::string debugName;

    BottomLevelAccelerationStructureDesc& addGeometry(BottomLevelGeometryDesc geometry)
    {
        geometries.push_back(std::move(geometry));
        return *this;
    }

    BottomLevelAccelerationStructureDesc& setAllowUpdate(bool value = true) noexcept
    {
        allowUpdate = value;
        return *this;
    }

    BottomLevelAccelerationStructureDesc& setDebugName(std::string value)
    {
        debugName = std::move(value);
        return *this;
    }
};

struct RayTracingInstanceDesc
{
    BottomLevelAccelerationStructure* bottomLevel = nullptr;
    std::array<float, 12> transform = {
        1.f, 0.f, 0.f, 0.f,
        0.f, 1.f, 0.f, 0.f,
        0.f, 0.f, 1.f, 0.f
    };

    uint32_t instanceID = 0;
    uint32_t hitGroupIndex = 0;
    uint8_t mask = 0xff;
    uint32_t flags = 0;
};

struct TopLevelAccelerationStructureDesc
{
    std::vector<RayTracingInstanceDesc> instances;
    bool allowUpdate = false;
    bool preferFastTrace = true;
    bool preferFastBuild = false;
    std::string debugName;

    TopLevelAccelerationStructureDesc& addInstance(RayTracingInstanceDesc instance)
    {
        instances.push_back(std::move(instance));
        return *this;
    }

    TopLevelAccelerationStructureDesc& setAllowUpdate(bool value = true) noexcept
    {
        allowUpdate = value;
        return *this;
    }

    TopLevelAccelerationStructureDesc& setDebugName(std::string value)
    {
        debugName = std::move(value);
        return *this;
    }
};

struct BottomLevelAccelerationStructure
{
public:
    BottomLevelAccelerationStructure();
    explicit BottomLevelAccelerationStructure(const BottomLevelAccelerationStructureDesc& desc);

    BottomLevelAccelerationStructure(const BottomLevelAccelerationStructure& other);
    BottomLevelAccelerationStructure(BottomLevelAccelerationStructure&& other) noexcept;
    ~BottomLevelAccelerationStructure();

    BottomLevelAccelerationStructure& operator=(const BottomLevelAccelerationStructure& other);
    BottomLevelAccelerationStructure& operator=(BottomLevelAccelerationStructure&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept
    {
        return accelerationStructureID.load(std::memory_order_acquire) != 0;
    }

    explicit operator bool() const noexcept
    {
        return valid();
    }

    [[nodiscard]] uintptr_t getAccelerationStructureID() const noexcept
    {
        return accelerationStructureID.load(std::memory_order_acquire);
    }

private:
    mutable std::mutex accelerationStructureMutex;
    std::atomic<std::uintptr_t> accelerationStructureID{0};
};

struct TopLevelAccelerationStructure
{
public:
    TopLevelAccelerationStructure();
    explicit TopLevelAccelerationStructure(const TopLevelAccelerationStructureDesc& desc);

    TopLevelAccelerationStructure(const TopLevelAccelerationStructure& other);
    TopLevelAccelerationStructure(TopLevelAccelerationStructure&& other) noexcept;
    ~TopLevelAccelerationStructure();

    TopLevelAccelerationStructure& operator=(const TopLevelAccelerationStructure& other);
    TopLevelAccelerationStructure& operator=(TopLevelAccelerationStructure&& other) noexcept;

    [[nodiscard]] bool valid() const noexcept
    {
        return accelerationStructureID.load(std::memory_order_acquire) != 0;
    }

    explicit operator bool() const noexcept
    {
        return valid();
    }

    [[nodiscard]] uintptr_t getAccelerationStructureID() const noexcept
    {
        return accelerationStructureID.load(std::memory_order_acquire);
    }

private:
    mutable std::mutex accelerationStructureMutex;
    std::atomic<std::uintptr_t> accelerationStructureID{0};
};

struct AccelerationStructureBuildCommand
{
    AccelerationStructureBuildMode mode = AccelerationStructureBuildMode::Build;
    BottomLevelAccelerationStructure* bottomLevel = nullptr;
    TopLevelAccelerationStructure* topLevel = nullptr;

    static AccelerationStructureBuildCommand build(BottomLevelAccelerationStructure& accelerationStructure)
    {
        AccelerationStructureBuildCommand cmd;
        cmd.mode = AccelerationStructureBuildMode::Build;
        cmd.bottomLevel = &accelerationStructure;
        return cmd;
    }

    static AccelerationStructureBuildCommand update(BottomLevelAccelerationStructure& accelerationStructure)
    {
        AccelerationStructureBuildCommand cmd;
        cmd.mode = AccelerationStructureBuildMode::Update;
        cmd.bottomLevel = &accelerationStructure;
        return cmd;
    }

    static AccelerationStructureBuildCommand build(TopLevelAccelerationStructure& accelerationStructure)
    {
        AccelerationStructureBuildCommand cmd;
        cmd.mode = AccelerationStructureBuildMode::Build;
        cmd.topLevel = &accelerationStructure;
        return cmd;
    }

    static AccelerationStructureBuildCommand update(TopLevelAccelerationStructure& accelerationStructure)
    {
        AccelerationStructureBuildCommand cmd;
        cmd.mode = AccelerationStructureBuildMode::Update;
        cmd.topLevel = &accelerationStructure;
        return cmd;
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







