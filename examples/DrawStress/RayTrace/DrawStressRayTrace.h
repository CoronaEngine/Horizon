#pragma once

#include <vector>
#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(camera_raygen.slang)
#include SLANG(DrawStress/RayTrace/drawstress_miss.slang)
#include SLANG(DrawStress/RayTrace/drawstress_closest_hit.slang)

class DrawStressRayTrace final : public ExampleBase {
public:
    explicit DrawStressRayTrace(void* native_window);
    void run(float seconds) override;

private:
    static constexpr uint32_t width = 800, height = 600;
    HardwareDisplayer display;
    HardwareImage color{width, height, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage};
    RayTracePipeline<camera_raygen_slang_t, drawstress_miss_slang_t, drawstress_closest_hit_slang_t> pipeline;
    static constexpr int dim = 40;
    std::vector<RayTracePipeline<>::Instance> instances;
    HardwareExecutor executor;
};
