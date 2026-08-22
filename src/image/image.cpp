//
// Created by Zero on 26/07/2022.
//
#include "image.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION

#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "stb_image_write.h"

#define TINYEXR_IMPLEMENTATION

#include <limits>
#include "math/basic_types.h"
#include <tinyexr.h>
#include "core/util/logging.h"

namespace horizon::image {

using namespace horizon::core;
using namespace horizon::math;

namespace {

template<typename T>
[[nodiscard]] T lerp(float t, const T &a, const T &b) noexcept {
    return a + t * (b - a);
}

[[nodiscard]] float srgb_to_linear(float value) {
    if (value < 0.04045f) {
        return value / 12.92f;
    }
    return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] float4 srgb_to_linear(const float4 &value) {
    return make_float4(srgb_to_linear(value.x),
                       srgb_to_linear(value.y),
                       srgb_to_linear(value.z),
                       srgb_to_linear(value.w));
}

[[nodiscard]] uint32_t make_8bit(float value) {
    return static_cast<uint32_t>(std::clamp(static_cast<int>(value * 256.0f), 0, 255));
}

[[nodiscard]] uint32_t make_rgba(const float4 &color) {
    return (make_8bit(color.x) << 0u) |
           (make_8bit(color.y) << 8u) |
           (make_8bit(color.z) << 16u) |
           (make_8bit(color.w) << 24u);
}

[[nodiscard]] bool is_8bit_extension(const std::string &extension) {
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
           extension == ".bmp" || extension == ".tga";
}

[[noreturn]] void throw_unsupported_extension(const fs::path &path) {
    throw std::runtime_error("Unsupported image extension '" +
                             path.extension().string() + "' for path: " + path.string());
}

struct ExrHeaderGuard {
    EXRHeader *header;
    ~ExrHeaderGuard() { FreeEXRHeader(header); }
};

struct ExrImageGuard {
    EXRImage *image;
    ~ExrImageGuard() { FreeEXRImage(image); }
};

[[noreturn]] void throw_exr_error(const std::string &operation,
                                  const fs::path &path, const char *error) {
    const std::string reason = error == nullptr ? "unknown TinyEXR error" : error;
    FreeEXRErrorMessage(error);
    throw std::runtime_error(operation + " '" + path.string() + "': " + reason);
}

[[nodiscard]] int find_exr_channel(const EXRHeader &header, const char *name) {
    for (int channel = 0; channel < header.num_channels; ++channel) {
        if (std::strcmp(header.channels[channel].name, name) == 0) {
            return channel;
        }
    }
    return -1;
}

void validate_save_input(const fs::path &path, PixelStorage pixel_storage,
                         uint2 resolution, const void *pixels) {
    if (pixels == nullptr) {
        throw std::runtime_error("Invalid null pixel data for image: " + path.string());
    }
    if (!is_8bit(pixel_storage) && !is_32bit(pixel_storage)) {
        throw std::runtime_error(
            "Invalid pixel storage for image '" + path.string() + "': " +
            std::to_string(static_cast<uint32_t>(pixel_storage)));
    }
    if (resolution.x == 0u || resolution.y == 0u ||
        resolution.x > static_cast<uint>(std::numeric_limits<int>::max()) ||
        resolution.y > static_cast<uint>(std::numeric_limits<int>::max()) ||
        static_cast<uint64_t>(resolution.x) * resolution.y >
            static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Invalid image resolution for '" + path.string() +
                                 "': " + std::to_string(resolution.x) + "x" +
                                 std::to_string(resolution.y));
    }
}

void validate_storage_class(const fs::path &path, PixelStorage pixel_storage,
                            bool require_32bit) {
    const bool valid = require_32bit ? is_32bit(pixel_storage) : is_8bit(pixel_storage);
    if (!valid) {
        throw std::runtime_error(
            "Invalid pixel storage for image '" + path.string() + "': " +
            std::to_string(static_cast<uint32_t>(pixel_storage)));
    }
}

}// namespace

ImageView::ImageView(horizon::core::PixelStorage pixel_storage, const std::byte *pixel, horizon::math::uint2 res)
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
    : ImageBase(horizon::core::move(other)) {
    pixel_ = horizon::core::move(other.pixel_);
    path_ = horizon::core::move(other.path_);
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

Image Image::create_empty(horizon::core::PixelStorage pixel_format, horizon::math::uint2 res) {
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
    } else if (is_8bit_extension(extension)) {
        ret = load_other(path, color_space, scale);
    } else {
        throw_unsupported_extension(path);
    }
    ret.path_ = path;
    return ret;
}

