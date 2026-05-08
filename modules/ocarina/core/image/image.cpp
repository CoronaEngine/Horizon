//
// Created by Zero on 26/07/2022.
//
#include "image.h"

#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image_write.h"

//#define TINYEXR_IMPLEMENTATION

#include <limits>
#include "math/base.h"
#include "ext/tinyexr/tinyexr.h"
#include "core/util/logging.h"

namespace ocarina {

ImageView::ImageView(ocarina::PixelStorage pixel_storage, const std::byte *pixel, ocarina::uint2 res)
    : ImageBase(pixel_storage, res), pixel_(pixel) {}

Image::Image(PixelStorage pixel_storage, const std::byte *pixel, uint2 res, const fs::path &path)
    : ImageBase(pixel_storage, res),
      path_(path) {
    pixel_.reset(pixel);
}

ImageView Image::view() const noexcept {
    return {pixel_storage(), pixel_.get(), resolution()};
}

Image::Image(PixelStorage pixel_storage, const std::byte *pixel, uint2 res)
    : ImageBase(pixel_storage, res) {
    pixel_.reset(pixel);
}

Image::Image(Image &&other) noexcept
    : ImageBase(ocarina::move(other)) {
    pixel_ = ocarina::move(other.pixel_);
    path_ = ocarina::move(other.path_);
}

Image &Image::operator=(Image &&rhs) noexcept {
    (*(ImageBase *)this) = std::forward<ImageBase>(rhs);
    std::swap(this->pixel_, rhs.pixel_);
    std::swap(this->path_, rhs.path_);
    return *this;
}

Image Image::pure_color(float4 color, ColorSpace color_space, uint2 res) {
    auto pixel_count = res.x * res.y;
    auto pixel_size = PixelStorageImpl<float4>::pixel_size * pixel_count;
    auto pixel = new_array<std::byte>(pixel_size);
    auto dest = (float4 *)pixel;
    if (color_space == ColorSpace::LINEAR) {
        for (auto i = 0; i < pixel_count; ++i) {
            dest[i] = color;
        }
    } else {
        for (auto i = 0; i < pixel_count; ++i) {
            dest[i] = srgb_to_linear(color);
        }
    }
    Image ret{PixelStorage::FLOAT4, pixel, res};
    ret.average<4>() = color;
    return ret;
}

Image Image::create_empty(ocarina::PixelStorage pixel_format, ocarina::uint2 res) {
    size_t size_in_bytes = pixel_size(pixel_format) * res.x * res.y;
    auto pixel = new_array<std::byte>(size_in_bytes);
    return {pixel_format, pixel, res};
}

Image Image::load(const fs::path &path, ColorSpace color_space, float3 scale) {
    auto extension = to_lower(path.extension().string());
    OC_INFO("load picture ", path.string());
    Image ret;
    if (extension == ".exr") {
        ret = load_exr(path, color_space, scale);
    } else if (extension == ".hdr") {
        ret = load_hdr(path, color_space, scale);
    } else {
        ret = load_other(path, color_space, scale);
    }
    ret.path_ = path;
    return ret;
}

Image Image::load_hdr(const fs::path &path, ColorSpace color_space, float3 scale) {
    int w, h;
    int comp;
    auto path_str = fs::absolute(path).string();
    float *rgb = stbi_loadf(path_str.c_str(), &w, &h, &comp, 3);
    int pixel_num = w * h;
    PixelStorage pixel_storage = detail::PixelStorageImpl<float4>::storage;
    int pixel_size = detail::PixelStorageImpl<float4>::pixel_size;
    size_t size_in_bytes = pixel_num * pixel_size;
    auto pixel = new_array(size_in_bytes);
    float *src = rgb;
    auto dest = (float *)pixel;
    float4 average = make_float4(0.f);
    if (color_space == SRGB) {
        for (int i = 0; i < pixel_num; ++i, src += 3, dest += 4) {
            dest[0] = srgb_to_linear(src[0]) * scale.x;
            dest[1] = srgb_to_linear(src[1]) * scale.y;
            dest[2] = srgb_to_linear(src[2]) * scale.z;
            dest[3] = 1.f;
            average = lerp(1.f / (i + 1), average, *reinterpret_cast<float4 *>(dest));
        }
    } else {
        for (int i = 0; i < pixel_num; ++i, src += 3, dest += 4) {
            dest[0] = src[0] * scale.x;
            dest[1] = src[1] * scale.y;
            dest[2] = src[2] * scale.z;
            dest[3] = 1.f;
            average = lerp(1.f / (i + 1), average, *reinterpret_cast<float4 *>(dest));
        }
    }
    free(rgb);
    Image ret = {pixel_storage, pixel, make_uint2(w, h), path};
    ret.average<4>() = average;
    return ret;
}

Image Image::load_exr(const fs::path &fn, ColorSpace color_space, float3 scale) {
    // Parse OpenEXR
    EXRVersion exr_version;
    auto path_str = fs::absolute(fn).string();
    if (auto ret = ParseEXRVersionFromFile(&exr_version, path_str.c_str()); ret != 0) {
        OC_EXCEPTION("Failed to parse OpenEXR version for file: ", fn.string());
    }

    if (exr_version.multipart) {
        OC_EXCEPTION("Multipart OpenEXR format is not supported in file: ", fn.string());
    }
    // 2. Read EXR header
    EXRHeader exr_header;
    InitEXRHeader(&exr_header);
    const char *err = nullptr;// or `nullptr` in C++11 or later.
    FreeEXRErrorMessage(err);
    if (auto ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, path_str.c_str(), &err); ret != 0) {
        OC_EXCEPTION("Failed to parse ", fn.string(), ": ", err);
    }
    auto predict = [](const EXRChannelInfo &channel) noexcept {
        return channel.pixel_type != TINYEXR_PIXELTYPE_FLOAT &&
               channel.pixel_type != TINYEXR_PIXELTYPE_HALF;
    };
    if (exr_header.num_channels > 4 || exr_header.tiled ||
        std::any_of(exr_header.channels, exr_header.channels + exr_header.num_channels, predict)) {
        OC_EXCEPTION("Unsupported pixel format in file: ", fn.string());
    }

