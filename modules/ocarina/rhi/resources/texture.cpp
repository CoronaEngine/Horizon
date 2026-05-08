//
// Created by Z on 2026/1/18.
//

#include "texture.h"

namespace ocarina {

Texture::Texture(Device::Impl *device, ocarina::PixelStorage pixel_storage,
                 RHIResource::Tag tag, ocarina::handle_ty handle)
    : RHIResource(device, tag, handle) {
}

void Texture::upload_immediately(const void *data) const noexcept {
    upload_sync(data)->accept(*device_->command_visitor());
}

void Texture::download_immediately(void *data) const noexcept {
    download_sync(data)->accept(*device_->command_visitor());
}

//#region Texture2D

Texture2D::Texture2D(Device::Impl *device, uint2 res,
                     PixelStorage pixel_storage, uint level_num,
                     const string &desc)
    : Texture(device, Tag::TEXTURE2D,
              device->create_texture2d(res, pixel_storage,
                                       detail::compute_mip_level_num(make_uint3(res, 1), level_num), desc)) {}

Texture2D::Texture2D(Device::Impl *device, Image *image_resource, const TextureViewCreation &texture_view)
    : Texture(device, Tag::TEXTURE2D,
              device->create_texture2d(image_resource, texture_view)) {}

Texture2D::Texture2D(Device::Impl *device, ocarina::uint external_handle,
                     const string &desc)
    : Texture(device, Tag::TEXTURE2D, device->create_texture2d_from_external(external_handle, desc)) {}

TextureOpCommand *Texture2D::upload(const void *data, bool async) const noexcept {
    return Texture2DUploadCommand::create(data, array_handle(), impl()->resolution(),
                                          impl()->pixel_storage(), async);
}

TextureOpCommand *Texture2D::upload_sync(const void *data) const noexcept {
    return upload(data, false);
}

TextureOpCommand *Texture2D::download(void *data, bool async) const noexcept {
    return Texture2DDownloadCommand::create(data, array_handle(), impl()->resolution(),
                                            impl()->pixel_storage(), async);
}

TextureOpCommand *Texture2D::download_sync(void *data) const noexcept {
    return download(data, false);
}

DataCopyCommand *Texture2D::copy_from(const ocarina::Texture &src, bool async) const noexcept {
    return Texture2DCopyCommand::create(src.array_handle(), array_handle(),
                                        resolution(), impl()->pixel_storage(), 0, 0, async);
}

BufferToTextureCommand *Texture2D::copy_from_buffer_impl(handle_ty buffer_handle,
                                                         size_t buffer_offset_in_byte, bool async) const noexcept {
    return BufferToTexture2DCommand::create(buffer_handle, buffer_offset_in_byte, array_handle(),
                                            impl()->pixel_storage(),
                                            impl()->resolution(), 0, async);
}

const Expression *Texture2D::expression() const noexcept {
    const CapturedResource &captured_resource = Function::current()->get_captured_resource(Type::of<decltype(*this)>(),
                                                                                           Variable::Tag::TEXTURE2D,
                                                                                           memory_block());
    return captured_resource.expression();
}

//#endregion

//#region Texture3D
Texture3D::Texture3D(Device::Impl *device, uint3 res,
                     PixelStorage pixel_storage, uint level_num,
                     const string &desc)
    : Texture(device, Tag::TEXTURE3D,
              device->create_texture3d(res, pixel_storage,
                                       detail::compute_mip_level_num(res, level_num), desc)) {}

Texture3D::Texture3D(Device::Impl *device, Image *image_resource, const TextureViewCreation &texture_view)
    : Texture(device, Tag::TEXTURE3D,
              device->create_texture3d(image_resource, texture_view)) {}

TextureOpCommand *Texture3D::upload(const void *data, bool async) const noexcept {
    return Texture3DUploadCommand::create(data, array_handle(), impl()->resolution(),
                                          impl()->pixel_storage(), async);
}

TextureOpCommand *Texture3D::upload_sync(const void *data) const noexcept {
    return upload(data, false);
}

TextureOpCommand *Texture3D::download(void *data, bool async) const noexcept {
    return Texture3DDownloadCommand::create(data, array_handle(), impl()->resolution(),
                                            impl()->pixel_storage(), async);
}

TextureOpCommand *Texture3D::download_sync(void *data) const noexcept {
    return download(data, false);
}

DataCopyCommand *Texture3D::copy_from(const ocarina::Texture &src, bool async) const noexcept {
    return Texture3DCopyCommand::create(src.array_handle(), array_handle(),
                                        resolution(), impl()->pixel_storage(), 0, 0, async);
}

BufferToTextureCommand *Texture3D::copy_from_buffer_impl(handle_ty buffer_handle,
                                                         size_t buffer_offset_in_byte, bool async) const noexcept {
    return BufferToTexture3DCommand::create(buffer_handle, buffer_offset_in_byte, array_handle(),
                                            impl()->pixel_storage(),
                                            impl()->resolution(), 0, async);
}

const Expression *Texture3D::expression() const noexcept {
    const CapturedResource &captured_resource = Function::current()->get_captured_resource(Type::of<decltype(*this)>(),
                                                                                           Variable::Tag::TEXTURE3D,
                                                                                           memory_block());
    return captured_resource.expression();
}

//#endregion

}// namespace ocarina