#include "external_compute.h"

namespace ocarina {

ExternalComputeModule::ExternalComputeModule(
    Device::Impl *device,
    span<const std::byte> image,
    const string &name) noexcept
    : RHIResource(device, Tag::EXTERNAL_COMPUTE_MODULE, 0u) {
    handle_ = device->create_external_compute_module(
        image.data(), image.size(), name, &status_);
}

ExternalComputeKernel ExternalComputeModule::kernel(
    const string &entry) const noexcept {
    ExternalComputeStatus status{};
    if (!valid()) {
        status = status_;
        if (status.ok()) {
            status.code = ExternalComputeStatusCode::InvalidImage;
        }
        return {0u, status};
    }
    const handle_ty kernel = device_->create_external_compute_kernel(
        handle_, entry, &status);
    return {kernel, status};
}

ExternalComputeDispatchCommand *ExternalComputeKernel::dispatch(
    span<const std::byte> parameter_data,
    uint3 grid_dim,
    uint3 block_dim,
    uint32_t shared_memory_bytes) const noexcept {
    auto status = ocarina::make_shared<ExternalComputeStatus>();
    if (!valid() || parameter_data.empty() ||
        grid_dim.x == 0u || grid_dim.y == 0u || grid_dim.z == 0u ||
        block_dim.x == 0u || block_dim.y == 0u || block_dim.z == 0u ||
        static_cast<uint64_t>(block_dim.x) * block_dim.y * block_dim.z > 1024u) {
        status->code = ExternalComputeStatusCode::InvalidLaunchConfiguration;
        status->backend_code = 0;
    }
    return ExternalComputeDispatchCommand::create(
        entry_, parameter_data, grid_dim, block_dim,
        shared_memory_bytes, ocarina::move(status));
}

} // namespace ocarina
