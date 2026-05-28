#include "resource_pool.h"

namespace Corona::Horizon
{
    void destroy_buffer(BufferWrap& buffer) noexcept
    {
        // ResourceManager does not expose BufferWrap destruction yet.
        // Keep the pool release path stable and leave native cleanup wiring to that API.
        buffer.clear_handles();
    }

    ResourcePool& resource_pool()
    {
        static ResourcePool* pool = new ResourcePool();
        return *pool;
    }
}
