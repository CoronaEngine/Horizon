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

	HardwareBuffer::~HardwareBuffer()
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

	bool HardwareBuffer::write_bytes(std::span<const std::byte> data, uint64_t offset) const
	{

	}

	bool HardwareBuffer::read_bytes(std::span<std::byte> output, uint64_t offset) const
	{

	}

	BufferCopyCommand HardwareBuffer::copy_to(const HardwareBuffer& dst, BufferRange src, uint64_t dst_offset) const
	{
		
	}

	BufferToImageCommand HardwareBuffer::copy_to(const HardwareImage& dst, uint64_t buffer_offset, uint32_t image_layer, uint32_t image_mip) const
	{

	}

    uint32_t HardwareBuffer::store_descriptor() const
	{

	}

	HardwareBuffer HardwareBuffer::import_external(const ExternalMemoryHandle& handle, const HardwareBufferDesc& desc)
	{

	}

	ExternalMemoryHandle HardwareBuffer::export_external() const
	{

	}
}
