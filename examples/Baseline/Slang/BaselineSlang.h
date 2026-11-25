#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"

#include <chrono>
#include <cstdint>
#include <glm/glm.hpp>

class BaselineSlang final : public ExampleBase {
public:
    explicit BaselineSlang(void* native_window);
    ~BaselineSlang() override = default;
    void run() override;

private:
    std::chrono::steady_clock::time_point start;
    std::uint64_t frame = 0;
    HardwareDisplayer display;
    RasterGeometry geometry;
    HardwareImage texture_image;
    HardwareImage color;
    RasterizerPipeline<> rasterizer;
    struct DrawParameters {
        glm::mat4 model{};
        uint32_t texIndex{};
    } parameters{};
    HardwareExecutor render_executor;
};
