#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "features_chain.h"

namespace Corona::Horizon
{
    class DeviceManager
    {
    public:
        DeviceManager();
        ~DeviceManager();

        //void initialize(const CreateCallback& createCallback, const VkInstance& vkInstance, const VkPhysicalDevice& physicalDevice);
        //void shutdown() noexcept;

    private:
    };

}
