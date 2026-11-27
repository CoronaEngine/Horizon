#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(Baseline/Mesh/baseline_mesh.slang)
#include SLANG(Baseline/Slang/baseline_frag.slang)

class BaselineMesh final : public ExampleBase {
public:
    explicit BaselineMesh(void* native_window);
    void run(float seconds) override;

private:
    HardwareDisplayer display;
    HardwareImage color, depth;
    HardwareBuffer groups;
    MeshPipeline<baseline_mesh_slang_t, baseline_frag_slang_t> pipeline;
    const ktm::fmat4x4 view = ktm::look_at_rh<float>({2, 2, 2}, {0, 0, 0}, {0, 0, 1});
    HardwareExecutor executor;
};
