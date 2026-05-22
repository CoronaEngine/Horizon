#include "hardware_wrapper_vulkan/resource_pool.h"

#include "hardware_wrapper_vulkan/hardware/resource_manager.h"
#include "hardware_wrapper_vulkan/hardware_context.h"

namespace Corona::Horizon::Vulkan
{
    namespace
    {
        void destroy_registered_buffer(ResourceManager::BufferHardwareWrap& buffer)
        {
            if (buffer.resource_manager)
            {
                buffer.resource_manager->destroy_buffer(buffer);
            }
            else if (buffer.resourceManager)
            {
                buffer.resourceManager->destroy_buffer(buffer);
            }
        }

        void destroy_registered_image(ResourceManager::ImageHardwareWrap& image)
        {
            if (image.resource_manager)
            {
                image.resource_manager->destroy_image(image);
            }
            else if (image.resourceManager)
            {
                image.resourceManager->destroy_image(image);
            }
        }

    }

    BufferStorage global_buffer_storages(destroy_registered_buffer);
    ImageStorage global_image_storages(destroy_registered_image);
    RasterizerPipelineStorage rasterizer_pipeline_storage;
    ComputePipelineStorage compute_pipeline_storage;
    DisplayerStorage global_displayer_storages;
    ExecutorStorage executor_storage;
    PushConstantStorage global_push_constant_storages;

    bool retain_buffer(std::uintptr_t id) noexcept
    {
        try
        {
            return global_buffer_storages.retain(id);
        }
        catch (...)
        {
            return false;
        }
    }

    bool release_buffer(std::uintptr_t id) noexcept
    {
        try
        {
            return global_buffer_storages.release(id);
        }
        catch (...)
        {
            return false;
        }
    }

    bool is_buffer_alive(std::uintptr_t id) noexcept
    {
        try
        {
            return global_buffer_storages.contains(id);
        }
        catch (...)
        {
            return false;
        }
    }

    bool retain_image(std::uintptr_t id) noexcept
    {
        try
        {
            return global_image_storages.retain(id);
        }
        catch (...)
        {
            return false;
        }
    }

    bool release_image(std::uintptr_t id) noexcept
    {
        try
        {
            return global_image_storages.release(id);
        }
        catch (...)
        {
            return false;
        }
    }

    bool is_image_alive(std::uintptr_t id) noexcept
    {
        try
        {
            return global_image_storages.contains(id);
        }
        catch (...)
        {
            return false;
        }
    }
}
