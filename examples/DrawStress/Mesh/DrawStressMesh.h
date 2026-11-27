#pragma once

#include "../../ExampleBase.h"
#include "../DrawStressTransforms.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(DrawStress/Mesh/drawstress_mesh.slang)
#include SLANG(DrawStress/Slang/drawstress_frag.slang)

class DrawStressMesh final : public ExampleBase {
public:
    explicit DrawStressMesh(void* native_window);
    void run(float seconds) override;

private:
    HardwareDisplayer display;
    HardwareImage color, depth;
    HardwareBuffer commands;
    DrawStressTransforms transforms;
    MeshPipeline<drawstress_mesh_slang_t, drawstress_frag_slang_t> pipeline;
    HardwareExecutor executor;
};
