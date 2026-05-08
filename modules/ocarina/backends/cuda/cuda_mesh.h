//
// Created by Zero on 09/08/2022.
//

#pragma once

#include "core/stl.h"
#include "rhi/rtx/mesh.h"
#include "util.h"

namespace ocarina {
class CUDADevice;
class CUDAMesh : public RHIMesh::Impl {
private:
    CUDADevice *device_;
    MeshParams params_;
    handle_ty final_vert_handle{};
    OptixTraversableHandle blas_handle_{};
    Buffer<std::byte> blas_buffer_{};
    OptixBuildInput build_input_{};

public:
    CUDAMesh(CUDADevice *device, const MeshParams &params)
        : device_(device), params_(params) {}
    void update_mesh(const MeshParams &mesh_params) noexcept { params_ = mesh_params; }
    void init_build_input() noexcept;
    [[nodiscard]] handle_ty blas_handle() const noexcept override { return blas_handle_; }
    [[nodiscard]] uint vertex_num() const noexcept override { return params_.vert_num; }
    [[nodiscard]] uint triangle_num() const noexcept override { return params_.tri_num; }
    void build_bvh(const BLASBuildCommand *cmd) noexcept;
};
}// namespace ocarina