    // Read HALF channel as FLOAT.
    for (int i = 0; i < exr_header.num_channels; i++) {
        if (exr_header.pixel_types[i] == TINYEXR_PIXELTYPE_HALF) {
            exr_header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
        }
    }

    EXRImage exr_image;
    InitEXRImage(&exr_image);
    if (auto ret = LoadEXRImageFromFile(&exr_image, &exr_header, path_str.c_str(), &err); ret != 0) {
        OC_EXCEPTION("Failed to load ", fn.string(), ": ", err);
    }
    size_t pixel_num = exr_image.width * exr_image.height;
    uint2 resolution = make_uint2(exr_image.width, exr_image.height);
    switch (exr_image.num_channels) {
        case 1: {
            using PixelType = float;
            PixelStorage pixel_storage = detail::PixelStorageImpl<PixelType>::storage;
            PixelType *pixel = new_array<PixelType>(pixel_num);
            float average = 0;
            if (color_space == SRGB) {
                for (int i = 0; i < pixel_num; ++i) {
                    PixelType val = reinterpret_cast<PixelType *>(exr_image.images[0])[i];
                    pixel[i] = srgb_to_linear(val) * scale.x;
                    average = lerp(1.f / (i + 1), average, pixel[i]);
                }
            } else {
                for (int i = 0; i < pixel_num; ++i) {
                    PixelType val = reinterpret_cast<PixelType *>(exr_image.images[0])[i];
                    pixel[i] = val * scale.x;
                    average = lerp(1.f / (i + 1), average, pixel[i]);
                }
            }
            auto ret = Image(pixel_storage, (std::byte *)pixel, resolution, fn);
            ret.average<1>() = average;
            return ret;
        }
        case 2: {
            using PixelType = float2;
            PixelStorage pixel_storage = detail::PixelStorageImpl<PixelType>::storage;
            PixelType *pixel = new_array<PixelType>(pixel_num);
            float2 average = make_float2(0.f);
            if (color_space == SRGB) {
                for (int i = 0; i < pixel_num; ++i) {
                    pixel[i] = make_float2(
                        srgb_to_linear(reinterpret_cast<float *>(exr_image.images[1])[i]) *
                            scale.x,
                        srgb_to_linear(reinterpret_cast<float *>(exr_image.images[0])[i]) *
                            scale.y);
                    average = lerp(1.f / (i + 1), average, pixel[i]);
                }
            } else {
                for (int i = 0; i < pixel_num; ++i) {
                    pixel[i] = make_float2(
                        reinterpret_cast<float *>(exr_image.images[1])[i] * scale.x,
                        reinterpret_cast<float *>(exr_image.images[0])[i] * scale.y);
                    average = lerp(1.f / (i + 1), average, pixel[i]);
                }
            }
            auto ret = Image(pixel_storage, (std::byte *)pixel, resolution, fn);
            ret.average<2>() = average;
            return ret;
        }
        case 3: {
            PixelStorage pixel_storage = detail::PixelStorageImpl<float4>::storage;
            float4 *pixel = new_array<float4>(pixel_num);
            float4 average = make_float4(0.f);
            if (color_space == SRGB) {
                for (int i = 0; i < pixel_num; ++i) {
                    pixel[i] = make_float4(
                                   srgb_to_linear(reinterpret_cast<float *>(exr_image.images[3])[i]),
                                   srgb_to_linear(reinterpret_cast<float *>(exr_image.images[2])[i]),
                                   srgb_to_linear(reinterpret_cast<float *>(exr_image.images[1])[i]),
                                   1.f) *
                               make_float4(scale, 1.f);
                    average = lerp(1.f / (i + 1), average, pixel[i]);
                }
            } else {
                for (int i = 0; i < pixel_num; ++i) {
                    pixel[i] = make_float4(
                                   (reinterpret_cast<float *>(exr_image.images[3])[i]),
                                   (reinterpret_cast<float *>(exr_image.images[2])[i]),
                                   (reinterpret_cast<float *>(exr_image.images[1])[i]),
                                   1.f) *
                               make_float4(scale, 1.f);
                    average = lerp(1.f / (i + 1), average, pixel[i]);
                }
            }
            auto ret = Image(pixel_storage, (std::byte *)pixel, resolution, fn);
            ret.average<4>() = average;
            return ret;
        }
        case 4: {
            PixelStorage pixel_storage = detail::PixelStorageImpl<float4>::storage;
            float4 *pixel = new_array<float4>(pixel_num);
            float4 average = make_float4(0.f);
            if (color_space == SRGB) {
                for (int i = 0; i < pixel_num; ++i) {
                    pixel[i] = make_float4(
                                   srgb_to_linear(reinterpret_cast<float *>(exr_image.images[3])[i]),
                                   srgb_to_linear(reinterpret_cast<float *>(exr_image.images[2])[i]),
                                   srgb_to_linear(reinterpret_cast<float *>(exr_image.images[1])[i]),
                                   srgb_to_linear(reinterpret_cast<float *>(exr_image.images[0])[i])) *
                               make_float4(scale, 1.f);
                    average = lerp(1.f / (i + 1), average, pixel[i]);
                }
            } else {
                for (int i = 0; i < pixel_num; ++i) {
                    pixel[i] = make_float4(
                                   (reinterpret_cast<float *>(exr_image.images[3])[i]),
                                   (reinterpret_cast<float *>(exr_image.images[2])[i]),
                                   (reinterpret_cast<float *>(exr_image.images[1])[i]),
                                   (reinterpret_cast<float *>(exr_image.images[0])[i])) *
                               make_float4(scale, 1.f);
                    average = lerp(1.f / (i + 1), average, pixel[i]);
                }
            }
            auto ret = Image(pixel_storage, (std::byte *)pixel, resolution, fn);
            ret.average<4>() = average;
            return ret;
        }
        default:
            OC_ERROR("unknown");
    }
}

