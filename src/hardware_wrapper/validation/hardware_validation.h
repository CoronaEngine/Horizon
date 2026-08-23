#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Corona::Horizon
{
    class HardwareBuffer;
    struct HardwareBufferDesc;
    struct HardwareImageDesc;
    struct ImageSubresourceRange;
    struct RasterizerPipelineDesc;
    struct RasterizerPipelineShaders;

    bool validate_buffer_source_data(std::span<const std::byte> data, uint32_t element_size);
    bool validate_buffer_desc(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data = {});
    bool validate_buffer_host_write(const HardwareBuffer& buffer, std::span<const std::byte> data, uint64_t offset = 0);
    bool validate_image_desc(const HardwareImageDesc& desc, std::span<const std::byte> upload_data = {});
    bool validate_buffer_to_image_copy(const HardwareBuffer& src,
                                       uint64_t buffer_offset,
                                       const HardwareImageDesc& dst_desc,
                                       ImageSubresourceRange dst_range,
                                       uint32_t dst_layer,
                                       uint32_t dst_mip);
    bool validate_image_host_write(const HardwareImageDesc& desc,
                                   ImageSubresourceRange range,
                                   uint32_t layer_index,
                                   uint32_t mip_index,
                                   std::span<const std::byte> data,
                                   uint64_t row_pitch = 0,
                                   uint64_t slice_pitch = 0);
    bool validate_rasterizer_pipeline_desc(const RasterizerPipelineDesc& desc, const RasterizerPipelineShaders& shaders);
}
