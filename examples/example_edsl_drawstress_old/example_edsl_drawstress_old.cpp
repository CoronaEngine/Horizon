#include "example_edsl_drawstress_old.h"
#include "example_drawstress_old/example_drawstress_old.h"
#include "legacy_edsl_support.h"

using namespace legacy_example::edsl;

namespace legacy_drawstress_edsl {
struct Varyings { Float3 color; };
}
using namespace legacy_drawstress_edsl;

struct DrawstressEdslOldPipeline::State {
    // View the existing 24-byte vertices and 64-byte matrices without aggregate indexing.
    EmbeddedShader::Array<float> vertices;
    EmbeddedShader::Array<ktm::fmat4x4> matrices;
    VariateProxy<uint32_t> draw_index;
    Texture2D<fvec4> output;
    std::unique_ptr<RasterizerPipeline<>> pipeline;
    State() {
        draw_index.as_push_constant();
        auto vs = [&] {
            Uint base = vertexIndex() * Uint(6);
            Float3 p(vertices[base], vertices[base + Uint(1)], vertices[base + Uint(2)]);
            // Declare first: copying an indexed proxy would inherit the array's AST type.
            Float4x4 mvp;
            mvp = matrices[draw_index];
            position() = mul(mvp, Float4(p, Float(1)));
            Aggregate<Varyings> out;
            out->color = Float3(vertices[base + Uint(3)], vertices[base + Uint(4)], vertices[base + Uint(5)]);
            return out;
        };
        auto fs = [&](Aggregate<Varyings> in) { output << Float4(in->color, Float(1)); };
        pipeline = std::make_unique<RasterizerPipeline<>>(vs, fs, 1, options());
    }
};

DrawstressEdslOldPipeline::DrawstressEdslOldPipeline() : state_(std::make_unique<State>()) {}
DrawstressEdslOldPipeline::~DrawstressEdslOldPipeline() = default;
void DrawstressEdslOldPipeline::bind_buffers(HardwareBuffer& matrices, HardwareBuffer& vertices) {
    state_->matrices = matrices;
    state_->vertices = vertices;
    state_->output = outColor;
}
void DrawstressEdslOldPipeline::setDepthImage(HardwareImage& image) { state_->pipeline->setDepthImage(image); }
void DrawstressEdslOldPipeline::record(const HardwareBuffer& ib, const HardwareBuffer& vb, const DrawIndexedParams& params) {
    state_->draw_index = model_pc.draw_index;
    (*state_->pipeline)(0, 0).record(ib, vb, params);
}
RasterizerPipelineBase& DrawstressEdslOldPipeline::operator()(uint16_t width, uint16_t height) {
    return (*state_->pipeline)(width, height);
}
void run_example_edsl_drawstress_old() { run_example_drawstress_old(true); }
