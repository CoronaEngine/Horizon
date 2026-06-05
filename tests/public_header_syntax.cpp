#include <horizon.h>

namespace
{
    void horizon_public_header_compile_surface()
    {
        using namespace Corona::Horizon;

        HardwareBuffer buffer;
        HardwareImage image;
        CommandBatch batch;
        HardwareExecutor* executor = nullptr;
        HardwareDisplayer* displayer = nullptr;
        HardwareStream* stream = nullptr;
        RasterizerPipeline* rasterizer = nullptr;
        ComputePipeline* compute = nullptr;

        Format format = Format::RGBA8_UNORM;
        BufferUsageFlags buffer_usage = BufferUsageFlags::Vertex | BufferUsageFlags::Index | BufferUsageFlags::Storage;
        ImageUsageFlags image_usage = ImageUsageFlags::Sampled | ImageUsageFlags::Storage | ImageUsageFlags::TransferSrc | ImageUsageFlags::TransferDst;
        HardwareImageDesc image_desc = HardwareImageDesc::texture_2d(1, 1, format, image_usage);
        HardwareBufferDesc buffer_desc = HardwareBufferDesc::typed<uint32_t>(1, buffer_usage);

        DrawIndexedParams draw_params;
        ScissorRect scissor;
        DrawIndexedDesc draw_desc;
        CopyRegion copy_region;
        BufferImageCopyRegion buffer_image_region;
        ImageCopyRegion image_region;
        DispatchDesc dispatch_desc;
        RenderingDesc rendering_desc;

        batch << copy({}, {}, copy_region)
              << copy_image({}, {}, image_region)
              << copy_to_image({}, {}, buffer_image_region)
              << copy_to_buffer({}, {}, buffer_image_region)
              << dispatch({}, dispatch_desc)
              << begin_rendering(rendering_desc)
              << end_rendering()
              << draw_indexed({}, {}, draw_desc)
              << present(DisplayerRef {}, ImageRef {})
              << host_callback({})
              << keep_alive(1);

        (void)buffer;
        (void)image;
        (void)executor;
        (void)displayer;
        (void)stream;
        (void)rasterizer;
        (void)compute;
        (void)image_desc;
        (void)buffer_desc;
        (void)draw_params;
        (void)scissor;
        (void)commit();
    }
}
