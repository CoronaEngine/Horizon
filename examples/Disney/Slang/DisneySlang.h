#pragma once

#include "../DisneyScene.h"
#include "../../ExampleBase.h"
#include "Codegen/ControlFlows.h"
#include SLANG(Disney/Slang/disney_vert.slang)
#include SLANG(Disney/Slang/disney_frag.slang)

class DisneySlang final : public ExampleBase {
public:
    explicit DisneySlang(void* native_window);
    void run(float) override;

private:
    HardwareExecutor render_executor;
    DisneyScene scene;
    HardwareDisplayer display;
    HardwareBuffer vertex_buffer, index_buffer, commands;
    RasterPipeline<disney_vert_slang_t, disney_frag_slang_t> pipeline;
};