Image Image::load_other(const fs::path &path, ColorSpace color_space, float3 scale) {
    uint8_t *rgba;
    int w, h;
    int channel;
    auto fn = path.string();
    rgba = stbi_load(fn.c_str(), &w, &h, &channel, 4);
    if (!rgba) {
        throw std::runtime_error(fn + " load fail");
    }
    PixelStorage pixel_storage = detail::PixelStorageImpl<uchar4>::storage;
    int pixel_size = detail::PixelStorageImpl<uchar4>::pixel_size;
    size_t pixel_num = w * h;
    size_t size_in_bytes = pixel_size * pixel_num;
    uint2 resolution = make_uint2(w, h);
    auto pixel = new_array<std::byte>(size_in_bytes);
    uint8_t *src = rgba;
    auto dest = (uint32_t *)pixel;
    float4 average = make_float4(0.f);
    if (color_space == SRGB) {
        for (int i = 0; i < pixel_num; ++i, src += 4, dest += 1) {
            float r = (float)src[0] / 255;
            float g = (float)src[1] / 255;
            float b = (float)src[2] / 255;
            float a = (float)src[3] / 255;
            float4 color = make_float4(r, g, b, a) * make_float4(scale, 1.f);
            color = srgb_to_linear(color);
            average = lerp(1.f / (i + 1), average, color);
            *dest = make_rgba(color);
        }
    } else {
        for (int i = 0; i < pixel_num; ++i, src += 4, dest += 1) {
            float r = (float)src[0] / 255;
            float g = (float)src[1] / 255;
            float b = (float)src[2] / 255;
            float a = (float)src[3] / 255;
            float4 color = make_float4(r, g, b, a) * make_float4(scale, 1.f);
            average = lerp(1.f / (i + 1), average, color);
            *dest = make_rgba(color);
        }
    }
    free(rgba);
    Image ret = {pixel_storage, pixel, resolution, path};
    ret.average<4>() = average;
    return ret;
}