Image Image::load_hdr(const fs::path &path, ColorSpace color_space, float3 scale) {
    int w, h;
    int comp;
    auto path_str = fs::absolute(path).string();
    float *rgb = stbi_loadf(path_str.c_str(), &w, &h, &comp, 3);
    if (rgb == nullptr) {
        const char *reason = stbi_failure_reason();
        throw std::runtime_error("Failed to load HDR image '" + path.string() +
                                 "': " + (reason == nullptr ? "unknown STB error" : reason));
    }
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
    stbi_image_free(rgb);
    Image ret = {pixel_storage, pixel, make_uint2(w, h), path};
    ret.average<4>() = average;
    return ret;
}

Image Image::load_exr(const fs::path &fn, ColorSpace color_space, float3 scale) {
    EXRVersion exr_version{};
    auto path_str = fs::absolute(fn).string();
    if (auto ret = ParseEXRVersionFromFile(&exr_version, path_str.c_str()); ret != 0) {
        throw std::runtime_error("Failed to parse OpenEXR version for file: " + fn.string());
    }

    if (exr_version.multipart) {
        throw std::runtime_error("Multipart OpenEXR format is not supported in file: " + fn.string());
    }

    EXRHeader exr_header;
    InitEXRHeader(&exr_header);
    ExrHeaderGuard header_guard{&exr_header};
    const char *err = nullptr;
    if (auto ret = ParseEXRHeaderFromFile(&exr_header, &exr_version, path_str.c_str(), &err); ret != 0) {
        throw_exr_error("Failed to parse EXR image", fn, err);
    }
    auto predict = [](const EXRChannelInfo &channel) noexcept {
        return channel.pixel_type != TINYEXR_PIXELTYPE_FLOAT &&
               channel.pixel_type != TINYEXR_PIXELTYPE_HALF;
    };
    if (exr_header.num_channels < 1 || exr_header.num_channels > 4 || exr_header.tiled ||
        std::any_of(exr_header.channels, exr_header.channels + exr_header.num_channels, predict)) {
        throw std::runtime_error("Unsupported EXR pixel format in file: " + fn.string());
    }

    for (int i = 0; i < exr_header.num_channels; i++) {
        if (exr_header.pixel_types[i] == TINYEXR_PIXELTYPE_HALF) {
            exr_header.requested_pixel_types[i] = TINYEXR_PIXELTYPE_FLOAT;
        }
    }

    EXRImage exr_image;
    InitEXRImage(&exr_image);
    ExrImageGuard image_guard{&exr_image};
    if (auto ret = LoadEXRImageFromFile(&exr_image, &exr_header, path_str.c_str(), &err); ret != 0) {
        throw_exr_error("Failed to load EXR image", fn, err);
    }

    size_t pixel_num = exr_image.width * exr_image.height;
    uint2 resolution = make_uint2(exr_image.width, exr_image.height);
    auto channel_value = [&](int channel, size_t pixel_index) {
        return reinterpret_cast<const float *>(exr_image.images[channel])[pixel_index];
    };
    auto convert_color = [&](float value, float channel_scale) {
        return (color_space == SRGB ? srgb_to_linear(value) : value) * channel_scale;
    };

    switch (exr_image.num_channels) {
        case 1: {
            using PixelType = float;
            PixelStorage pixel_storage = detail::PixelStorageImpl<PixelType>::storage;
            auto storage = new_array<std::byte>(pixel_num * sizeof(PixelType));
            auto pixel = reinterpret_cast<PixelType *>(storage);
            float average = 0;
            for (size_t i = 0; i < pixel_num; ++i) {
                pixel[i] = convert_color(channel_value(0, i), scale.x);
                average = lerp(1.f / static_cast<float>(i + 1), average, pixel[i]);
            }
            auto ret = Image(pixel_storage, storage, resolution, fn);
            ret.average<1>() = average;
            return ret;
        }
        case 2: {
            using PixelType = float2;
            PixelStorage pixel_storage = detail::PixelStorageImpl<PixelType>::storage;
            auto storage = new_array<std::byte>(pixel_num * sizeof(PixelType));
            auto pixel = reinterpret_cast<PixelType *>(storage);
            float2 average = make_float2(0.f);
            int first = find_exr_channel(exr_header, "R");
            int second = find_exr_channel(exr_header, "G");
            if (first < 0 || second < 0) {
                first = 1;
                second = 0;
            }
            for (size_t i = 0; i < pixel_num; ++i) {
                pixel[i] = make_float2(convert_color(channel_value(first, i), scale.x),
                                       convert_color(channel_value(second, i), scale.y));
                average = lerp(1.f / static_cast<float>(i + 1), average, pixel[i]);
            }
            auto ret = Image(pixel_storage, storage, resolution, fn);
            ret.average<2>() = average;
            return ret;
        }
        case 3: {
            PixelStorage pixel_storage = detail::PixelStorageImpl<float4>::storage;
            auto storage = new_array<std::byte>(pixel_num * sizeof(float4));
            auto pixel = reinterpret_cast<float4 *>(storage);
            float4 average = make_float4(0.f);
            int red = find_exr_channel(exr_header, "R");
            int green = find_exr_channel(exr_header, "G");
            int blue = find_exr_channel(exr_header, "B");
            if (red < 0 || green < 0 || blue < 0) {
                red = 2;
                green = 1;
                blue = 0;
            }
            for (size_t i = 0; i < pixel_num; ++i) {
                pixel[i] = make_float4(convert_color(channel_value(red, i), scale.x),
                                       convert_color(channel_value(green, i), scale.y),
                                       convert_color(channel_value(blue, i), scale.z), 1.f);
                average = lerp(1.f / static_cast<float>(i + 1), average, pixel[i]);
            }
            auto ret = Image(pixel_storage, storage, resolution, fn);
            ret.average<4>() = average;
            return ret;
        }
        case 4: {
            PixelStorage pixel_storage = detail::PixelStorageImpl<float4>::storage;
            auto storage = new_array<std::byte>(pixel_num * sizeof(float4));
            auto pixel = reinterpret_cast<float4 *>(storage);
            float4 average = make_float4(0.f);
            int red = find_exr_channel(exr_header, "R");
            int green = find_exr_channel(exr_header, "G");
            int blue = find_exr_channel(exr_header, "B");
            int alpha = find_exr_channel(exr_header, "A");
            if (red < 0 || green < 0 || blue < 0 || alpha < 0) {
                red = 3;
                green = 2;
                blue = 1;
                alpha = 0;
            }
            for (size_t i = 0; i < pixel_num; ++i) {
                pixel[i] = make_float4(convert_color(channel_value(red, i), scale.x),
                                       convert_color(channel_value(green, i), scale.y),
                                       convert_color(channel_value(blue, i), scale.z),
                                       channel_value(alpha, i));
                average = lerp(1.f / static_cast<float>(i + 1), average, pixel[i]);
            }
            auto ret = Image(pixel_storage, storage, resolution, fn);
            ret.average<4>() = average;
            return ret;
        }
        default:
            throw std::runtime_error("Unsupported EXR channel count in file: " + fn.string());
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
    stbi_image_free(rgba);
    Image ret = {pixel_storage, pixel, resolution, path};
    ret.average<4>() = average;
    return ret;
}

void Image::save(const fs::path &fn) const {
    save_image(fn, pixel_storage_, resolution(), pixel_.get());
}

void Image::save_exr(const fs::path &fn, PixelStorage pixel_storage,
                     uint2 res, const std::byte *ptr) {
    validate_save_input(fn, pixel_storage, res, ptr);
    validate_storage_class(fn, pixel_storage, true);
    const int c = static_cast<int>(::horizon::image::channel_num(pixel_storage));
    EXRHeader header;
    InitEXRHeader(&header);

    EXRImage image;
    InitEXRImage(&image);
    int count = res.x * res.y;
    horizon::core::array<float *, 4> image_ptr{nullptr, nullptr, nullptr, nullptr};
    image.num_channels = c;
    image.width = res.x;
    image.height = res.y;
    image.images = reinterpret_cast<uint8_t **>(image_ptr.data());

    horizon::core::array<int, 4> pixel_types{TINYEXR_PIXELTYPE_FLOAT, TINYEXR_PIXELTYPE_FLOAT, TINYEXR_PIXELTYPE_FLOAT,
                                       TINYEXR_PIXELTYPE_FLOAT};
    horizon::core::array<EXRChannelInfo, 4> channels{};
    header.num_channels = c;
    header.channels = channels.data();
    header.pixel_types = pixel_types.data();
    header.requested_pixel_types = pixel_types.data();

    std::vector<float> images;
    images.resize(c * count);
    for (int channel = 0; channel < c; ++channel) {
        image_ptr[channel] = images.data() + channel * count;
    }

    const auto *source = reinterpret_cast<const float *>(ptr);
    for (int pixel_index = 0; pixel_index < count; ++pixel_index) {
        for (int source_channel = 0; source_channel < c; ++source_channel) {
            const int exr_channel = c - source_channel - 1;
            image_ptr[exr_channel][pixel_index] = source[pixel_index * c + source_channel];
        }
    }

    static constexpr const char *channel_names[4][4]{
        {"Y", nullptr, nullptr, nullptr},
        {"G", "R", nullptr, nullptr},
        {"B", "G", "R", nullptr},
        {"A", "B", "G", "R"},
    };
    for (int channel = 0; channel < c; ++channel) {
        strcpy_s(header.channels[channel].name,
                 sizeof(header.channels[channel].name),
                 channel_names[c - 1][channel]);
    }
    const char *err = nullptr;
    if (auto ret = SaveEXRImageToFile(&image, &header, fn.string().c_str(), &err); ret != TINYEXR_SUCCESS) {
        const std::string reason = err == nullptr ? "unknown TinyEXR error" : err;
        FreeEXRErrorMessage(err);
        throw std::runtime_error("Failed to save EXR image '" + fn.string() + "': " + reason);
    }
}

void Image::save_hdr(const fs::path &fn, PixelStorage pixel_storage,
                     uint2 res, const std::byte *ptr) {
    validate_save_input(fn, pixel_storage, res, ptr);
    validate_storage_class(fn, pixel_storage, true);
    auto path_str = fs::absolute(fn).string();
    const int components = static_cast<int>(::horizon::image::channel_num(pixel_storage));
    if (stbi_write_hdr(path_str.c_str(), res.x, res.y, components,
                       reinterpret_cast<const float *>(ptr)) == 0) {
        throw std::runtime_error("Failed to save HDR image: " + fn.string());
    }
}

void Image::save_other(const fs::path &fn, PixelStorage pixel_storage,
                       uint2 res, const std::byte *ptr) {
    validate_save_input(fn, pixel_storage, res, ptr);
    validate_storage_class(fn, pixel_storage, false);
    auto path_str = fs::absolute(fn).string();
    auto extension = to_lower(fn.extension().string());
    const int components = static_cast<int>(::horizon::image::channel_num(pixel_storage));
    int result = 0;
    if (extension == ".png") {
        result = stbi_write_png(path_str.c_str(), res.x, res.y, components, ptr,
                                res.x * components);
    } else if (extension == ".bmp") {
        result = stbi_write_bmp(path_str.c_str(), res.x, res.y, components, ptr);
    } else if (extension == ".tga") {
        result = stbi_write_tga(path_str.c_str(), res.x, res.y, components, ptr);
    } else {
        result = stbi_write_jpg(path_str.c_str(), res.x, res.y, components, ptr, 100);
    }
    if (result == 0) {
        throw std::runtime_error("Failed to save image '" + fn.string() +
                                 "' as " + extension);
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
    validate_save_input(fn, pixel_storage, res, raw_ptr);
    const std::byte *ptr = reinterpret_cast<const std::byte *>(raw_ptr);
    auto extension = to_lower(fn.extension().string());
    if (extension == ".exr") {
        if (is_32bit(pixel_storage)) {
            save_exr(fn, pixel_storage, res, ptr);
        } else {
            auto [format, pixel] = convert_to_32bit(pixel_storage, ptr, res);
            std::unique_ptr<const std::byte[]> converted{pixel};
            save_exr(fn, format, res, converted.get());
        }
    } else if (extension == ".hdr") {
        if (is_32bit(pixel_storage)) {
            save_hdr(fn, pixel_storage, res, ptr);
        } else {
            auto [format, pixel] = convert_to_32bit(pixel_storage, ptr, res);
            std::unique_ptr<const std::byte[]> converted{pixel};
            save_hdr(fn, format, res, converted.get());
        }
    } else if (is_8bit_extension(extension)) {
        if (is_8bit(pixel_storage)) {
            save_other(fn, pixel_storage, res, ptr);
        } else {
            auto [format, pixel] = convert_to_8bit(pixel_storage, ptr, res);
            std::unique_ptr<const std::byte[]> converted{pixel};
            save_other(fn, format, res, converted.get());
        }
    } else {
        throw_unsupported_extension(fn);
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
            pixel_storage = PixelStorage::FLOAT1;
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
            using TargetType = uint8_t;
            pixel = new_array(pixel_num * sizeof(TargetType) * 2u);
            auto dest = reinterpret_cast<TargetType *>(const_cast<std::byte *>(pixel));
            auto src = reinterpret_cast<const float *>(ptr);
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

}// namespace horizon::image
