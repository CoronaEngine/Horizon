//
// Created by Zero on 26/07/2022.
//

#pragma once

#include "core/image/image_base.h"

namespace ocarina {

class OC_CORE_API ImageView : public ImageBase {
private:
    const std::byte *pixel_{nullptr};

public:
    ImageView(PixelStorage pixel_storage, const std::byte *pixel, uint2 res);

    ImageView(const float4 *pixel, uint2 res)
        : ImageView(PixelStorage::FLOAT4,
                    reinterpret_cast<const std::byte *>(pixel), res) {}
    ImageView(const uchar4 *pixel, uint2 res)
        : ImageView(PixelStorage::BYTE4,
                    reinterpret_cast<const std::byte *>(pixel), res) {}
    template<typename T = std::byte>
    const T *pixel_ptr() const { return reinterpret_cast<const T *>(pixel_); }

    template<typename T = std::byte>
    T *pixel_ptr() { return reinterpret_cast<T *>(const_cast<std::byte *>(pixel_)); }
};

class OC_CORE_API Image : public ImageBase {
private:
    fs::path path_;
    std::unique_ptr<const std::byte[]> pixel_;

public:
    using foreach_signature = void(const std::byte *, int, PixelStorage);

public:
    Image() = default;
    Image(PixelStorage pixel_storage, const std::byte *pixel, uint2 res, const fs::path &path);
    Image(PixelStorage pixel_storage, const std::byte *pixel, uint2 res);
    Image(Image &&other) noexcept;
    [[nodiscard]] ImageView view() const noexcept;
    Image(const Image &other) = delete;
    Image &operator=(const Image &other) = delete;
    Image &operator=(Image &&other) noexcept;
    OC_MAKE_MEMBER_GETTER(path, &)
    template<typename T = std::byte>
    const T *pixel_ptr() const { return reinterpret_cast<const T *>(pixel_.get()); }
    template<typename T = std::byte>
    T *pixel_ptr() { return reinterpret_cast<T *>(const_cast<std::byte *>(pixel_.get())); }
    [[nodiscard]] static Image pure_color(float4 color, ColorSpace color_space, uint2 res = make_uint2(1u));
    [[nodiscard]] static Image load(const fs::path &fn, ColorSpace color_space, float3 scale = make_float3(1.f));
    [[nodiscard]] static Image load_hdr(const fs::path &fn, ColorSpace color_space, float3 scale = make_float3(1.f));
    [[nodiscard]] static Image load_exr(const fs::path &fn, ColorSpace color_space, float3 scale = make_float3(1.f));
    [[nodiscard]] static Image create_empty(PixelStorage pixel_format, uint2 res);
    template<typename T>
    static Image from_data(T *data, uint2 res) {
        size_t size_in_bytes = sizeof(T) * res.x * res.y;
        auto pixel = allocate(size_in_bytes);
        auto pixel_format = PixelStorageImpl<T>::storage;
        oc_memcpy(pixel, data, size_in_bytes);
        return {pixel_format, pixel, res};
    }
    /**
     * ".bmp" or ".png" or ".tga" or ".jpg" or ".jpeg"
     */
    static Image load_other(const fs::path &fn, ColorSpace color_space,
                            float3 scale = make_float3(1.f));

    void clear() noexcept { pixel_.reset(); }

    template<typename Func>
    void for_each_pixel(Func func) const {
        auto p = pixel_.get();
        int stride = pixel_size(pixel_storage_);
        for (int i = 0; i < pixel_num(); ++i) {
            const std::byte *pixel = p + stride * i;
            func(pixel, i, pixel_storage_);
        }
    }

    template<typename Func>
    void for_each_pixel(Func func) {
        auto p = pixel_.get();
        int stride = pixel_size(pixel_storage_);
        for (int i = 0; i < pixel_num(); ++i) {
            std::byte *pixel = const_cast<std::byte *>(p + stride * i);
            func(pixel, i, pixel_storage_);
        }
    }

    void save(const fs::path &fn) const;
    void convert_to_8bit_image();
    void convert_to_32bit_image();
    static std::pair<PixelStorage, const std::byte *> convert_to_32bit(PixelStorage pixel_format,
                                                                       const std::byte *ptr, uint2 res);
    static std::pair<PixelStorage, const std::byte *> convert_to_8bit(PixelStorage pixel_format,
                                                                      const std::byte *ptr, uint2 res);
    static void save_image(const fs::path &fn, PixelStorage pixel_format,
                           uint2 res, const void *ptr);
    static void save_exr(const fs::path &fn, PixelStorage pixel_format,
                         uint2 res, const std::byte *ptr);
    static void save_hdr(const fs::path &fn, PixelStorage pixel_format,
                         uint2 res, const std::byte *ptr);
    static void save_other(const fs::path &fn, PixelStorage pixel_format,
                           uint2 res, const std::byte *ptr);
};
}// namespace ocarina