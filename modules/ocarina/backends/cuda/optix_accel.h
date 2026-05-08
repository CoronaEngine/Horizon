//
// Created by Zero on 2022/8/8.
//

#pragma once

#include "core/stl.h"
#include "rhi/rtx/accel.h"
#include "util.h"

namespace ocarina {
class CUDADevice;
class CUDACommandVisitor;
class OptixAccel : public Accel::Impl {
private:
    Buffer<std::byte> tlas_buffer_;
    OptixTraversableHandle tlas_handle_{};
    CUDADevice *device_;
    Buffer<OptixInstance> instances_{};
    OptixBuildInput instance_input_{};

public:
    explicit OptixAccel(CUDADevice *device, AccelUsageTag usage_tag) : Accel::Impl(usage_tag), device_(device) {}
    void build_bvh(CUDACommandVisitor *visitor) noexcept;
    void update_bvh(CUDACommandVisitor *visitor) noexcept;
    [[nodiscard]] constexpr bool allow_update() const noexcept {
        return usage_tag_ == FAST_UPDATE;
    }
    [[nodiscard]] constexpr bool allow_compaction() const noexcept {
        return usage_tag_ != FAST_UPDATE;
    }
    [[nodiscard]] constexpr OptixAccelBuildOptions build_options(AccelBuildTag build_tag) const noexcept {
        OptixAccelBuildOptions accel_options = {};
        switch (usage_tag_) {
            case FAST_BUILD:
                accel_options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_BUILD;
                break;
            case FAST_UPDATE:
                accel_options.buildFlags = (OPTIX_BUILD_FLAG_ALLOW_UPDATE | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE);
                break;
            case FAST_TRACE:
            default:
                accel_options.buildFlags = (OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE);
                break;
        }
        accel_options.operation = build_tag == AccelBuildTag::BUILD ?
                                      OPTIX_BUILD_OPERATION_BUILD :
                                      OPTIX_BUILD_OPERATION_UPDATE;
        return accel_options;
    }
    [[nodiscard]] vector<OptixInstance> construct_optix_instances() const noexcept;
    [[nodiscard]] OptixAccelBufferSizes compute_memory_usage(OptixAccelBuildOptions build_options,
                                                             OptixBuildInput instance_input) const noexcept;
    void init_instance_input(uint instance_num) noexcept;
    [[nodiscard]] size_t data_size() const noexcept override;
    [[nodiscard]] size_t data_alignment() const noexcept override;
    void clear() noexcept override;
    [[nodiscard]] handle_ty handle() const noexcept override { return tlas_handle_; }
    [[nodiscard]] const void *handle_ptr() const noexcept override { return &tlas_handle_; }
};
}// namespace ocarina