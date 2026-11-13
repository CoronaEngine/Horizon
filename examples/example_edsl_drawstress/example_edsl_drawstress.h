#pragma once
#include "legacy_common.h"
#include <memory>

void run_example_edsl_drawstress();

class DrawstressEdslPipeline {
public:
    HardwareImage outColor;
    DrawstressEdslPipeline();
    ~DrawstressEdslPipeline();
    void setDepthImage(HardwareImage&);
    RasterBatch beginBatch(uint16_t width, uint16_t height);
private:
    struct State;
    std::unique_ptr<State> state_;
};
