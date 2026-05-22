#include "horizon_refac.h"

namespace Corona::Horizon
{
	HardwareBuffer::HardwareBuffer() : buffer_id(0) {}

	HardwareBuffer::HardwareBuffer(const HardwareBufferDesc& desc, std::span<const std::byte> upload_data)
	{


	}

	HardwareBuffer::HardwareBuffer(const HardwareBuffer& other)
	{

	}

	HardwareBuffer::HardwareBuffer(HardwareBuffer&& other) noexcept
	{

	}


	HardwareBuffer& HardwareBuffer::operator=(const HardwareBuffer& other)
	{

	}


	HardwareBuffer& HardwareBuffer::operator=(HardwareBuffer&& other) noexcept
	{

	}

    uint64_t HardwareBuffer::get_element_size() const
    {
   
    }

	uint64_t HardwareBuffer::get_element_count() const
    {
     
    }

	void* HardwareBuffer::get_mapped_data() const
    {
        
    }

	bool HardwareBuffer::write_bytes(std::span<const std::byte> data, uint64_t offset = 0) const
	{

	}

	bool HardwareBuffer::read_bytes(std::span<std::byte> output, uint64_t offset = 0) const
	{

	}

	BufferCopyCommand copy_to(const HardwareBuffer &dst, BufferRange src = BufferRange::entire(), uint64_t dst_offset = 0) const;
    BufferToImageCommand copy_to(const HardwareImage &dst, uint64_t buffer_offset = 0, uint32_t image_layer = 0, uint32_t image_mip = 0) const;
    uint32_t store_descriptor() const;
    static HardwareBuffer import_external(const ExternalMemoryHandle &handle, const HardwareBufferDesc &desc);
    [[nodiscard]] ExternalMemoryHandle export_external() const;


}