#include "cuda_external_compute.h"

#include "cuda_device.h"

namespace ocarina {

CUDAExternalComputeModule::~CUDAExternalComputeModule() noexcept {
    if (!module_) {
        return;
    }
    device_->use_context([&] {
        const CUresult result = cuModuleUnload(module_);
        if (result != CUDA_SUCCESS) {
            OC_WARNING_FORMAT(
                "CUDA external compute module unload failed with code {}.",
                static_cast<int>(result));
        }
    });
    module_ = nullptr;
}

bool CUDAExternalComputeModule::load(
    const std::byte *image,
    size_t image_size,
    ExternalComputeStatus *status) noexcept {
    if (!status) {
        return false;
    }
    if (!image || image_size == 0u) {
        status->code = ExternalComputeStatusCode::InvalidImage;
        status->backend_code = 0;
        return false;
    }
    const CUresult result = device_->use_context([&] {
        return cuModuleLoadDataEx(&module_, image, 0u, nullptr, nullptr);
    });
    if (result != CUDA_SUCCESS) {
        module_ = nullptr;
        status->code = ExternalComputeStatusCode::ModuleLoadFailed;
        status->backend_code = static_cast<int64_t>(result);
        return false;
    }
    status->code = ExternalComputeStatusCode::Success;
    status->backend_code = 0;
    return true;
}

handle_ty CUDAExternalComputeModule::kernel(
    const string &entry,
    ExternalComputeStatus *status) noexcept {
    if (!status || !module_ || entry.empty()) {
        if (status) {
            status->code = ExternalComputeStatusCode::EntryNotFound;
            status->backend_code = 0;
        }
        return 0u;
    }
    CUfunction function{};
    const CUresult result = device_->use_context([&] {
        return cuModuleGetFunction(&function, module_, entry.c_str());
    });
    if (result != CUDA_SUCCESS || !function) {
        status->code = ExternalComputeStatusCode::EntryNotFound;
        status->backend_code = static_cast<int64_t>(result);
        return 0u;
    }
    status->code = ExternalComputeStatusCode::Success;
    status->backend_code = 0;
    return reinterpret_cast<handle_ty>(function);
}

} // namespace ocarina
