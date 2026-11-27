#include "BaselineSlang.h"

BaselineSlang::BaselineSlang(void* native_window)
    : ExampleBase(native_window), display(native_window),
      color(800, 600, HardwareImage::Format::RGBA16_FLOAT),
      depth(800, 600, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage),
      pipeline({.compare = RasterPipeline<>::Compare::LessEqual})
{
    assets = loadBaselineAssets(render_executor, HardwareBuffer::Usage::VertexBuffer, HardwareBuffer::Usage::IndexBuffer);
    const RasterPipeline<>::IndirectPacket command{.vertexCount = static_cast<uint32_t>(assets.indices.getElementCount()),
                                                   .instanceCount = 1};
    render_executor << commands.createDeviceLocal(HardwareBuffer::Usage::RasterIndirect, &command, 1, sizeof(command));
    render_executor.commit().wait();

    pipeline.outColor = color;
    pipeline.vp.texIndex = assets.texture.storeSampledDescriptor();
    pipeline.vp.view = ktm::look_at_rh<float>({2, 2, 2}, {0, 0, 0}, {0, 0, 1});
    pipeline.vp.proj = baselineProjection();
    for (auto& buffer : parameter_buffers)
        buffer = HardwareBuffer(sizeof(ktm::fmat4x4), HardwareBuffer::Usage::StorageBuffer);
}

void BaselineSlang::run(float seconds)
{
    const auto model = ktm::rotate3d_z(seconds * ktm::radians(90.0f));
    if (next_parameter_buffer == parameter_buffers.size()) {
        render_executor.wait();
        next_parameter_buffer = 0;
    }
    auto& parameter_buffer = parameter_buffers[next_parameter_buffer++];
    parameter_buffer.copyFromData(&model, sizeof(model));

    render_executor << pipeline(assets.vertices, assets.indices, commands, 1, parameter_buffer, depth)
                    << display(color);
    render_executor.commit();
}
