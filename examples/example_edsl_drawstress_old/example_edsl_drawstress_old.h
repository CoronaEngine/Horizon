#pragma once
#include "legacy_common.h"
#include <memory>

void run_example_edsl_drawstress_old();

class DrawstressEdslOldPipeline {
public:
    struct Draw {
        uint32_t per_draw_ssbo_index{}, vertex_ssbo_index{}, draw_index{};
    } model_pc;
    HardwareImage outColor;
    DrawstressEdslOldPipeline();
    ~DrawstressEdslOldPipeline();
    void bind_buffers(HardwareBuffer& matrices, HardwareBuffer& vertices);
    void setDepthImage(HardwareImage&);
    void record(const HardwareBuffer&, const HardwareBuffer&, const DrawIndexedParams&);
    RasterizerPipelineBase& operator()(uint16_t width, uint16_t height);
private:
    struct State;
    std::unique_ptr<State> state_;
};
