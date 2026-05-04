//
// Created by Zero on 09/08/2022.
//

#include "cuda_mesh.h"
#include "cuda_device.h"
#include <optix_stack_size.h>
#include <optix.h>
#include <optix_stubs.h>

namespace ocarina {

void CUDAMesh::build_bvh(const BLASBuildCommand *cmd) noexcept {
    device_->use_context([&] {
        init_build_input();
        OptixAccelBuildOptions accel_options = {};
        accel_options.buildFlags = (OPTIX_BUILD_FLAG_ALLOW_COMPACTION | OPTIX_BUILD_FLAG_PREFER_FAST_TRACE);
        accel_options.motionOptions.numKeys = 1;
        accel_options.operation = OPTIX_BUILD_OPERATION_BUILD;

        OptixAccelBufferSizes gas_buffer_sizes;
        OC_OPTIX_CHECK(optixAccelComputeMemoryUsage(
            device_->optix_device_context(),
            &accel_options,
            &build_input_,
            1,// num_build_inputs
            &gas_buffer_sizes));

        OC_INFO_FORMAT("blas outputSizeInBytes is {} byte, tempSizeInBytes is {} byte",
                       gas_buffer_sizes.outputSizeInBytes,
                       gas_buffer_sizes.tempSizeInBytes);

        blas_buffer_ = Buffer<std::byte>(device_, gas_buffer_sizes.outputSizeInBytes, "mesh BLAS buffer");

        Buffer temp_buffer = Buffer(device_, gas_buffer_sizes.tempSizeInBytes, "mesh BLAS temp buffer");
        Buffer compact_size_buffer = Buffer<uint64_t>(device_, 1, "mesh BLAS compact_size_buffer");
        OptixAccelEmitDesc emit_desc;
        emit_desc.type = OPTIX_PROPERTY_TYPE_COMPACTED_SIZE;
        emit_desc.result = compact_size_buffer.handle();

        OC_OPTIX_CHECK(optixAccelBuild(device_->optix_device_context(), nullptr,
                                       &accel_options, &build_input_, 1,
                                       temp_buffer.handle(), gas_buffer_sizes.tempSizeInBytes,
                                       blas_buffer_.handle(), gas_buffer_sizes.outputSizeInBytes,
                                       &blas_handle_, &emit_desc, 1));

        size_t compacted_gas_size = device_->download<size_t>(emit_desc.result);

        OC_INFO_FORMAT("blas : compacted_gas_size is {} byte", compacted_gas_size);

        if (compacted_gas_size < gas_buffer_sizes.outputSizeInBytes) {
            auto blas_buffer = Buffer<std::byte>(device_, compacted_gas_size, "mesh BLAS compacted buffer");
            OC_OPTIX_CHECK(optixAccelCompact(device_->optix_device_context(), nullptr,
                                             blas_handle_,
                                             blas_buffer.handle(),
                                             compacted_gas_size,
                                             &blas_handle_));
            OC_INFO("blas : optixAccelCompact was executed");
            blas_buffer_ = ocarina::move(blas_buffer);
        }
        OC_CU_CHECK(cuCtxSynchronize());
    });
}

void CUDAMesh::init_build_input() noexcept {
    build_input_.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
    {
        build_input_.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        build_input_.triangleArray.vertexStrideInBytes = params_.vert_stride;
        build_input_.triangleArray.numVertices = params_.vert_num;
        final_vert_handle = params_.vert_handle + params_.vert_offset;
        build_input_.triangleArray.vertexBuffers = reinterpret_cast<const CUdeviceptr *>(&final_vert_handle);
    }
    {
        build_input_.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_UNSIGNED_INT3;
        build_input_.triangleArray.indexStrideInBytes = params_.tri_stride;
        build_input_.triangleArray.numIndexTriplets = params_.tri_num;
        build_input_.triangleArray.indexBuffer = params_.tri_handle + params_.tri_offset;
    }
    {
        static constexpr uint32_t geom_flags = OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
        build_input_.triangleArray.flags = &geom_flags;
        build_input_.triangleArray.numSbtRecords = 1;
        build_input_.triangleArray.sbtIndexOffsetBuffer = reinterpret_cast<CUdeviceptr>(nullptr);
        build_input_.triangleArray.sbtIndexOffsetSizeInBytes = sizeof(uint32_t);
        build_input_.triangleArray.sbtIndexOffsetStrideInBytes = sizeof(uint32_t);
    }
}

}// namespace ocarina