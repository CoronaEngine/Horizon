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
    struct BufferRange;
    struct DrawIndexedParams;
    struct ImageSubresourceRange;
    struct RasterizerPipelineDesc;

    bool validate_buffer_source_data(std::span<const std::byte> data, uint32_t element_size);
    bool validate_buffer_desc(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data = {});
    bool validate_buffer_upload(const HardwareBuffer& dst, std::span<const std::byte> data, uint64_t dst_offset = 0);
    bool validate_buffer_copy(const HardwareBuffer& src, const HardwareBuffer& dst, BufferRange src_range, uint64_t dst_offset = 0);
    bool validate_buffer_host_write(const HardwareBuffer& buffer, std::span<const std::byte> data, uint64_t offset = 0);
    bool validate_buffer_host_read(const HardwareBuffer& buffer, std::span<std::byte> output, uint64_t offset = 0);
    bool validate_image_desc(const HardwareImageDesc& desc, std::span<const std::byte> upload_data = {});
    bool validate_image_upload(const HardwareImageDesc& desc,
                               ImageSubresourceRange range,
                               uint32_t layer_index,
                               uint32_t mip_index,
                               std::span<const std::byte> data);
    bool validate_image_copy(const HardwareImageDesc& src_desc,
                             ImageSubresourceRange src_range,
                             uint32_t src_layer,
                             uint32_t src_mip,
                             const HardwareImageDesc& dst_desc,
                             ImageSubresourceRange dst_range,
                             uint32_t dst_layer,
                             uint32_t dst_mip);
    bool validate_buffer_to_image_copy(const HardwareBuffer& src,
                                       uint64_t buffer_offset,
                                       const HardwareImageDesc& dst_desc,
                                       ImageSubresourceRange dst_range,
                                       uint32_t dst_layer,
                                       uint32_t dst_mip);
    bool validate_image_to_buffer_copy(const HardwareImageDesc& src_desc,
                                       ImageSubresourceRange src_range,
                                       uint32_t src_layer,
                                       uint32_t src_mip,
                                       const HardwareBuffer& dst,
                                       uint64_t buffer_offset);
    bool validate_image_host_write(const HardwareImageDesc& desc,
                                   ImageSubresourceRange range,
                                   uint32_t layer_index,
                                   uint32_t mip_index,
                                   std::span<const std::byte> data,
                                   uint64_t row_pitch = 0,
                                   uint64_t slice_pitch = 0);
    bool validate_image_host_read(const HardwareImageDesc& desc,
                                  ImageSubresourceRange range,
                                  uint32_t layer_index,
                                  uint32_t mip_index,
                                  std::span<std::byte> output,
                                  uint64_t row_pitch = 0,
                                  uint64_t slice_pitch = 0);
    bool validate_rasterizer_pipeline_desc(const RasterizerPipelineDesc& desc);
    bool validate_rasterizer_pipeline_record(const HardwareBuffer& index_buffer, const HardwareBuffer& vertex_buffer, const DrawIndexedParams& params);
}
