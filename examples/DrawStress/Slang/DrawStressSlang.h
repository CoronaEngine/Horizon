#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(DrawStress/Slang/drawstress_vert.slang)
#include SLANG(DrawStress/Slang/drawstress_frag.slang)

#include <array>
#include <cstddef>
#include <cstdint>
#include <ktm/ktm.h>

class DrawStressSlang final : public ExampleBase {
public:
    explicit DrawStressSlang(void* native_window);
    ~DrawStressSlang() override = default;
    void run(float seconds) override;

private:
    static constexpr int dim = 40;
    static constexpr uint32_t draw_count = dim * dim * dim;

    HardwareDisplayer display;
    HardwareBuffer vertex_buffer, index_buffer;
    HardwareBuffer commands;
    std::array<HardwareBuffer, 2> parameter_buffers;
    std::size_t next_parameter_buffer{};
    ktm::fvec4 axis_projection;
    std::array<ktm::fvec4, dim> translations;
    HardwareImage final_output_image;
    RasterPipeline<drawstress_vert_slang_t, drawstress_frag_slang_t> pipeline;
    HardwareExecutor render_executor;
};
