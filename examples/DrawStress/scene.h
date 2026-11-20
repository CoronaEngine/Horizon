#pragma once

#include "Horizon.h"
#include <glm/mat4x4.hpp>
#include <cstdint>

struct GLFWwindow;

namespace drawstress {
constexpr uint32_t stress_width = 1920;
constexpr uint32_t stress_height = 1080;
constexpr int dim_min = 2;
constexpr int dim_max = 40;

struct StressState
{
    int dim = 40; // 与 bgfx drawstress 默认一致（64000 draws）
    uint32_t draw_count() const { return static_cast<uint32_t>(dim * dim * dim); }
};

void attach_input(GLFWwindow* window, StressState& state, const char* mode);

class CubeGrid {
public:
    CubeGrid();
    void draw(RasterBatch& batch, const StressState& state, float time) const;

private:
    RasterGeometry cube_;
    HardwareBuffer packets_;
    glm::mat4 view_proj_;
};
} // namespace drawstress
