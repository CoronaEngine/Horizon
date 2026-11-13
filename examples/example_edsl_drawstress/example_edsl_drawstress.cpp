#include "example_edsl_drawstress.h"
#include "example_drawstress/example_drawstress.h"
#include "legacy_edsl_support.h"

using namespace legacy_example::edsl;

namespace legacy_drawstress_edsl {
struct Varyings { Float3 color; };
}
using namespace legacy_drawstress_edsl;

struct DrawstressEdslPipeline::State {
    Texture2D<fvec4> output;
    std::unique_ptr<RasterizerPipeline<>> pipeline;
    State() {
        auto vs = [&](Float3 pos, Float3 color) {
            auto mvp = rasterDrawParameter<fmat4x4>(0);
            position() = mul(mvp, Float4(pos, Float(1)));
            Aggregate<Varyings> out;
            out->color = color;
            return out;
        };
        auto fs = [&](Aggregate<Varyings> in) { output << Float4(in->color, Float(1)); };
        pipeline = std::make_unique<RasterizerPipeline<>>(vs, fs, 1, options());
    }
};

DrawstressEdslPipeline::DrawstressEdslPipeline() : state_(std::make_unique<State>()) {}
DrawstressEdslPipeline::~DrawstressEdslPipeline() = default;
RasterBatch DrawstressEdslPipeline::beginBatch(uint16_t width, uint16_t height) {
    state_->output = outColor;
    return state_->pipeline->beginBatch(width, height);
}
void DrawstressEdslPipeline::setDepthImage(HardwareImage& image) { state_->pipeline->setDepthImage(image); }
void run_example_edsl_drawstress() { run_example_drawstress(true); }
