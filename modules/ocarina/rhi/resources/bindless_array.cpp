//
// Created by Zero on 12/01/2023.
//

#include "bindless_array.h"
#include "texture.h"
#include "buffer.h"
#include "byte_buffer.h"
#include "managed.h"

namespace ocarina {

BindlessArray::BindlessArray(Device::Impl *device)
    : RHIResource(device, Tag::BINDLESS_ARRAY,
                  device->create_bindless_array()) {}

size_t BindlessArray::emplace(const Texture3D &texture) noexcept {
    return impl()->emplace_texture3d(texture.tex_handle());
}

void BindlessArray::set_texture3d(ocarina::handle_ty index,
                                const ocarina::Texture3D &texture) noexcept {
    impl()->set_texture3d(index, texture.tex_handle());
}

size_t BindlessArray::emplace(const Texture2D &texture) noexcept {
    return impl()->emplace_texture2d(texture.tex_handle());
}

void BindlessArray::set_texture2d(ocarina::handle_ty index,
                                  const ocarina::Texture2D &texture) noexcept {
    impl()->set_texture2d(index, texture.tex_handle());
}

ByteBufferView BindlessArray::byte_buffer_view(ocarina::uint index) const noexcept {
    ByteBufferDesc buffer_desc = impl()->buffer_view(index);
    return {buffer_desc.head(), buffer_desc.size_in_byte()};
}

CommandBatch BindlessArray::upload_handles(bool async) noexcept {
    CommandBatch ret;
    ret.push_back(impl()->upload_buffer_handles(async));
    ret.push_back(impl()->upload_texture3d_handles(async));
    ret.push_back(impl()->upload_texture2d_handles(async));
    return ret;
}

uint BindlessArray::buffer_num() const noexcept {
    return impl()->buffer_num();
}

uint BindlessArray::texture3d_num() const noexcept {
    return impl()->texture3d_num();
}

uint BindlessArray::texture2d_num() const noexcept {
    return impl()->texture2d_num();
}

}// namespace ocarina