void Image::save(const fs::path &fn) const {
    save_image(fn, pixel_storage_, resolution(), pixel_.get());
}

void Image::save_exr(const fs::path &fn, PixelStorage pixel_storage,
                     uint2 res, const std::byte *ptr) {
    OC_ASSERT(is_32bit(pixel_storage));
    EXRHeader header;
    InitEXRHeader(&header);

    EXRImage image;
    int c = 4;
    InitEXRImage(&image);
    int count = res.x * res.y;
    ocarina::array<float *, 4> image_ptr{nullptr, nullptr, nullptr, nullptr};
    image.num_channels = 4;
    image.width = res.x;
    image.height = res.y;
    image.images = reinterpret_cast<uint8_t **>(image_ptr.data());

    ocarina::array<int, 4> pixel_types{TINYEXR_PIXELTYPE_FLOAT, TINYEXR_PIXELTYPE_FLOAT, TINYEXR_PIXELTYPE_FLOAT,
                                       TINYEXR_PIXELTYPE_FLOAT};
    ocarina::array<EXRChannelInfo, 4> channels{};
    header.num_channels = c;
    header.channels = channels.data();
    header.pixel_types = pixel_types.data();
    header.requested_pixel_types = pixel_types.data();

    std::vector<float> images;
    images.resize(c * count);
    image_ptr[0] = images.data();
    image_ptr[1] = image_ptr[0] + count;
    image_ptr[2] = image_ptr[1] + count;
    image_ptr[3] = image_ptr[2] + count;
    auto rgba = reinterpret_cast<const float4 *>(ptr);
    for (int i = 0u; i < count; i++) {
        image_ptr[3][i] = rgba[i].x;
        image_ptr[2][i] = rgba[i].y;
        image_ptr[1][i] = rgba[i].z;
        image_ptr[0][i] = rgba[i].w;
    }
    strcpy_s(header.channels[0].name, sizeof(header.channels[0].name), "A");
    strcpy_s(header.channels[1].name, sizeof(header.channels[1].name), "B");
    strcpy_s(header.channels[2].name, sizeof(header.channels[2].name), "G");
    strcpy_s(header.channels[3].name, sizeof(header.channels[3].name), "R");
    const char *err = nullptr;
    if (auto ret = SaveEXRImageToFile(&image, &header, fn.string().c_str(), &err); ret != TINYEXR_SUCCESS) {
        OC_EXCEPTION_IF("Failed to save texture as OpenEXR image: ", fn.string());
    }
}

