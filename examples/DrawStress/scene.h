#pragma once

#include <glm/mat4x4.hpp>
#include <cstdint>
#include <span>

struct RasterGeometry;
struct HardwareBuffer;
struct GLFWwindow;

namespace drawstress {
constexpr uint32_t stress_width = 1920;
constexpr uint32_t stress_height = 1080;
constexpr int dim_min = 2;
constexpr int dim_max = 40;

void attach_input(GLFWwindow* window, int& dim, const char* mode);
std::span<const uint16_t> cube_triangle_indices();
RasterGeometry make_cube();
HardwareBuffer make_draw_packets();
glm::mat4 make_view_projection();
void write_matrices(std::span<std::byte> destination, const glm::mat4& view_projection, int dim, float time);
} // namespace drawstress
