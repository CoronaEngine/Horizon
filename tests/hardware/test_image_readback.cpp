#include <horizon.h>
#include "../../src/hardware_wrapper_vulkan/hardware/command_ir.h"
#include "../../src/hardware_wrapper_vulkan/pipeline/vulkan_compute_pipeline.h"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <future>

using namespace Corona::Horizon;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void bindless_composite_tracks_every_image() {
    // Recording-only fixture: no shader is submitted. The resource-use list is
    // independently checked before Vulkan's layout encoder consumes it.
    ComputePipelineShaders shaders;
    shaders.compute.shaderCode=std::vector<uint32_t>{0x07230203};
    shaders.compute.shaderResources.pushConstantSize=12;
    EmbeddedShader::ShaderCodeModule::ShaderResources::ShaderBindInfo binding;
    binding.set=2; binding.binding=0; binding.elementCount=0;
    binding.bindType=decltype(binding.bindType)::storageTexture;
    shaders.compute.shaderResources.bindInfoPool.push_back(binding);
    VulkanComputePipeline pipeline({},shaders);
    const auto desc=HardwareImageDesc::texture_2d(3,2,Format::RGBA16_FLOAT,
        ImageUsage_Storage|ImageUsage_TransferSrc|ImageUsage_TransferDst);
    HardwareImage background(desc),foreground(desc),output(desc),replacement(desc);
    for(const auto& [offset,image]:std::array<std::pair<uint64_t,HardwareImage>,3>{{
        {0,background},{4,foreground},{8,output}}})
        pipeline.set_resource_direct(offset,4,image,static_cast<int32_t>(binding.bindType),2,0);
    ComputePipelineBase public_pipeline;
    CommandRecorder recorder;
    pipeline.record_into(public_pipeline,recorder);
    auto task=recorder.close();
    expect(task.commands.size()==1 && task.commands[0].resources.size()==3 &&
           task.commands[0].payload.dispatch.bindings.size()==3,
           "all three bindless composite images must be tracked, even in the same descriptor array");
    pipeline.set_resource_direct(4,4,replacement,static_cast<int32_t>(binding.bindType),2,0);
    auto snapshot=pipeline.snapshot();
    expect(snapshot.images.size()==3,"rebinding one descriptor member must replace, not grow, resource tracking");
    expect(snapshot.images[0].image.store_descriptor()==background.store_descriptor() &&
           snapshot.images[1].image.store_descriptor()==replacement.store_descriptor() &&
           snapshot.images[2].image.store_descriptor()==output.store_descriptor(),
           "rebinding one member must preserve the other image identities");
    CommandRecorder again;
    pipeline.record_into(public_pipeline,again);
    auto next=again.close();
    expect(next.commands[0].resources.size()==3,"rebinding must retain the other two image uses");
}

template<typename Image>
void run() {
    if constexpr (!requires(Image image, HardwareExecutor& executor) { image.readback(executor); }) {
        throw std::runtime_error("public image-to-host readback is unavailable");
    } else {
        HardwareExecutor producer;
        HardwareExecutor reader;
        const auto usage = ImageUsage_Sampled | ImageUsage_TransferSrc | ImageUsage_TransferDst;
        {
            Image source(HardwareImageDesc::texture_2d(3,2,Format::RGBA8_UNORM,usage));
            const std::array<uint32_t,6> blank{};
            auto destination=HardwareBuffer::from_bytes(std::as_bytes(std::span(blank)),4,BufferUsage_TransferDst);
            CommandRecorder recorder;
            DispatchDesc sample;
            sample.resource_uses.push_back({source,AccessKind::Read,0});
            recorder.dispatch(std::move(sample));
            recorder.copy_from_image({source},{destination},{});
            const auto plan=ExecutionCompiler{}.compile(recorder.close());
            bool dependency=!plan.dependencies.empty();
            for(const auto& submission:plan.submissions) dependency |= !submission.barriers.empty();
            expect(dependency,"readback layout transition must depend on a preceding shader reader");
        }
        // Asymmetric rows, odd width, and non-primary channels expose stride,
        // vertical flip, channel swaps and accidental display encoding.
        const std::array<unsigned char, 24> rgba{
            1,2,3,255, 64,128,192,255, 255,0,0,255,
            0,255,0,255, 0,0,255,255, 4,5,6,128};
        for (auto format : {Format::RGBA8_UNORM, Format::SRGBA8_UNORM}) {
            auto desc = HardwareImageDesc::texture_2d_array(6,4,2,format,usage);
            desc.mip_levels = 2;
            Image whole(desc);
            auto view = whole.subresource(1,1);
            auto staging = HardwareBuffer::from_bytes(std::as_bytes(std::span(rgba)),1,BufferUsage_TransferSrc);
            const auto receipt = producer.stream() << view.copy_from(staging) << commit();
            expect(receipt.serial != 0, "upload must produce a real submission");
            // No CPU wait on producer: readback must synchronize resource use
            // across executors, and return only when its bytes are host-visible.
            for (int i=0; i<3; ++i) {
                auto host = view.readback(reader);
                expect(host.extent.width==3 && host.extent.height==2 && host.extent.depth==1,
                       "readback must use the selected mip dimensions");
                expect(host.format==format && host.row_pitch==12 && host.slice_pitch==24,
                       "format and tight row/slice pitches must describe returned bytes");
                expect(host.pixels.size()==rgba.size() && std::memcmp(host.pixels.data(),rgba.data(),rgba.size())==0,
                       "readback must preserve rows, channels and encoded bytes");
            }
            auto threaded=std::async(std::launch::async,[view]() mutable {
                HardwareExecutor queue;
                return view.readback(queue);
            });
            auto host=threaded.get();
            expect(host.pixels.size()==rgba.size() && std::memcmp(host.pixels.data(),rgba.data(),rgba.size())==0,
                   "a reader on another thread must see the completed producer image");
        }
        const std::array<uint16_t, 8> half{0,0x3800,0x3c00,0x3c00, 0x3400,0x3a00,0x4000,0x3800};
        const std::array<float, 8> full{0.f,.5f,1.f,1.f, .25f,.75f,2.f,.5f};
        auto check_float = [&](auto& data, Format format) {
            Image image(HardwareImageDesc::texture_2d(1,2,format,usage));
            const auto bytes=std::as_bytes(std::span(data));
            auto staging=HardwareBuffer::from_bytes(bytes,1,BufferUsage_TransferSrc);
            (void)(producer.stream()<<image.copy_from(staging)<<commit());
            auto host=image.readback(reader);
            expect(host.format==format && host.pixels.size()==bytes.size() &&
                   std::memcmp(host.pixels.data(),bytes.data(),bytes.size())==0,
                   "floating output must retain exact bits without gamma or quantization");
            expect(host.row_pitch==bytes.size()/2 && host.extent.width==1 && host.extent.height==2,
                   "floating image layout must use its actual format");
        };
        check_float(half,Format::RGBA16_FLOAT);
        check_float(full,Format::RGBA32_FLOAT);
        bool rejected=false;
        try { (void)Image{}.readback(reader); } catch(const std::exception&) { rejected=true; }
        expect(rejected,"invalid images must fail explicitly");
        Image no_transfer(HardwareImageDesc::texture_2d(2,2,Format::RGBA8_UNORM));
        rejected=false;
        try { (void)no_transfer.readback(reader); } catch(const std::exception&) { rejected=true; }
        expect(rejected,"images without transfer-source usage must fail explicitly");
    }
}

int main() try {
    bindless_composite_tracks_every_image();
    run<HardwareImage>();
    std::cout << "Public image readback GPU tests passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
