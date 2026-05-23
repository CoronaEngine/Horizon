#include "horizon_refac.h"
#include "hardware_wrapper_vulkan/resource_pool.h"

namespace Corona::Horizon
{
    HardwareImage::HardwareImage() : image_id(0) {};

    HardwareImage::HardwareImage(const HardwareImageDesc& desc, std::span<const std::byte> upload_data)
    {

    }

    HardwareImage::HardwareImage(const HardwareImage& other)
    {

    }

    HardwareImage::HardwareImage(HardwareImage&& other) noexcept
    {

    }

    HardwareImage::~HardwareImage()
    {

    }

    HardwareImage& HardwareImage::operator=(const HardwareImage& other)
    {
        return *this;
    }
        
    HardwareImage& HardwareImage::operator=(HardwareImage&& other) noexcept
    {
        return *this;
    }

    HardwareImage HardwareImage::whole() const
    {
        return {};
    }

    HardwareImage HardwareImage::layer(uint32_t layer_index) const
    {
        return {};
    }

    HardwareImage HardwareImage::mip(uint32_t mip_index) const
    {
        return {};
    }

    HardwareImage HardwareImage::subresource(uint32_t layer_index, uint32_t mip_index) const
    {
        return {};
    }

    uint32_t HardwareImage::subresource_index(uint32_t layer_index, uint32_t mip_index) const
    {
        return 0;
    }

    uint32_t HardwareImage::subresource_count() const noexcept
    {
        return 0;
    }

    ImageExtent HardwareImage::mip_extent(uint32_t mip_index) const
    {
        return {};
    }

    ImageExtent HardwareImage::extent() const
    {
        return {};
    }

    bool HardwareImage::write_subresource_bytes(uint32_t layer_index, uint32_t mip_index, std::span<const std::byte> data, uint64_t row_pitch, uint64_t slice_pitch) const
    {
        return false;
    }

    bool HardwareImage::read_subresource_bytes(uint32_t layer_index, uint32_t mip_index, std::span<std::byte> output, uint64_t row_pitch, uint64_t slice_pitch) const
    {
        return false;
    }

    bool HardwareImage::write_bytes(std::span<const std::byte> data, uint64_t row_pitch, uint64_t slice_pitch) const
    {
        return false;
    }

    bool HardwareImage::read_bytes(std::span<std::byte> output, uint64_t row_pitch, uint64_t slice_pitch) const
    {
        return false;
    }

    void HardwareImage::set_clear_color(float r, float g, float b, float a)
    {
    }

    ImageCopyCommand HardwareImage::copy_to(const HardwareImage& dst, uint32_t src_layer, uint32_t dst_layer, uint32_t src_mip, uint32_t dst_mip) const
    {
        return {};
    }

    ImageToBufferCommand HardwareImage::copy_to(const HardwareBuffer& dst, uint32_t image_layer, uint32_t image_mip, uint64_t buffer_offset) const
    {
        return {};
    }

    BufferToImageCommand HardwareImage::copy_from(const HardwareBuffer& src, uint64_t buffer_offset, uint32_t image_layer, uint32_t image_mip) const
    {
        return {};
    }

    uint32_t HardwareImage::store_descriptor() const
    {
        return 0;
    }

    HardwareImage HardwareImage::import_external(const ExternalMemoryHandle& handle, const HardwareImageDesc& desc, uint64_t allocation_size)
    {
        return {};
    }

    ExternalMemoryHandle HardwareImage::export_external() const
    {
        return {};
    }

    HardwareImage HardwareImageLayerSelector::operator[](uint32_t mip_index) const
    {
        return {};
    }
}
