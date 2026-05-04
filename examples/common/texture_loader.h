#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Horizon.h"

enum class TextureEncoding : uint8_t
{
    RGBA8_SRGB,
    BC1_RGB_UNORM,
    BC1_RGB_SRGB,
};

struct TextureLoadOptions
{
    TextureEncoding encoding = TextureEncoding::RGBA8_SRGB;
    bool flip_vertically = false;
};

struct TextureLoadResult
{
    HardwareImage texture;
    uint32_t descriptor_id = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool success = false;
};

std::vector<uint8_t> compress_rgba_to_bc1(const unsigned char *rgba_data, int width, int height);

TextureLoadResult load_texture_from_file(const std::filesystem::path &texture_path,
                                         const TextureLoadOptions &options,
                                         std::string &error_message);

TextureLoadResult load_texture_rgba8_srgb(const std::filesystem::path &texture_path,
                                          bool flip_vertically,
                                          std::string &error_message);

TextureLoadResult load_texture_bc1(const std::filesystem::path &texture_path,
                                   bool use_srgb,
                                   bool flip_vertically,
                                   std::string &error_message);
