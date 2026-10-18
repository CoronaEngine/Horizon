#pragma once

#include "resource.h"
#include "rhi/command.h"
#include "rhi/device.h"

namespace ocarina {

class OC_RHI_API ExternalComputeKernel {
private:
    handle_ty entry_{};
    ExternalComputeStatus status_{};

public:
    ExternalComputeKernel() = default;
    ExternalComputeKernel(handle_ty entry, ExternalComputeStatus status) noexcept
        : entry_(entry), status_(status) {}

    [[nodiscard]] bool valid() const noexcept {
        return entry_ != 0u && status_.ok();
    }
    [[nodiscard]] const ExternalComputeStatus &status() const noexcept {
        return status_;
    }
    [[nodiscard]] ExternalComputeDispatchCommand *dispatch(
        span<const std::byte> parameter_data,
        uint3 grid_dim,
        uint3 block_dim,
        uint32_t shared_memory_bytes = 0u) const noexcept;
};

class OC_RHI_API ExternalComputeModule final : public RHIResource {
private:
    ExternalComputeStatus status_{};

public:
    ExternalComputeModule() = default;
    ExternalComputeModule(
        Device::Impl *device,
        span<const std::byte> image,
        const string &name) noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return RHIResource::valid() && handle_ != 0u && status_.ok();
    }
    [[nodiscard]] const ExternalComputeStatus &status() const noexcept {
        return status_;
    }
    [[nodiscard]] ExternalComputeKernel kernel(const string &entry) const noexcept;
};

} // namespace ocarina
