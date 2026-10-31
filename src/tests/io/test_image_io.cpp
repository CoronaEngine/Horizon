#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <system_error>

#include "core/image/image.h"
#include "core/util/logging.h"
#include "core/stl.h"
#include "math/base.h"

using namespace ocarina;

namespace {

struct TempDirectory {
    fs::path path;

    TempDirectory() {
        auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() / ("ocarina_test_image_io_" + std::to_string(stamp));
        fs::create_directories(path);
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

[[nodiscard]] size_t pixel_index(uint2 res, uint x, uint y) {
    return static_cast<size_t>(y) * res.x + x;
}

[[nodiscard]] bool close_float(float lhs, float rhs, float eps) {
    return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] bool equal_float4(float4 lhs, float4 rhs, float eps, bool compare_alpha = true) {
    return close_float(lhs.x, rhs.x, eps) &&
           close_float(lhs.y, rhs.y, eps) &&
           close_float(lhs.z, rhs.z, eps) &&
           (!compare_alpha || close_float(lhs.w, rhs.w, eps));
}

[[nodiscard]] bool equal_uchar4(uchar4 lhs, uchar4 rhs, uint tolerance, bool compare_alpha = true) {
    auto close_channel = [tolerance](uchar lhs_channel, uchar rhs_channel) noexcept {
        return std::abs(int(lhs_channel) - int(rhs_channel)) <= int(tolerance);
    };
    return close_channel(lhs.x, rhs.x) &&
           close_channel(lhs.y, rhs.y) &&
           close_channel(lhs.z, rhs.z) &&
           (!compare_alpha || close_channel(lhs.w, rhs.w));
}

[[nodiscard]] uchar4 quantize_to_uchar4(float4 value) {
    uint32_t rgba = make_rgba(value);
    return make_uchar4(static_cast<uchar>(rgba & 0xffu),
                       static_cast<uchar>((rgba >> 8u) & 0xffu),
                       static_cast<uchar>((rgba >> 16u) & 0xffu),
                       static_cast<uchar>((rgba >> 24u) & 0xffu));
}

[[nodiscard]] float4 normalize_uchar4(uchar4 value) {
    constexpr float inv_255 = 1.f / 255.f;
    return make_float4(static_cast<float>(value.x) * inv_255,
                       static_cast<float>(value.y) * inv_255,
                       static_cast<float>(value.z) * inv_255,
                       static_cast<float>(value.w) * inv_255);
}

template<typename T>
[[nodiscard]] Image make_owned_image(const vector<T> &pixels, uint2 res) {
    auto *owned_pixels = new_array<T>(pixels.size());
    std::copy(pixels.begin(), pixels.end(), owned_pixels);
    return {PixelStorageImpl<T>::storage,
            reinterpret_cast<const std::byte *>(owned_pixels),
            res};
}

[[nodiscard]] vector<uchar4> make_ldr_pixels(uint2 res) {
    vector<uchar4> pixels(res.x * res.y);
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            auto idx = pixel_index(res, x, y);
            uchar r = static_cast<uchar>((x * 31u + y * 17u + 13u) % 256u);
            uchar g = static_cast<uchar>((x * 7u + y * 29u + 101u) % 256u);
            uchar b = static_cast<uchar>((x * 19u + y * 11u + 53u) % 256u);
            pixels[idx] = make_uchar4(r, g, b, static_cast<uchar>(255u));
        }
    }
    return pixels;
}

[[nodiscard]] vector<float4> make_hdr_pixels(uint2 res) {
    vector<float4> pixels(res.x * res.y);
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            auto idx = pixel_index(res, x, y);
            float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(res.x);
            float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(res.y);
            pixels[idx] = make_float4(0.05f + 0.7f * u,
                                      0.1f + 0.6f * v,
                                      0.15f + 0.35f * (0.75f * u + 0.25f * v),
                                      0.2f + 0.7f * (0.4f * u + 0.6f * v));
        }
    }
    return pixels;
}

void log_storage_mismatch(const char *label, PixelStorage actual, PixelStorage expected) {
    std::cerr << "  FAIL [" << label << "] pixel storage mismatch"
              << " expected=" << static_cast<uint>(expected)
              << " actual=" << static_cast<uint>(actual)
              << std::endl;
}

void log_resolution_mismatch(const char *label, uint2 actual, uint2 expected) {
    std::cerr << "  FAIL [" << label << "] resolution mismatch"
              << " expected=(" << expected.x << ", " << expected.y << ")"
              << " actual=(" << actual.x << ", " << actual.y << ")"
              << std::endl;
}

