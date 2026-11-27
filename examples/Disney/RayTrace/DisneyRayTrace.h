#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(Disney/RayTrace/disney_raygen.slang)
#include SLANG(Disney/RayTrace/disney_miss.slang)
#include SLANG(Disney/RayTrace/disney_closest_hit.slang)

class DisneyRayTrace final : public ExampleBase {
public:
    explicit DisneyRayTrace(void* native_window);
    void run(float seconds) override;

private:
    static constexpr uint32_t width = 800, height = 600;
    HardwareDisplayer display;
    HardwareImage color{width, height, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage};
    RayTracePipeline<disney_raygen_slang_t, disney_miss_slang_t, disney_closest_hit_slang_t> pipeline;
    HardwareExecutor executor;
};