void Image::save_hdr(const fs::path &fn, PixelStorage pixel_storage,
                     uint2 res, const std::byte *ptr) {
    OC_ASSERT(is_32bit(pixel_storage));
    auto path_str = fs::absolute(fn).string();
    stbi_write_hdr(path_str.c_str(), res.x, res.y, 4,
                   reinterpret_cast<const float *>(ptr));
}

void Image::save_other(const fs::path &fn, PixelStorage pixel_storage,
                       uint2 res, const std::byte *ptr) {
    OC_ASSERT(is_8bit(pixel_storage));
    auto path_str = fs::absolute(fn).string();
    auto extension = to_lower(fn.extension().string());
    if (extension == ".png") {
        stbi_write_png(path_str.c_str(), res.x, res.y, 4, ptr, 0);
    } else if (extension == ".bmp") {
        stbi_write_bmp(path_str.c_str(), res.x, res.y, 4, ptr);
    } else if (extension == ".tga") {
        stbi_write_tga(path_str.c_str(), res.x, res.y, 4, ptr);
    } else {
        // jpg or jpeg
        stbi_write_jpg(path_str.c_str(), res.x, res.y, 4, ptr, 100);
    }
}

void Image::convert_to_8bit_image() {
    if (is_8bit(pixel_storage())) {
        return;
    }
    auto [new_format, ptr] = convert_to_8bit(pixel_storage(), pixel_.get(), resolution());
    pixel_storage_ = new_format;
    pixel_.reset(ptr);
}

void Image::convert_to_32bit_image() {
    if (is_32bit(pixel_storage())) {
        return;
    }
    auto [new_format, ptr] = convert_to_32bit(pixel_storage(), pixel_.get(), resolution());
    pixel_storage_ = new_format;
    pixel_.reset(ptr);
}

void Image::save_image(const fs::path &fn, PixelStorage pixel_storage,
                       uint2 res, const void *raw_ptr) {
    OC_ASSERT(raw_ptr != nullptr);
    const std::byte *ptr = reinterpret_cast<const std::byte *>(raw_ptr);
    auto extension = to_lower(fn.extension().string());
    if (extension == ".exr") {
        if (is_32bit(pixel_storage)) {
            save_exr(fn, pixel_storage, res, ptr);
        } else {
            auto [format, pixel] = convert_to_32bit(pixel_storage, ptr, res);
            save_exr(fn, format, res, pixel);
            delete_array(pixel);
        }
    } else if (extension == ".hdr") {
        if (is_32bit(pixel_storage)) {
            save_hdr(fn, pixel_storage, res, ptr);
        } else {
            auto [format, pixel] = convert_to_32bit(pixel_storage, ptr, res);
            save_hdr(fn, format, res, pixel);
            delete_array(pixel);
        }
    } else {
        if (is_8bit(pixel_storage)) {
            save_other(fn, pixel_storage, res, ptr);
        } else {
            auto [format, pixel] = convert_to_8bit(pixel_storage, ptr, res);
            save_other(fn, format, res, pixel);
            delete_array(pixel);
        }
    }
    OC_INFO("save picture ", fn);
}

