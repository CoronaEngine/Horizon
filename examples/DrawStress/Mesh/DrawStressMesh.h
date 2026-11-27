#pragma once

#include "../../ExampleBase.h"
#include <array>
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(DrawStress/Mesh/drawstress_mesh.slang)
#include SLANG(DrawStress/Slang/drawstress_frag.slang)

class DrawStressMesh final : public ExampleBase {
public:
    explicit DrawStressMesh(void* native_window);
    void run(float seconds) override;

private:
    static constexpr int dim = 40;
    static constexpr uint32_t draw_count = dim * dim * dim;

    HardwareDisplayer display;
    HardwareImage color, depth;
    HardwareBuffer commands;
    std::array<HardwareBuffer, 2> parameter_buffers;
    std::size_t next_parameter_buffer{};
    ktm::fvec4 axis_projection;
    std::array<ktm::fvec4, dim> translations;
    MeshPipeline<drawstress_mesh_slang_t, drawstress_frag_slang_t> pipeline;
    HardwareExecutor executor;
};
