#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <ktm/ktm.h>
#include <vk_mem_alloc.h>
#include <volk.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include "device_manager.h"
#include "corona/kernel/utils/storage.h"

namespace Corona::Horizon
{
    class ResourceManager
    {
    public:
        ResourceManager();
        ~ResourceManager();

        void initialize(DeviceManager& device_manager);
        void shutdown() noexcept;
    
    };
}