void log_uchar_mismatch(const char *label, uint x, uint y, uchar4 actual, uchar4 expected) {
    std::cerr << "  FAIL [" << label << "] at (" << x << ", " << y << ")"
              << " expected=(" << uint(expected.x) << ", " << uint(expected.y) << ", " << uint(expected.z) << ", " << uint(expected.w) << ")"
              << " actual=(" << uint(actual.x) << ", " << uint(actual.y) << ", " << uint(actual.z) << ", " << uint(actual.w) << ")"
              << std::endl;
}

void log_float_mismatch(const char *label, uint x, uint y, float4 actual, float4 expected) {
    std::cerr << "  FAIL [" << label << "] at (" << x << ", " << y << ")"
              << " expected=(" << expected.x << ", " << expected.y << ", " << expected.z << ", " << expected.w << ")"
              << " actual=(" << actual.x << ", " << actual.y << ", " << actual.z << ", " << actual.w << ")"
              << std::endl;
}

int verify_loaded_byte_image(const char *label,
                             const Image &image,
                             uint2 expected_res,
                             const vector<uchar4> &expected_pixels,
                             uint tolerance,
                             bool compare_alpha = true) {
    int failures = 0;
    if (image.pixel_storage() != PixelStorage::BYTE4) {
        log_storage_mismatch(label, image.pixel_storage(), PixelStorage::BYTE4);
        ++failures;
    }
    if (image.resolution().x != expected_res.x || image.resolution().y != expected_res.y) {
        log_resolution_mismatch(label, image.resolution(), expected_res);
        ++failures;
    }
    if (failures != 0) {
        return failures;
    }
    auto actual = image.pixel_ptr<uchar4>();
    for (uint y = 0; y < expected_res.y; ++y) {
        for (uint x = 0; x < expected_res.x; ++x) {
            auto idx = pixel_index(expected_res, x, y);
            if (!equal_uchar4(actual[idx], expected_pixels[idx], tolerance, compare_alpha)) {
                log_uchar_mismatch(label, x, y, actual[idx], expected_pixels[idx]);
                ++failures;
            }
        }
    }
    return failures;
}

int verify_loaded_float_image(const char *label,
                              const Image &image,
                              uint2 expected_res,
                              const vector<float4> &expected_pixels,
                              float tolerance,
                              bool compare_alpha = true) {
    int failures = 0;
    if (image.pixel_storage() != PixelStorage::FLOAT4) {
        log_storage_mismatch(label, image.pixel_storage(), PixelStorage::FLOAT4);
        ++failures;
    }
    if (image.resolution().x != expected_res.x || image.resolution().y != expected_res.y) {
        log_resolution_mismatch(label, image.resolution(), expected_res);
        ++failures;
    }
    if (failures != 0) {
        return failures;
    }
    auto actual = image.pixel_ptr<float4>();
    for (uint y = 0; y < expected_res.y; ++y) {
        for (uint x = 0; x < expected_res.x; ++x) {
            auto idx = pixel_index(expected_res, x, y);
            if (!equal_float4(actual[idx], expected_pixels[idx], tolerance, compare_alpha)) {
                log_float_mismatch(label, x, y, actual[idx], expected_pixels[idx]);
                ++failures;
            }
        }
    }
    return failures;
}

