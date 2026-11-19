#include "example_drawstress_edsl.h"
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
        auto vs = [&]() {
            const auto corner = vertexIndex();
            Aggregate<Varyings> out;
            out->color = Float3(corner & Uint(1), (corner >> Uint(1)) & Uint(1), (corner >> Uint(2)) & Uint(1));
            Float3 pos = out->color * Float3(2.0f, -2.0f, -2.0f) + Float3(-1.0f, 1.0f, 1.0f);
            auto mvp = rasterDrawParameter<fmat4x4>(0);
            position() = mul(mvp, Float4(pos, Float(1)));
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
