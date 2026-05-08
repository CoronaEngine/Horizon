//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "resource.h"
#include "rhi/command.h"
#include "texture_sampler.h"
#include "../graphics_descriptions.h"

namespace ocarina {

template<typename texture_type>
class TextureBehaviour {
public:
    [[nodiscard]] const texture_type *self() const noexcept {
        return static_cast<const texture_type *>(this);
    }

    [[nodiscard]] texture_type *self() noexcept {
        return static_cast<texture_type *>(this);
    }

    template<typename U, typename V>
    requires(is_all_floating_point_expr_v<U, V>)
    [[nodiscard]] auto sample(uint channel_num, const U &u, const V &v) const noexcept {
        return make_expr<texture_type>(self()->expression()).sample(channel_num, u, v);
    }

    template<typename UV>
    requires(is_float_vector2_v<expr_value_t<swizzle_decay_t<UV>>>)
    OC_NODISCARD auto sample(uint channel_num, const UV &uv) const noexcept {
        return [this]<typename T>(uint channel_num, const T &uv) {
            return this->sample(channel_num, uv.x, uv.y);
        }(channel_num, decay_swizzle(uv));
    }

    template<typename U, typename V, typename W>
    requires(is_all_floating_point_expr_v<U, V>)
    [[nodiscard]] auto sample(uint channel_num, const U &u, const V &v, const W &w) const noexcept {
        return make_expr<texture_type>(self()->expression()).sample(channel_num, u, v, w);
    }

    template<typename UVW>
    requires(is_float_vector3_v<expr_value_t<swizzle_decay_t<UVW>>>)
    OC_NODISCARD auto sample(uint channel_num, const UVW &uvw) const noexcept {
        return [this]<typename T>(uint channel_num, const T &uvw) {
            return this->sample(channel_num, uvw.x, uvw.y, uvw.z);
        }(channel_num, decay_swizzle(uvw));
    }

    template<typename Target, typename X, typename Y>
    requires(is_all_integral_expr_v<X, Y>)
    OC_NODISCARD auto read(const X &x, const Y &y) const noexcept {
        return make_expr<texture_type>(self()->expression()).template read<Target>(x, y);
    }

    template<typename Target, typename XY>
    requires((is_general_integer_vector2_v<remove_device_t<XY>>) &&
             (is_uchar_element_expr_v<Target> || is_float_element_expr_v<Target>))
    OC_NODISCARD auto read(const XY &xy) const noexcept {
        return [this]<typename T>(const T &xy) {
            return this->read<Target>(xy.x, xy.y);
        }(decay_swizzle(xy));
    }

    template<typename Target, typename X, typename Y, typename Z>
    requires(is_all_integral_expr_v<X, Y>)
    OC_NODISCARD auto read(const X &x, const Y &y, const Z &z) const noexcept {
        return make_expr<texture_type>(self()->expression()).template read<Target>(x, y, z);
    }

    template<typename Target, typename XYZ>
    requires((is_general_integer_vector3_v<remove_device_t<XYZ>>) &&
             (is_uchar_element_expr_v<Target> || is_float_element_expr_v<Target>))
    OC_NODISCARD auto read(const XYZ &xyz) const noexcept {
        return [this]<typename T>(const T &xyz) {
            return this->read<Target>(xyz.x, xyz.y, xyz.z);
        }(decay_swizzle(xyz));
    }

    template<typename X, typename Y, typename Val>
    requires(is_all_integral_expr_v<X, Y> &&
             (is_uchar_element_expr_v<Val> || is_float_element_expr_v<Val>))
    void write(const Val &elm, const X &x, const Y &y) noexcept {
        make_expr<texture_type>(self()->expression()).write(elm, x, y);
    }

    template<typename Target, typename XY>
    requires((is_general_integer_vector2_v<remove_device_t<XY>>) &&
             (is_uchar_element_expr_v<Target> || is_float_element_expr_v<Target>))
    void write(const Target &elm, const XY &xy) noexcept {
        [this]<typename T>(const Target &elm, const T &xy) {
            this->write(elm, xy.x, xy.y);
        }(elm, decay_swizzle(xy));
    }