std::pair<PixelStorage, const std::byte *>
Image::convert_to_32bit(PixelStorage pixel_storage, const std::byte *ptr, uint2 res) {
    OC_ASSERT(is_8bit(pixel_storage));
    uint pixel_num = res.x * res.y;
    const std::byte *pixel = nullptr;
    switch (pixel_storage) {
        case PixelStorage::BYTE1: {
            using TargetType = float;
            pixel = new_array<std::byte>(pixel_num * sizeof(TargetType));
            auto src = (uint8_t *)ptr;
            auto dest = (TargetType *)pixel;
            for (int i = 0; i < pixel_num; ++i, ++dest) {
                *dest = float(src[i]) / 255.f;
            }
            pixel_storage = PixelStorage::BYTE1;
            break;
        }
        case PixelStorage::BYTE2: {
            using TargetType = float2;
            pixel = new_array(pixel_num * sizeof(TargetType));
            auto src = (uint8_t *)ptr;
            auto dest = (TargetType *)pixel;
            for (int i = 0; i < pixel_num; ++i, ++dest, src += 2) {
                *dest = make_float2(float(src[0]) / 255.f, float(src[1]) / 255.f);
            }
            pixel_storage = PixelStorage::FLOAT2;
            break;
        }
        case PixelStorage::BYTE4: {
            using TargetType = float4;
            pixel = new_array(pixel_num * sizeof(TargetType));
            auto src = (uint8_t *)ptr;
            auto dest = (TargetType *)pixel;
            for (int i = 0; i < pixel_num; ++i, ++dest, src += 4) {
                *dest = make_float4(float(src[0]) / 255.f,
                                    float(src[1]) / 255.f,
                                    float(src[2]) / 255.f,
                                    float(src[3]) / 255.f);
            }
            pixel_storage = PixelStorage::FLOAT4;
            break;
        }
        default:
            OC_EXCEPTION("unknown pixel type");
    }
    return {pixel_storage, pixel};
}

std::pair<PixelStorage, const std::byte *>
Image::convert_to_8bit(PixelStorage pixel_storage, const std::byte *ptr, uint2 res) {
    OC_ASSERT(is_32bit(pixel_storage));
    uint pixel_num = res.x * res.y;
    const std::byte *pixel = nullptr;
    switch (pixel_storage) {
        case PixelStorage::FLOAT1: {
            using TargetType = uint8_t;
            pixel = new_array(pixel_num * sizeof(TargetType));
            auto dest = (TargetType *)pixel;
            auto src = (float *)ptr;
            for (int i = 0; i < pixel_num; ++i, ++dest, ++src) {
                *dest = make_8bit(src[0]);
            }
            pixel_storage = PixelStorage::BYTE1;
            break;
        }
        case PixelStorage::FLOAT2: {
            using TargetType = uint16_t;
            pixel = new_array(pixel_num * sizeof(TargetType));
            auto dest = (TargetType *)pixel;
            auto src = (float *)pixel;
            for (int i = 0; i < pixel_num; ++i, dest += 2, src += 2) {
                dest[0] = make_8bit(src[0]);
                dest[1] = make_8bit(src[1]);
            }
            pixel_storage = PixelStorage::BYTE2;
            break;
        }
        case PixelStorage::FLOAT4: {
            using TargetType = uint32_t;
            pixel = new_array(pixel_num * sizeof(TargetType));
            auto dest = (TargetType *)pixel;
            auto src = (float4 *)ptr;
            for (int i = 0; i < pixel_num; ++i, ++dest, ++src) {
                *dest = make_rgba(*src);
            }
            pixel_storage = PixelStorage::BYTE4;
            break;
        }
        default:
            break;
    }
    return {pixel_storage, pixel};
}

}// namespace ocarina