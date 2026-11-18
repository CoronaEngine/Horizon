#pragma once

#include "vk_shader.h"
#include "vk_trace_dispatch_limits.h"
#include <array>

namespace ocarina {

class VkRayTracingShader final : public VkShaderBase {
private:
    VkBufferAllocation sbt_{};
    // One record per region, ordered ray generation, miss, triangle hit.
    std::array<VkStridedDeviceAddressRegionKHR, 3> regions_{};
    VkTraceDispatchLimits limits_{};

    void create_sbt() noexcept;
    void record_dispatch(VkCommandBuffer cb, uint3 dim, uint8_t *push_constants) const noexcept override;

public:
    VkRayTracingShader(VulkanComputeDevice *device, const VkShaderProgram &program, const Function &function,
                       const string &description);
    ~VkRayTracingShader() noexcept override;
};

}// namespace ocarina
