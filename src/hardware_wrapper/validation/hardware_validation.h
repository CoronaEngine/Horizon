#pragma once

namespace Corona::Horizon
{
    struct Context
    {
        bool enabled = true;
        bool throw_on_error = false;
    };

    void set_context(Context context);
    [[nodiscard]] Context get_context();

    bool error(std::string_view message);
    void warning(std::string_view message);

    bool validate_buffer_desc(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data = {});
    bool validate_buffer_copy(const HardwareBuffer& src, const HardwareBuffer& dst, BufferRange src_range, uint64_t dst_offset = 0);
    bool validate_buffer_host_write(const HardwareBuffer& buffer, std::span<const std::byte> data, uint64_t offset = 0);
    bool validate_buffer_host_read(const HardwareBuffer& buffer, std::span<std::byte> output, uint64_t offset = 0);
}
