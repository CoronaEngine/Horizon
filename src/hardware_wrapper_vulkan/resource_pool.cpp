#include "resource_pool.h"

#include "hardware_wrapper_vulkan/hardware/resource_manager.h"

namespace Corona::Horizon
{
    void destroy_buffer(BufferWrap& buffer) noexcept
    {
        if (buffer.resource_manager != nullptr)
        {
            buffer.resource_manager->destroy_buffer(buffer);
            return;
        }

        buffer.clear_handles();
    }

    void destroy_image(ImageWrap& image) noexcept
    {
        if (image.resource_manager != nullptr)
        {
            image.resource_manager->destroy_image(image);
            return;
        }

        image.clear_handles();
    }

    ResourcePool& resource_pool()
    {
        static ResourcePool* pool = new ResourcePool();
        return *pool;
    }
}
