#pragma once
#include "core/header.h"
#include "core/concepts.h"
#include "core/stl.h"
#include <vulkan/vulkan.h>
namespace ocarina {
class InstanceCreation;

class VulkanInstance : public concepts::Noncopyable {
public:
    VulkanInstance(const InstanceCreation &instanceCreation);
    ~VulkanInstance();

    OC_MAKE_MEMBER_GETTER(instance, )
private:
    std::vector<std::string> m_supportedInstanceExtensions;
    VkInstance instance_;
    bool validation_ = false;
};
}// namespace ocarina