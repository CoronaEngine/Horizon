#pragma once

#include "../../ExampleBase.h"
#include "../DisneyScene.h"
#include "Codegen/ControlFlows.h"
#include SLANG(Disney/Mesh/disney_mesh.slang)
#include SLANG(Disney/Slang/disney_frag.slang)

class DisneyMesh final : public ExampleBase {
public:
    explicit DisneyMesh(void* native_window);
    void run(float) override;

private:
    HardwareExecutor executor;
    DisneyScene scene;
    HardwareDisplayer display;
    HardwareBuffer commands;
    MeshPipeline<disney_mesh_slang_t, disney_frag_slang_t> pipeline;
};
