//
// Created by Zero on 06/06/2022.
//

#include "device.h"
#include "resources/texture.h"
#include "resources/stream.h"
#include "rtx/accel.h"
#include "resources/bindless_array.h"
#include "resources/byte_buffer.h"
#include "resources/dynamic_buffer.h"
#include "resources/external_compute.h"
#include "resources/command_graph.h"
#include "context.h"
#include "core/runtime/dynamic_module.h"

namespace ocarina {

handle_ty Device::Impl::create_external_compute_module(
    const std::byte *, size_t, const string &,
    ExternalComputeStatus *status) noexcept {
    if (status) {
        status->code = ExternalComputeStatusCode::UnsupportedBackend;
        status->backend_code = 0;
    }
    return 0;
}

handle_ty Device::Impl::create_external_compute_kernel(
    handle_ty, const string &, ExternalComputeStatus *status) noexcept {
    if (status) {
        status->code = ExternalComputeStatusCode::UnsupportedBackend;
        status->backend_code = 0;
    }
    return 0;
}

handle_ty Device::Impl::create_command_graph(
    CommandGraphStatus *status) noexcept {
    if (status) {
        status->code = CommandGraphStatusCode::UnsupportedBackend;
        status->backend_code = 0;
    }
    return 0u;
}

bool Device::Impl::prepare_command_graph(
    handle_ty,
    span<Command *const>,
    CommandGraphStatus *status) noexcept {
    if (status) {
        status->code = CommandGraphStatusCode::UnsupportedBackend;
        status->backend_code = 0;
    }
    return false;
}

ByteBuffer Device::create_byte_buffer(size_t size, const std::string &name) const noexcept {
    return ByteBuffer(impl_.get(), size, name);
}

detail::RawDynamicBuffer Device::create_raw_dynamic_buffer(const Type *logical_type,
                                                           StoragePrecisionPolicy policy,
                                                           size_t element_count,
                                                           const string &name,
                                                           DynamicBufferLayout layout) const noexcept {
    return create<detail::RawDynamicBuffer>(logical_type, policy, element_count, layout, name);
}

detail::RawDynamicBuffer Device::create_raw_dynamic_buffer_resolved(const Type *resolved_type,
                                                                    size_t element_count,
                                                                    const string &name,
                                                                    DynamicBufferLayout layout) const noexcept {
    return create<detail::RawDynamicBuffer>(resolved_type, element_count, layout, name);
}

Stream Device::create_stream() noexcept {
    return create<Stream>();
}

ExternalComputeModule Device::create_external_compute_module(
    span<const std::byte> image,
    const string &name) const noexcept {
    return ExternalComputeModule(impl_.get(), image, name);
}

CommandGraph Device::create_command_graph() const noexcept {
    return CommandGraph(impl_.get());
}

Accel Device::create_accel(AccelUsageTag usage_tag) const noexcept {
    return create<Accel>(usage_tag);
}

BindlessArray Device::create_bindless_array() const noexcept {
    return create<BindlessArray>();
}

Texture3D Device::create_texture3d(uint3 res, PixelStorage storage, const string &desc) const noexcept {
    return create<Texture3D>(res, storage, 1, desc);
}

Texture3D Device::create_texture3d(uint2 res, PixelStorage storage, const string &desc) const noexcept {
    return create_texture3d(make_uint3(res, 1u), storage, desc);
}

Texture2D Device::create_texture2d(ocarina::uint2 res, ocarina::PixelStorage storage,
                                   const std::string &desc) const noexcept {
    return create<Texture2D>(res, storage, 1, desc);
}

Texture2D Device::create_texture2d_from_external(ocarina::uint external_handle,
    const string &desc) const noexcept {
    return create<Texture2D>(external_handle, desc);
}

Texture3D Device::create_texture(Image *image_resource, const TextureViewCreation &texture_view,
                                 const string &desc) const noexcept {
    Texture3D tex(impl_.get(), image_resource, texture_view);
    return tex;
}

Device Device::create_device(const string &backend_name, const ocarina::InstanceCreation &instance_creation) {
    RHIContext &rhi_context = RHIContext::instance();
    return rhi_context.create_device(backend_name, instance_creation);
}

Device Device::create_device(const string &backend_name) {
    RHIContext &rhi_context = RHIContext::instance();
    return rhi_context.create_device(backend_name);
}

}// namespace ocarina
