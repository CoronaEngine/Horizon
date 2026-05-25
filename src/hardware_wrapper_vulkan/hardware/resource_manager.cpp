#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#include "resource_manager.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "hardware_wrapper_vulkan/hardware_context.h"
//#include "hardware_wrapper_vulkan/ResourcePool.h"
#include "corona/kernel/core/i_logger.h"

namespace Corona::Horizon
{
    ResourceManager::ResourceManager()
    {
        
    }

    ResourceManager::~ResourceManager()
    {
        
    }

    void ResourceManager::initialize(DeviceManager& device_manager)
    {
        
    }

    void ResourceManager::shutdown() noexcept
    {
        
    }
}
