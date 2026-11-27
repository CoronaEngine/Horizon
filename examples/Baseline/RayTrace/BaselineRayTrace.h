#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(Baseline/RayTrace/baseline_raygen.slang)
#include SLANG(Baseline/RayTrace/baseline_miss.slang)
#include SLANG(Baseline/RayTrace/baseline_closest_hit.slang)

class BaselineRayTrace final : public ExampleBase {
public:
    explicit BaselineRayTrace(void* native_window);
    void run(float seconds) override;

private:
    static constexpr uint32_t width = 800, height = 600;
    HardwareDisplayer display;
    HardwareImage color{width, height, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage};
    RayTracePipeline<baseline_raygen_slang_t, baseline_miss_slang_t, baseline_closest_hit_slang_t> pipeline;
    ktm::fmat4x4 viewProjection;
    HardwareExecutor executor;
};
