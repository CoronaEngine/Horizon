#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(Baseline/Slang/baseline_vert.slang)
#include SLANG(Baseline/Slang/baseline_frag.slang)

#include <array>
#include <cstddef>
#include <cstdint>
#include <ktm/ktm.h>

class BaselineSlang final : public ExampleBase {
public:
    explicit BaselineSlang(void* native_window);
    ~BaselineSlang() override = default;
    void run(float seconds) override;

private:
    HardwareDisplayer display;
    HardwareBuffer vertex_buffer, index_buffer, commands;
    HardwareImage texture_image;
    HardwareImage color;
    RasterizerPipeline<baseline_vert_slang_t, baseline_frag_slang_t> pipeline;
    struct DrawParameters {
        ktm::fmat4x4 model{};
        uint32_t texIndex{};
    } parameters{};
    static_assert(offsetof(DrawParameters, texIndex) == 64);
    std::array<HardwareBuffer, 2> parameter_buffers;
    std::size_t next_parameter_buffer{};
    HardwareExecutor render_executor;
};
