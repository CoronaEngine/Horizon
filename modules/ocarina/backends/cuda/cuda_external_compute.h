#pragma once

#include "rhi/command.h"
#include <cuda.h>

namespace ocarina {

class CUDADevice;

class CUDAExternalComputeModule {
private:
    CUDADevice *device_{};
    CUmodule module_{};

public:
    explicit CUDAExternalComputeModule(CUDADevice *device) noexcept
        : device_(device) {}
    ~CUDAExternalComputeModule() noexcept;

    CUDAExternalComputeModule(const CUDAExternalComputeModule &) = delete;
    CUDAExternalComputeModule &operator=(const CUDAExternalComputeModule &) = delete;

    [[nodiscard]] bool load(
        const std::byte *image,
        size_t image_size,
        ExternalComputeStatus *status) noexcept;
    [[nodiscard]] handle_ty kernel(
        const string &entry,
        ExternalComputeStatus *status) noexcept;
};

} // namespace ocarina
