#include "image/image.h"

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <tinyexr.h>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void expect_near(float actual, float expected, float tolerance, const char *message) {
    expect(std::fabs(actual - expected) <= tolerance, message);
}

std::filesystem::path test_directory() {
    const auto path = std::filesystem::temp_directory_path() / "horizon-image-tests";
    std::filesystem::create_directories(path);
    return path;
}

void remove_test_file(const std::filesystem::path &path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

void test_in_memory_image() {
    using horizon::core::PixelStorage;
    using horizon::image::Image;
    using horizon::math::float4;
    using horizon::math::make_uint2;

    const std::array<float4, 4> pixels{{
        {0.25f, 0.5f, 1.0f, 1.0f},
        {1.5f, 0.25f, 0.0f, 1.0f},
        {0.0f, 2.0f, 0.5f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    }};

    Image image = Image::from_data(pixels.data(), make_uint2(2u, 2u));
    expect(image.width() == 2u && image.height() == 2u, "in-memory resolution");
    expect(image.pixel_storage() == PixelStorage::FLOAT4, "in-memory pixel storage");
    expect(image.size_in_bytes() == sizeof(pixels), "in-memory byte size");
    expect(image.view().pixel_ptr<float4>() == image.pixel_ptr<float4>(), "view references image pixels");
    expect(std::memcmp(image.pixel_ptr(), pixels.data(), sizeof(pixels)) == 0,
           "in-memory pixels are copied exactly");
}

void test_channel_conversions() {
    using horizon::core::PixelStorage;
    using horizon::core::delete_array;
    using horizon::image::Image;
    using horizon::math::float2;
    using horizon::math::make_uint2;

    const std::array<uint8_t, 3> gray_pixels{0u, 127u, 255u};
    auto [gray_format, gray_storage] = Image::convert_to_32bit(
        PixelStorage::BYTE1, reinterpret_cast<const std::byte *>(gray_pixels.data()),
        make_uint2(3u, 1u));
    expect(gray_format == PixelStorage::FLOAT1, "BYTE1 converts to FLOAT1");
    const auto *gray_float = reinterpret_cast<const float *>(gray_storage);
    expect_near(gray_float[0], 0.0f, 1e-6f, "BYTE1 zero conversion");
    expect_near(gray_float[1], 127.0f / 255.0f, 1e-6f, "BYTE1 middle conversion");
    expect_near(gray_float[2], 1.0f, 1e-6f, "BYTE1 maximum conversion");
    delete_array(gray_storage);

    const std::array<float2, 2> dual_pixels{{{0.0f, 1.0f}, {0.5f, 0.25f}}};
    auto [byte_format, byte_storage] = Image::convert_to_8bit(
        PixelStorage::FLOAT2, reinterpret_cast<const std::byte *>(dual_pixels.data()),
        make_uint2(2u, 1u));
    expect(byte_format == PixelStorage::BYTE2, "FLOAT2 converts to BYTE2");
    const auto *dual_bytes = reinterpret_cast<const uint8_t *>(byte_storage);
    expect(dual_bytes[0] == 0u && dual_bytes[1] == 255u,
           "FLOAT2 first pixel conversion");
    expect(dual_bytes[2] == 128u && dual_bytes[3] == 64u,
           "FLOAT2 second pixel conversion");
    delete_array(byte_storage);
}

void test_8bit_formats(const std::filesystem::path &directory) {
    using horizon::core::PixelStorage;
    using horizon::image::ColorSpace;
    using horizon::image::Image;
    using horizon::math::make_uchar4;
    using horizon::math::make_uint2;
    using horizon::math::uchar4;

    std::array<uchar4, 4> pixels{{
        make_uchar4(255u, 0u, 0u, 255u),
        make_uchar4(0u, 255u, 0u, 255u),
        make_uchar4(0u, 0u, 255u, 255u),
        make_uchar4(255u, 255u, 255u, 255u),
    }};
    Image source = Image::from_data(pixels.data(), make_uint2(2u, 2u));

    for (const auto *extension : {".png", ".bmp", ".tga", ".jpg"}) {
        const auto path = directory / (std::string{"roundtrip"} + extension);
        remove_test_file(path);
        source.save(path);
        expect(std::filesystem::exists(path), "8-bit image file is created");

        Image loaded = Image::load(path, ColorSpace::LINEAR);
        expect(loaded.width() == 2u && loaded.height() == 2u, "8-bit round-trip resolution");
        expect(loaded.pixel_storage() == PixelStorage::BYTE4, "8-bit round-trip storage");
        if (std::string_view{extension} != ".jpg") {
            expect(std::memcmp(loaded.pixel_ptr(), pixels.data(), sizeof(pixels)) == 0,
                   "lossless 8-bit round-trip pixels");
        }
        remove_test_file(path);
    }
}

void test_float_formats(const std::filesystem::path &directory) {
    using horizon::core::PixelStorage;
    using horizon::image::ColorSpace;
    using horizon::image::Image;
    using horizon::math::float4;
    using horizon::math::make_uint2;

    std::array<float4, 4> pixels{{
        {0.25f, 0.5f, 1.0f, 1.0f},
        {1.5f, 0.25f, 0.0f, 1.0f},
        {0.0f, 2.0f, 0.5f, 1.0f},
        {1.0f, 1.0f, 1.0f, 1.0f},
    }};
    Image source = Image::from_data(pixels.data(), make_uint2(2u, 2u));

    for (const auto *extension : {".hdr", ".exr"}) {
        const auto path = directory / (std::string{"roundtrip"} + extension);
        remove_test_file(path);
        source.save(path);
        expect(std::filesystem::exists(path), "float image file is created");

        Image loaded = Image::load(path, ColorSpace::LINEAR);
        expect(loaded.width() == 2u && loaded.height() == 2u, "float round-trip resolution");
        expect(loaded.pixel_storage() == PixelStorage::FLOAT4, "float round-trip storage");
        const auto *loaded_pixels = loaded.pixel_ptr<float4>();
        for (size_t pixel_index = 0; pixel_index < pixels.size(); ++pixel_index) {
            for (size_t channel = 0; channel < 4; ++channel) {
                expect(std::isfinite(loaded_pixels[pixel_index][channel]),
                       "float round-trip values are finite");
                expect_near(loaded_pixels[pixel_index][channel], pixels[pixel_index][channel],
                            extension == std::string_view{".hdr"} ? 0.02f : 1e-5f,
                            "float round-trip values");
            }
        }
        remove_test_file(path);
    }
}

void test_narrow_channel_saves(const std::filesystem::path &directory) {
    using horizon::core::PixelStorage;
    using horizon::image::ColorSpace;
    using horizon::image::Image;
    using horizon::math::float2;
    using horizon::math::make_uint2;

    const std::array<uint8_t, 2> gray_pixels{0u, 127u};
    Image gray = Image::from_data(gray_pixels.data(), make_uint2(2u, 1u));
    const auto gray_path = directory / "gray.png";
    remove_test_file(gray_path);
    gray.save(gray_path);
    Image loaded_gray = Image::load(gray_path, ColorSpace::LINEAR);
    const auto *rgba = loaded_gray.pixel_ptr<uint8_t>();
    expect(loaded_gray.pixel_storage() == PixelStorage::BYTE4,
           "one-channel PNG loads as RGBA");
    expect(rgba[0] == 0u && rgba[1] == 0u && rgba[2] == 0u && rgba[3] == 255u,
           "one-channel PNG first pixel");
    expect(rgba[4] == 127u && rgba[5] == 127u && rgba[6] == 127u && rgba[7] == 255u,
           "one-channel PNG second pixel");
    remove_test_file(gray_path);

    const std::array<float2, 2> dual_pixels{{{0.25f, 0.75f}, {0.5f, 1.0f}}};
    Image dual = Image::from_data(dual_pixels.data(), make_uint2(2u, 1u));
    const auto exr_path = directory / "dual.exr";
    remove_test_file(exr_path);
    dual.save(exr_path);
    Image loaded_dual = Image::load(exr_path, ColorSpace::LINEAR);
    expect(loaded_dual.pixel_storage() == PixelStorage::FLOAT2,
           "two-channel EXR preserves channel count");
    const auto *dual_result = loaded_dual.pixel_ptr<float2>();
    expect_near(dual_result[0].x, dual_pixels[0].x, 1e-5f,
                "two-channel EXR first channel");
    expect_near(dual_result[0].y, dual_pixels[0].y, 1e-5f,
                "two-channel EXR second channel");
    remove_test_file(exr_path);
}

void test_three_channel_exr(const std::filesystem::path &directory) {
    using horizon::core::PixelStorage;
    using horizon::image::ColorSpace;
    using horizon::image::Image;
    using horizon::math::float4;

    const std::array<float, 6> rgb_pixels{
        0.25f, 0.5f, 0.75f,
        1.0f, 0.125f, 0.625f,
    };
    const auto path = directory / "rgb.exr";
    remove_test_file(path);
    const char *error = nullptr;
    const int result = SaveEXR(rgb_pixels.data(), 2, 1, 3, 0,
                               path.string().c_str(), &error);
    if (result != TINYEXR_SUCCESS) {
        const std::string message = error == nullptr ? "unknown TinyEXR error" : error;
        FreeEXRErrorMessage(error);
        std::cerr << "FAIL: RGB EXR fixture creation: " << message << '\n';
        ++failures;
        return;
    }

    Image loaded = Image::load(path, ColorSpace::LINEAR);
    expect(loaded.pixel_storage() == PixelStorage::FLOAT4,
           "three-channel EXR expands to FLOAT4");
    const auto *pixels = loaded.pixel_ptr<float4>();
    expect_near(pixels[0].x, rgb_pixels[0], 1e-5f, "RGB EXR red channel");
    expect_near(pixels[0].y, rgb_pixels[1], 1e-5f, "RGB EXR green channel");
    expect_near(pixels[0].z, rgb_pixels[2], 1e-5f, "RGB EXR blue channel");
    expect_near(pixels[0].w, 1.0f, 1e-5f, "RGB EXR alpha expansion");
    remove_test_file(path);
}

void test_unsupported_extension(const std::filesystem::path &directory) {
    using horizon::image::Image;
    using horizon::math::float4;

    const auto path = directory / "unsupported.gif";
    remove_test_file(path);
    Image image = Image::pure_color(float4{1.0f, 0.0f, 0.0f, 1.0f},
                                    horizon::image::ColorSpace::LINEAR);

    try {
        image.save(path);
        expect(false, "unsupported extension throws");
    } catch (const std::exception &error) {
        const std::string message = error.what();
        expect(message.find(".gif") != std::string::npos,
               "unsupported extension error names extension");
    }
    expect(!std::filesystem::exists(path), "unsupported extension creates no file");
}

void test_missing_hdr_reports_error(const std::filesystem::path &directory) {
    using horizon::image::ColorSpace;
    using horizon::image::Image;

    const auto path = directory / "missing.hdr";
    remove_test_file(path);
    try {
        static_cast<void>(Image::load(path, ColorSpace::LINEAR));
        expect(false, "missing HDR throws");
    } catch (const std::exception &error) {
        expect(std::string{error.what()}.find(path.string()) != std::string::npos,
               "missing HDR error names path");
    }
}

void test_write_failure_reports_error(const std::filesystem::path &directory) {
    using horizon::image::ColorSpace;
    using horizon::image::Image;
    using horizon::math::float4;

    const auto blocking_file = directory / "not-a-directory";
    remove_test_file(blocking_file);
    {
        std::ofstream stream{blocking_file, std::ios::binary};
        stream.put('x');
    }
    const auto path = blocking_file / "output.png";
    Image image = Image::pure_color(float4{1.0f, 0.0f, 0.0f, 1.0f},
                                    ColorSpace::LINEAR);
    try {
        image.save(path);
        expect(false, "write failure throws");
    } catch (const std::exception &error) {
        expect(std::string{error.what()}.find(path.string()) != std::string::npos,
               "write failure error names path");
    }
    remove_test_file(blocking_file);
}

void test_invalid_save_input_reports_error(const std::filesystem::path &directory) {
    using horizon::core::PixelStorage;
    using horizon::image::Image;
    using horizon::math::make_uint2;

    const std::array<std::byte, 4> pixels{};
    const auto path = directory / "invalid.png";
    remove_test_file(path);
    try {
        Image::save_image(path, PixelStorage::UNKNOWN, make_uint2(1u, 1u),
                          pixels.data());
        expect(false, "invalid save format throws");
    } catch (const std::exception &error) {
        const std::string message = error.what();
        expect(message.find(path.string()) != std::string::npos,
               "invalid save format error names path");
        expect(message.find("pixel storage") != std::string::npos,
               "invalid save format error names pixel storage");
    }
}

}// namespace

int main() {
    const auto directory = test_directory();
    test_in_memory_image();
    test_channel_conversions();
    test_8bit_formats(directory);
    test_float_formats(directory);
    test_narrow_channel_saves(directory);
    test_three_channel_exr(directory);
    test_unsupported_extension(directory);
    test_missing_hdr_reports_error(directory);
    test_write_failure_reports_error(directory);
    test_invalid_save_input_reports_error(directory);
    return failures == 0 ? 0 : 1;
}
