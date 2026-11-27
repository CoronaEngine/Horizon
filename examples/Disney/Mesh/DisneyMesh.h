#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "Codegen/ControlFlows.h"
#include SLANG(Disney/Mesh/disney_mesh.slang)
#include SLANG(Disney/Slang/disney_frag.slang)

class DisneyMesh final : public ExampleBase {
public:
    explicit DisneyMesh(void* native_window);
    void run(float) override;

private:
    static constexpr uint32_t rows = 10, columns = 11;
    HardwareImage color{800, 600, HardwareImage::Format::RGBA16_FLOAT};
    HardwareImage depth{800, 600, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage};
    HardwareDisplayer display;
    HardwareBuffer commands;
    MeshPipeline<disney_mesh_slang_t, disney_frag_slang_t> pipeline;
    HardwareExecutor executor;
};
