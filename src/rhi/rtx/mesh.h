//
// Created by Zero on 2022/8/8.
//

#pragma once

#include "rhi/resources/resource.h"
#include "rhi/command.h"
#include "dsl/types/struct.h"
#include "../resources/buffer.h"

namespace ocarina {

class RHIMesh : public RHIResource {
public:
    class Impl {
    public:
        virtual ~Impl() = default;
        [[nodiscard]] virtual handle_ty blas_handle() const noexcept = 0;
        [[nodiscard]] virtual uint vertex_num() const noexcept = 0;
        [[nodiscard]] virtual uint triangle_num() const noexcept = 0;
    };

public:
    RHIMesh() = default;
    RHIMesh(Device::Impl *device, const MeshParams &params)
        : RHIResource(device, Tag::MESH,
                      device->create_mesh(params)) {}

    [[nodiscard]] const Impl *impl() const noexcept { return reinterpret_cast<const Impl *>(handle_); }
    [[nodiscard]] Impl *impl() noexcept { return reinterpret_cast<Impl *>(handle_); }
    [[nodiscard]] uint vertex_num() const noexcept { return impl()->vertex_num(); }
    [[nodiscard]] uint triangle_num() const noexcept { return impl()->triangle_num(); }
    [[nodiscard]] BLASBuildCommand *build_bvh() noexcept { return BLASBuildCommand::create(handle_); }
};

template<typename VBuffer, typename TBuffer>
RHIMesh Device::create_mesh(const VBuffer &v_buffer, const TBuffer &t_buffer,
                            AccelUsageTag usage_tag, AccelGeomTag geom_tag) const noexcept {
    MeshParams params;
    params.vert_handle = v_buffer.handle();
    params.vert_offset = v_buffer.offset_in_byte();
    params.vert_stride = v_buffer.element_size();
    params.vert_num = v_buffer.size();

    params.tri_handle = t_buffer.handle();
    params.tri_offset = t_buffer.offset_in_byte();
    params.tri_num = t_buffer.size();
    params.tri_stride = t_buffer.element_size();

    params.usage_tag = usage_tag;
    params.geom_tag = geom_tag;

    return create<RHIMesh>(params);
}

}// namespace ocarina
