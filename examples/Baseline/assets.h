#pragma once

#include <array>
#include <cstdint>
#include <vector>

struct RasterGeometry;
struct HardwareImage;

namespace baseline {
struct Vertex {
    std::array<float, 3> pos{};
    std::array<float, 2> tex_coord{};
    bool operator==(const Vertex&) const = default;
};
struct ModelData {
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
};
struct TextureData {
    int width, height;
    std::vector<unsigned char> rgba;
};
ModelData read_model(const char* asset_name);
TextureData read_texture(const char* asset_name);
RasterGeometry load_model(const char* asset_name);
HardwareImage load_texture(const char* asset_name);
} // namespace baseline