    template<typename X, typename Y, typename Z, typename Val>
    requires(is_all_integral_expr_v<X, Y, Z> &&
             (is_uchar_element_expr_v<Val> || is_float_element_expr_v<Val>))
    void write(const Val &elm, const X &x, const Y &y, const Z &z) noexcept {
        make_expr<texture_type>(self()->expression()).write(elm, x, y, z);
    }

    template<typename Target, typename XYZ>
    requires((is_general_integer_vector3_v<remove_device_t<XYZ>>) &&
             (is_uchar_element_expr_v<Target> || is_float_element_expr_v<Target>))
    void write(const Target &elm, const XYZ &xyz) noexcept {
        [this]<typename T>(const Target &elm, const T &xyz) {
            this->write(elm, xyz.x, xyz.y, xyz.z);
        }(elm, decay_swizzle(xyz));
    }
};

class Image;
namespace detail {
[[nodiscard]] constexpr uint compute_mip_level_num(uint3 res, uint request_level_num) noexcept {
    uint max_size = std::max({res.x, res.y, res.z});
    auto max_levels = 0u;
    while (max_size != 0u) {
        max_size >>= 1u;
        max_levels++;
    }
    return request_level_num == 0 ? max_levels : std::min(request_level_num, max_levels);
}
}// namespace detail

class OC_RHI_API Texture : public RHIResource {
public:
    class Impl {
    public:
        virtual ~Impl() = default;
        [[nodiscard]] virtual PixelStorage pixel_storage() const noexcept = 0;
        [[nodiscard]] virtual handle_ty array_handle() const noexcept = 0;
        [[nodiscard]] virtual handle_ty tex_handle() const noexcept = 0;
        [[nodiscard]] virtual const TextureDesc &descriptor() const noexcept = 0;
        [[nodiscard]] virtual bool is_external() const noexcept {
            OC_NOT_IMPLEMENT_ERROR(is_external);
            return false;
        }
        /// for device side structure
        [[nodiscard]] virtual const void *handle_ptr() const noexcept = 0;
        [[nodiscard]] virtual size_t data_size() const noexcept = 0;
        [[nodiscard]] virtual size_t data_alignment() const noexcept = 0;
        [[nodiscard]] virtual size_t max_member_size() const noexcept = 0;
        [[nodiscard]] virtual uint3 resolution() const noexcept = 0;
    };

public:
    using RHIResource::RHIResource;
    Texture(Device::Impl *device, PixelStorage pixel_storage,
            RHIResource::Tag tag, handle_ty handle);


    [[nodiscard]] Impl *impl() noexcept { return reinterpret_cast<Impl *>(handle_); }
    [[nodiscard]] const Impl *impl() const noexcept { return reinterpret_cast<const Impl *>(handle_); }
    [[nodiscard]] Impl *operator->() noexcept { return impl(); }
    [[nodiscard]] const Impl *operator->() const noexcept { return impl(); }
    [[nodiscard]] uint pixel_num() const noexcept {
        uint3 res = impl()->resolution();
        return res.x * res.y * res.z;
    }

    [[nodiscard]] uint size_in_byte() const noexcept {
        return pixel_num() * pixel_size();
    }

    [[nodiscard]] uint pixel_size() const noexcept {
        return ::ocarina::pixel_size(impl()->pixel_storage());
    }
    void upload_immediately(const void *data) const noexcept;
    void download_immediately(void *data) const noexcept;
    [[nodiscard]] virtual TextureOpCommand *upload(const void *data, bool async = true) const noexcept = 0;
    [[nodiscard]] virtual TextureOpCommand *upload_sync(const void *data) const noexcept = 0;
    [[nodiscard]] virtual TextureOpCommand *download(void *data, bool async = true) const noexcept = 0;
    [[nodiscard]] virtual TextureOpCommand *download_sync(void *data) const noexcept = 0;
    [[nodiscard]] virtual DataCopyCommand *copy_from(const Texture &src,
                                                     bool async = true) const noexcept = 0;
    [[nodiscard]] virtual BufferToTextureCommand *copy_from_buffer_impl(handle_ty buffer_handle, size_t buffer_offset_in_byte,
                                                                 bool async = true) const noexcept = 0;

