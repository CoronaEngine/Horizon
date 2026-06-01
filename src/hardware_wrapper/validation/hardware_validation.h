#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Corona::Horizon
{
    // This module owns policy checks for hardware wrapper operations.
    // Keep pure descriptor math and hard safety invariants in the owning public types.
    // Validation compile/runtime switches gate optional diagnostics only.

    class HardwareBuffer;
    struct HardwareBufferDesc;
    struct HardwareImageDesc;
    struct BufferRange;

    bool validate_buffer_source_data(std::span<const std::byte> data, uint32_t element_size);
    bool validate_buffer_desc(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data = {});
    bool validate_buffer_copy(const HardwareBuffer& src, const HardwareBuffer& dst, BufferRange src_range, uint64_t dst_offset = 0);
    bool validate_buffer_host_write(const HardwareBuffer& buffer, std::span<const std::byte> data, uint64_t offset = 0);
    bool validate_buffer_host_read(const HardwareBuffer& buffer, std::span<std::byte> output, uint64_t offset = 0);
    bool validate_image_desc(const HardwareImageDesc& desc, std::span<const std::byte> upload_data = {});
}
