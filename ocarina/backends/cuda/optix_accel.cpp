//
// Created by Zero on 2022/8/8.
//

#include "optix_accel.h"
#include "cuda_device.h"
#include <optix_stubs.h>
#include "cuda_mesh.h"
#include "cuda_command_visitor.h"

namespace ocarina {

namespace detail {
void mat4x4_to_array12(float4x4 mat, float *output) {

    output[0] = mat[0][0];
    output[1] = mat[1][0];
    output[2] = mat[2][0];
    output[3] = mat[3][0];

    output[4] = mat[0][1];
    output[5] = mat[1][1];
    output[6] = mat[2][1];
    output[7] = mat[3][1];

    output[8] = mat[0][2];
    output[9] = mat[1][2];
    output[10] = mat[2][2];
    output[11] = mat[3][2];
}
}// namespace detail

void OptixAccel::clear() noexcept {
    Accel::Impl::clear();
    tlas_buffer_.destroy();
    instances_.destroy();
    tlas_handle_ = 0;
}

OptixAccelBufferSizes OptixAccel::compute_memory_usage(OptixAccelBuildOptions build_options,
                                                       OptixBuildInput instance_input) const noexcept {
    OptixAccelBufferSizes ias_buffer_sizes;
    OC_OPTIX_CHECK(optixAccelComputeMemoryUsage(device_->optix_device_context(),
                                                &build_options, &instance_input,
                                                1,// num build inputs
                                                &ias_buffer_sizes));
    OC_INFO_FORMAT("tlas: outputSizeInBytes is {} byte, tempSizeInBytes is {} byte, tempUpdateSizeInBytes is {} byte",
                   ias_buffer_sizes.outputSizeInBytes,
                   ias_buffer_sizes.tempSizeInBytes,
                   ias_buffer_sizes.tempUpdateSizeInBytes);
    return ias_buffer_sizes;
}

void OptixAccel::init_instance_input(uint instance_num) noexcept {
    instances_ = Buffer<OptixInstance>(device_, instance_num, "instance buffer");
    instance_input_.type = OPTIX_BUILD_INPUT_TYPE_INSTANCES;
    instance_input_.instanceArray.numInstances = instance_num;
    instance_input_.instanceArray.instances = instances_.ptr<CUdeviceptr>();
}

vector<OptixInstance> OptixAccel::construct_optix_instances() const noexcept {
    vector<OptixInstance> optix_instances;
    uint instance_num = transforms_.size();
    optix_instances.reserve(instance_num);
    for (int i = 0; i < instance_num; ++i) {
        float4x4 transform = transforms_[i];
        OptixInstance optix_instance;
        const auto *cuda_mesh = dynamic_cast<const CUDAMesh *>(meshes_[i].impl());
        optix_instance.traversableHandle = cuda_mesh->blas_handle();
        optix_instance.flags = OPTIX_INSTANCE_FLAG_NONE;
        optix_instance.instanceId = i;
        optix_instance.visibilityMask = 1;
        optix_instance.sbtOffset = 0;
        detail::mat4x4_to_array12(transform, optix_instance.transform);
        optix_instances.push_back(optix_instance);
    }
    return optix_instances;
}

void OptixAccel::update_bvh(ocarina::CUDACommandVisitor *visitor) noexcept {
    size_t instance_num = transforms_.size();
    if (instance_num == 0) {
        clear();
        return;
    }
    if (!allow_update()) {
        build_bvh(visitor);
        return;
    }
    if (tlas_handle_ == 0 || tlas_buffer_.size_in_byte() == 0 || instances_.size() != instance_num) {
        build_bvh(visitor);
        return;
    }

    bool need_rebuild = false;
    device_->use_context([&] {
        OptixAccelBuildOptions accel_options = build_options(AccelBuildTag::UPDATE);
        OptixAccelBufferSizes ias_buffer_sizes = compute_memory_usage(accel_options, instance_input_);
        if (ias_buffer_sizes.outputSizeInBytes > tlas_buffer_.size_in_byte()) {
            need_rebuild = true;
            return;
        }

        auto temp_buffer = Buffer<std::byte>(device_, ias_buffer_sizes.tempUpdateSizeInBytes, "TLAS update temp buffer");
        vector<OptixInstance> optix_instances = construct_optix_instances();
        instances_.upload_immediately(optix_instances.data());
        OC_OPTIX_CHECK(optixAccelBuild(device_->optix_device_context(),
                                       nullptr, &accel_options,
                                       &instance_input_, 1,
                                       temp_buffer.ptr<CUdeviceptr>(),
                                       ias_buffer_sizes.tempUpdateSizeInBytes,
                                       tlas_buffer_.ptr<CUdeviceptr>(),
                                       tlas_buffer_.size_in_byte(),
                                       &tlas_handle_, nullptr, 0));
        mark_update();
        OC_CU_CHECK(cuCtxSynchronize());
    });

    if (need_rebuild) {
        build_bvh(visitor);
    }
}

void OptixAccel::build_bvh(CUDACommandVisitor *visitor) noexcept {
    device_->use_context([&] {
        size_t instance_num = transforms_.size();
        if (instance_num == 0) {
            clear();
            return;
        }
        init_instance_input(instance_num);

        OptixAccelBuildOptions accel_options = build_options(AccelBuildTag::BUILD);

        OptixAccelBufferSizes ias_buffer_sizes = compute_memory_usage(accel_options, instance_input_);

        tlas_buffer_ = Buffer<std::byte>(device_, ias_buffer_sizes.outputSizeInBytes, "TLAS buffer");
        auto temp_buffer = Buffer<std::byte>(device_, ias_buffer_sizes.tempSizeInBytes, "TLAS temp buffer");
        Buffer<uint64_t> compact_size_buffer;
        OptixAccelEmitDesc emit_desc{};
        OptixAccelEmitDesc *emit_desc_ptr = nullptr;
        uint emit_desc_num = 0;
        if (allow_compaction()) {
            compact_size_buffer = Buffer<uint64_t>(device_, 1, "OptixAccel::compact_size_buffer");
            emit_desc.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
            emit_desc.result = compact_size_buffer.handle();
            emit_desc_ptr = &emit_desc;
            emit_desc_num = 1;
        }

        vector<OptixInstance> optix_instances = construct_optix_instances();

        instances_.upload_immediately(optix_instances.data());
        OC_OPTIX_CHECK(optixAccelBuild(device_->optix_device_context(),
                                       nullptr, &accel_options,
                                       &instance_input_, 1,
                                       temp_buffer.ptr<CUdeviceptr>(),
                                       ias_buffer_sizes.tempSizeInBytes,
                                       tlas_buffer_.ptr<CUdeviceptr>(),
                                       ias_buffer_sizes.outputSizeInBytes,
                                       &tlas_handle_, emit_desc_ptr, emit_desc_num));
        mark_build();
        if (allow_compaction()) {
            auto compacted_tlas_size = device_->download<size_t>(compact_size_buffer.handle());
            OC_INFO_FORMAT("tlas : compacted_tlas_size is {} byte", compacted_tlas_size);
            if (compacted_tlas_size < ias_buffer_sizes.outputSizeInBytes) {
                auto compacted_buffer = Buffer<std::byte>(device_, compacted_tlas_size, "TLAS compacted buffer");
                OC_OPTIX_CHECK(optixAccelCompact(device_->optix_device_context(), nullptr,
                                                 tlas_handle_,
                                                 compacted_buffer.ptr<CUdeviceptr>(),
                                                 compacted_tlas_size,
                                                 &tlas_handle_));
                tlas_buffer_ = ocarina::move(compacted_buffer);
                OC_INFO("tlas : optixAccelCompact was executed");
            }
        }
        OC_INFO("tlas handle is ", tlas_handle_);
        OC_CU_CHECK(cuCtxSynchronize());
    });
}

size_t OptixAccel::data_size() const noexcept {
    return CUDADevice::size(Type::Tag::ACCEL);
}

size_t OptixAccel::data_alignment() const noexcept {
    return CUDADevice::alignment(Type::Tag::ACCEL);
}

}// namespace ocarina