    template<typename Arg>
    requires is_buffer_or_view_v<Arg>
    [[nodiscard]] BufferToTextureCommand *copy_from_buffer(const Arg &buffer, size_t buffer_offset, bool async = true) const noexcept {
        return copy_from_buffer_impl(buffer.handle(), buffer_offset * buffer.element_size(), async);
    }

    [[nodiscard]] uint3 resolution() const noexcept { return impl()->resolution(); }
    [[nodiscard]] handle_ty array_handle() const noexcept { return impl()->array_handle(); }
    [[nodiscard]] handle_ty tex_handle() const noexcept { return impl()->tex_handle(); }
    [[nodiscard]] const void *handle_ptr() const noexcept override { return impl()->handle_ptr(); }
    [[nodiscard]] size_t data_size() const noexcept override { return impl()->data_size(); }
    [[nodiscard]] size_t data_alignment() const noexcept override { return impl()->data_alignment(); }
    [[nodiscard]] size_t max_member_size() const noexcept override { return impl()->max_member_size(); }
};

class OC_RHI_API Texture2D : public Texture, public TextureBehaviour<Texture2D> {
public:
    Texture2D() = default;
    explicit Texture2D(Device::Impl *device, uint2 res,
                       PixelStorage pixel_storage, uint level_num = 1u,
                       const string &desc = "");

    explicit Texture2D(Device::Impl *device, Image *image_resource,
                       const TextureViewCreation &texture_view);

    Texture2D(Device::Impl *device, uint external_handle,
              const string &desc = "");

    [[nodiscard]] TextureOpCommand *upload(const void *data, bool async = true) const noexcept override;
    [[nodiscard]] TextureOpCommand *upload_sync(const void *data) const noexcept override;
    [[nodiscard]] TextureOpCommand *download(void *data, bool async = true) const noexcept override;
    [[nodiscard]] TextureOpCommand *download_sync(void *data) const noexcept override;
    [[nodiscard]] DataCopyCommand *copy_from(const ocarina::Texture &src,
                                             bool async = true) const noexcept override;
    [[nodiscard]] BufferToTextureCommand *copy_from_buffer_impl(handle_ty buffer_handle,
                                                         size_t buffer_offset_in_byte,
                                                         bool async) const noexcept override;
    /// for dsl
    [[nodiscard]] const Expression *expression() const noexcept override;
};

class OC_RHI_API Texture3D : public Texture, public TextureBehaviour<Texture3D> {
public:
    Texture3D() = default;
    explicit Texture3D(Device::Impl *device, uint3 res,
                       PixelStorage pixel_storage, uint level_num = 1u,
                       const string &desc = "");

    explicit Texture3D(Device::Impl *device, Image *image_resource,
                       const TextureViewCreation &texture_view);

    [[nodiscard]] TextureOpCommand *upload(const void *data, bool async = true) const noexcept override;
    [[nodiscard]] TextureOpCommand *upload_sync(const void *data) const noexcept override;
    [[nodiscard]] TextureOpCommand *download(void *data, bool async = true) const noexcept override;
    [[nodiscard]] TextureOpCommand *download_sync(void *data) const noexcept override;
    [[nodiscard]] DataCopyCommand *copy_from(const ocarina::Texture &src,
                                             bool async = true) const noexcept override;
    [[nodiscard]] BufferToTextureCommand *copy_from_buffer_impl(handle_ty buffer_handle,
                                                         size_t buffer_offset_in_byte,
                                                         bool async) const noexcept override;
    /// for dsl
    [[nodiscard]] const Expression *expression() const noexcept override;
};

}// namespace ocarina