int test_byte_round_trip(const TempDirectory &temp_dir,
                         const char *label,
                         const char *extension,
                         uint tolerance,
                         bool compare_alpha = true) {
    constexpr uint2 res = make_uint2(16u, 11u);
    std::cout << "=== " << label << " ===" << std::endl;
    auto source = make_ldr_pixels(res);
    auto file = temp_dir.path / (string(label) + extension);
    auto image = make_owned_image(source, res);
    image.save(file);
    auto loaded = Image::load(file, LINEAR);
    int failures = verify_loaded_byte_image(label, loaded, res, source, tolerance, compare_alpha);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_float_to_ldr_round_trip(const TempDirectory &temp_dir,
                                 const char *label,
                                 const char *extension) {
    constexpr uint2 res = make_uint2(13u, 9u);
    std::cout << "=== " << label << " ===" << std::endl;
    auto source = make_hdr_pixels(res);
    vector<uchar4> expected(res.x * res.y);
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            auto idx = pixel_index(res, x, y);
            expected[idx] = quantize_to_uchar4(source[idx]);
        }
    }
    auto file = temp_dir.path / (string(label) + extension);
    auto image = make_owned_image(source, res);
    image.save(file);
    auto loaded = Image::load(file, LINEAR);
    int failures = verify_loaded_byte_image(label, loaded, res, expected, 0u);
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_float_round_trip(const TempDirectory &temp_dir,
                          const char *label,
                          const char *extension,
                          float tolerance,
                          bool compare_alpha) {
    constexpr uint2 res = make_uint2(12u, 10u);
    std::cout << "=== " << label << " ===" << std::endl;
    auto source = make_hdr_pixels(res);
    auto file = temp_dir.path / (string(label) + extension);
    auto image = make_owned_image(source, res);
    image.save(file);
    auto loaded = Image::load(file, LINEAR);
    int failures = verify_loaded_float_image(label, loaded, res, source, tolerance, compare_alpha);
    if (!compare_alpha && failures == 0) {
        auto actual = loaded.pixel_ptr<float4>();
        for (uint y = 0; y < res.y; ++y) {
            for (uint x = 0; x < res.x; ++x) {
                auto idx = pixel_index(res, x, y);
                if (!close_float(actual[idx].w, 1.f, tolerance)) {
                    log_float_mismatch(label, x, y, actual[idx], make_float4(source[idx].xyz(), 1.f));
                    ++failures;
                }
            }
        }
    }
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

int test_byte_to_float_round_trip(const TempDirectory &temp_dir,
                                  const char *label,
                                  const char *extension,
                                  float tolerance,
                                  bool compare_alpha) {
    constexpr uint2 res = make_uint2(14u, 8u);
    std::cout << "=== " << label << " ===" << std::endl;
    auto source = make_ldr_pixels(res);
    vector<float4> expected(res.x * res.y);
    for (uint y = 0; y < res.y; ++y) {
        for (uint x = 0; x < res.x; ++x) {
            auto idx = pixel_index(res, x, y);
            expected[idx] = normalize_uchar4(source[idx]);
        }
    }
    auto file = temp_dir.path / (string(label) + extension);
    auto image = make_owned_image(source, res);
    image.save(file);
    auto loaded = Image::load(file, LINEAR);
    int failures = verify_loaded_float_image(label, loaded, res, expected, tolerance, compare_alpha);
    if (!compare_alpha && failures == 0) {
        auto actual = loaded.pixel_ptr<float4>();
        for (uint y = 0; y < res.y; ++y) {
            for (uint x = 0; x < res.x; ++x) {
                auto idx = pixel_index(res, x, y);
                if (!close_float(actual[idx].w, 1.f, tolerance)) {
                    log_float_mismatch(label, x, y, actual[idx], make_float4(expected[idx].xyz(), 1.f));
                    ++failures;
                }
            }
        }
    }
    if (failures == 0) {
        std::cout << "  PASSED" << std::endl;
    }
    return failures;
}

}// namespace

int main() {
    log_level_error();
    TempDirectory temp_dir;
    int total_failures = 0;

    total_failures += test_byte_round_trip(temp_dir, "byte_png_round_trip", ".png", 0u);
    total_failures += test_byte_round_trip(temp_dir, "byte_bmp_round_trip", ".bmp", 0u);
    total_failures += test_byte_round_trip(temp_dir, "byte_tga_round_trip", ".tga", 0u);
    total_failures += test_byte_round_trip(temp_dir, "byte_jpg_round_trip", ".jpg", 20u, false);
    total_failures += test_byte_round_trip(temp_dir, "byte_jpeg_round_trip", ".jpeg", 20u, false);

    total_failures += test_float_to_ldr_round_trip(temp_dir, "float_png_round_trip", ".png");

    total_failures += test_float_round_trip(temp_dir, "float_hdr_round_trip", ".hdr", 0.01f, false);
    total_failures += test_float_round_trip(temp_dir, "float_exr_round_trip", ".exr", 1e-6f, true);

    total_failures += test_byte_to_float_round_trip(temp_dir, "byte_hdr_round_trip", ".hdr", 0.01f, false);
    total_failures += test_byte_to_float_round_trip(temp_dir, "byte_exr_round_trip", ".exr", 1e-6f, true);

    if (total_failures != 0) {
        std::cerr << "[test-imageio] failed with " << total_failures << " mismatches" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "[test-imageio] all checks passed" << std::endl;
    return EXIT_SUCCESS;
}