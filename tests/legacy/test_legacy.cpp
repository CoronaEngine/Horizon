#include "Horizon_old.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/TypeAlias.h"
#include "Compiler/ShaderUtils.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"

#include <array>
#include <cstdlib>
#include <utility>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <spirv-tools/libspirv.hpp>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace
{
void expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void test_cpu()
{
    using namespace EmbeddedShader;
    int vertexParses = 0, fragmentParses = 0, computeParses = 0;
    auto vertex = [&](Float3 pos) { ++vertexParses; position() = Float4(pos, Float(1)); };
    auto fragment = [&] { ++fragmentParses; return Float4(1, 0, 0, 1); };
    auto stages = RasterizedPipelineObject::compile(vertex, fragment);
    Texture2D<ktm::fvec4> target;
    auto compute = ComputePipelineObject::compile([&] {
        ++computeParses;
        target[dispatchThreadID()->xy()] = Float4(1, 0, 0, 1);
    }, ktm::uvec3(4, 2, 1));
    expect(vertexParses == 1 && fragmentParses == 1 && computeParses == 1, "each EDSL stage is parsed once");
    auto cs = compute.compute->getShaderCode(ShaderLanguage::SpirV);
    auto* storageTable = cs.shaderResources.findShaderBindInfo("textureHandles");
    expect(storageTable && storageTable->set == 2 && storageTable->binding == 0 &&
           cs.shaderResources.pushConstantSize > 0, "default compute SPIR-V uses the bindless storage image table");
    auto spirv = stages.vertex->getShaderCode(ShaderLanguage::SpirV);
    std::string vertexAssembly;
    spvtools::SpirvTools tools(SPV_ENV_VULKAN_1_4);
    expect(tools.Disassemble(std::get<std::vector<uint32_t>>(spirv.shaderCode), &vertexAssembly) &&
           vertexAssembly.find("BuiltIn DrawIndex") != std::string::npos &&
           vertexAssembly.find("BuiltIn VertexIndex") != std::string::npos &&
           vertexAssembly.find("PhysicalStorageBufferAddresses") != std::string::npos,
           "default raster compilation pulls typed vertices from the indirect draw packet");

    bool drawColor = false;
    auto vertexWithDraw = [](Float3 pos) { position() = Float4(pos, rasterDrawParameter<float>(3)); };
    auto conditionalColor = [&] {
        Float4 color;
        color = Float4(1, 1, 1, 1);
        $IF(drawColor) { color = rasterDrawParameter<ktm::fvec4>(0); };
        return color;
    };
    auto variants = RasterizedPipelineObject::compile(vertexWithDraw, conditionalColor);
    for (bool reads : {false, true, false}) {
        drawColor = reads;
        auto vs = variants.vertex->getShaderCode(ShaderLanguage::SpirV);
        auto fs = variants.fragment->getShaderCode(ShaderLanguage::SpirV);
        linkRasterDrawParameter(vs, fs);
        for (const auto* stage : {&vs, &fs}) {
            std::string assembly;
            expect(tools.Disassemble(std::get<std::vector<uint32_t>>(stage->shaderCode), &assembly),
                   "disassemble linked raster stage");
            expect((assembly.find(" Location 15\n") != std::string::npos) == reads,
                   "location 15 in the linked SPIR-V must match the fragment read");
        }
        expect((vs.shaderResources.findShaderBindInfo("hzParameterWord") != nullptr) == reads &&
               (fs.shaderResources.findShaderBindInfo("hzParameterWord") != nullptr) == reads,
               "only fragment variants reading per-draw data receive location 15");
    }

    HardwareBuffer empty;
    HardwareImage image;
    HardwareDisplayer displayer;
    const auto displayerID = displayer.getDisplayerID();
    HardwareDisplayer movedDisplayer(std::move(displayer));
    expect(displayer.getDisplayerID() == 0 && movedDisplayer.getDisplayerID() == displayerID,
           "displayer move construction transfers ownership");
    displayer = std::move(movedDisplayer);
    expect(movedDisplayer.getDisplayerID() == 0 && displayer.getDisplayerID() == displayerID,
           "displayer move assignment transfers ownership");
    expect(!empty && !image, "default resources must be empty");
    auto command = empty.copyTo(empty);
    expect(command.impl->getCommandRecord() == nullptr, "empty copy must not record GPU work");
}

bool has_vulkan_14_device()
{
    if (volkInitialize() != VK_SUCCESS || volkGetInstanceVersion() < VK_API_VERSION_1_4)
        return false;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_4;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    create.pApplicationInfo = &app;
    VkInstance instance = VK_NULL_HANDLE;
    coronaHardwareCheck(vkCreateInstance(&create, nullptr, &instance));
    volkLoadInstance(instance);
    uint32_t count = 0;
    coronaHardwareCheck(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    coronaHardwareCheck(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    bool supported = false;
    for (auto device : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        supported |= properties.apiVersion >= VK_API_VERSION_1_4;
    }
    vkDestroyInstance(instance, nullptr);
    return supported;
}

// Two scissor runs force DrawIndex to restart. Read both colors back after
// the pipeline and geometry handles have gone out of scope.
void test_raster_batch()
{
    using namespace EmbeddedShader;
    HardwareExecutor executor;
    HardwareImage target(4, 2, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage);
    {
        Texture2D<ktm::fvec4> color;
        bool useDrawColor = true;
        auto vs = [&](Float2 pos, Float scale) {
            Float w;
            w = Float(0);
            $IF(useDrawColor) { w = rasterDrawParameter<float>(3); };
            position() = Float4(pos, Float(0), w * scale);
        };
        auto fs = [&] {
            Float4 value;
            value = Float4(0, 0, 0, 1);
            $IF(useDrawColor) { value = rasterDrawParameter<ktm::fvec4>(0); };
            color << value;
        };
        RasterizerPipeline pipeline(vs, fs);
        color = target;
        pipeline.setDepthEnabled(false);
        auto batch = pipeline.beginBatch(4, 2);
        const std::array<std::array<float, 3>, 3> vertices{{{-1,-1,1}, {3,-1,1}, {-1,3,1}}};
        HardwareBuffer vb(vertices, BufferUsage::VertexBuffer);
        RasterGeometry left{vb, HardwareBuffer(std::array<uint16_t,3>{0,1,2}, BufferUsage::IndexBuffer)};
        RasterGeometry right{vb, HardwareBuffer(std::array<uint32_t,4>{99,1,2,3}, BufferUsage::IndexBuffer)};
        batch.draw(left, {.scissor = ScissorRect{0,0,2,2}}, std::array<float,4>{1,0,0,1});
        batch.draw(right, {.indexCount=3, .firstIndex=1, .vertexOffset=-1, .instanceCount=2,
                           .scissor = ScissorRect{2,0,2,2}}, std::array<float,4>{0,1,0,1});
        executor << std::move(batch);
    }
    std::array<std::array<float,4>,8> pixels{};
    HardwareBuffer readback(sizeof(pixels), BufferUsage::StorageBuffer);
    executor << target.copyTo(readback);
    executor.commit().waitForDeferredResources();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)), "raster readback");
    for (size_t i = 0; i < pixels.size(); ++i) {
        const bool left = i % 4 < 2;
        expect(pixels[i] == std::array<float,4>{float(left), float(!left), 0, 1},
               "indirect scissor run, index range or per-fragment parameter mismatch");
    }
}

void test_raster_sampled_storage_image()
{
    using namespace EmbeddedShader;
    using Pixel = std::array<float, 4>;
    const std::array<Pixel, 1> source{{{0.25f, 0.5f, 0.75f, 1.0f}}};
    HardwareImage input(1, 1, ImageFormat::RGBA32_FLOAT, ImageUsage::SampledStorageImage);
    HardwareImage target(1, 1, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage);
    HardwareBuffer upload(source, BufferUsage::StorageBuffer);
    HardwareBuffer readback(sizeof(source), BufferUsage::StorageBuffer);
    Texture2D<ktm::fvec4> texture = input, output = target;
    auto vs = [](Float2 pos) { position() = Float4(pos, Float(0), Float(1)); };
    auto fs = [&] { output << texture.sample(Float2(0.5f)); };
    RasterizerPipeline pipeline(vs, fs);
    pipeline.setDepthEnabled(false);
    const std::array<std::array<float, 2>, 3> vertices{{{-1, -1}, {3, -1}, {-1, 3}}};
    RasterGeometry mesh{HardwareBuffer(vertices, BufferUsage::VertexBuffer),
                        HardwareBuffer(std::array<uint16_t, 3>{0, 1, 2}, BufferUsage::IndexBuffer)};
    auto batch = pipeline.beginBatch(1, 1);
    batch.draw(mesh);
    HardwareExecutor executor;
    executor << upload.copyTo(input) << std::move(batch) << target.copyTo(readback);
    executor.commit().waitForDeferredResources();
    std::array<Pixel, 1> pixel{};
    expect(readback.copyToData(pixel.data(), sizeof(pixel)) && pixel == source,
           "fragment UBO handle samples a sampled/storage image from set 0");
}

// A compute-written packet draws the right half at the depth left by pass one.
// Clearing either attachment or losing the packet between stages changes a pixel.
void test_generated_packet_and_attachment_load()
{
    using namespace EmbeddedShader;
    HardwareImage target(4, 2, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage);
    HardwareImage depth(4, 2, ImageFormat::D32_FLOAT, ImageUsage::DepthImage);
    Texture2D<ktm::fvec4> output = target;
    auto vs = [] {
        position() = Float4(rasterVertexAttribute<ktm::fvec2>(0), Float(0.5), Float(1));
    };
    auto fs = [&] { output << rasterDrawParameter<ktm::fvec4>(0); };
    RasterizerPipeline pipeline(vs, fs);
    pipeline.setDepthImage(depth);
    pipeline.setRasterState({true, true, RasterCompare::Less, RasterBlend::Opaque});

    const std::array<std::array<float,2>,7> vertices{{{-1,-1},{3,-1},{-1,3},
                                                       {0,-1},{1,-1},{1,1},{0,1}}};
    const std::array<uint16_t,9> indices{0,1,2,3,4,5,3,5,6};
    RasterGeometry mesh{HardwareBuffer::uploadDeviceLocal(vertices, BufferUsage::VertexBuffer),
                        HardwareBuffer::uploadDeviceLocal(indices, BufferUsage::IndexBuffer)};
    auto first = pipeline.beginBatch(4, 2);
    first.draw(mesh, {.indexCount=3}, std::array<float,4>{1,0,0,1});
    HardwareBuffer packets(std::array<RasterIndirectPacket,1>{}, BufferUsage::RasterIndirect);
    EmbeddedShader::ArrayProxy<uint32_t> words(packets);
    ComputePipelineBase generate([&] {
        words[0] = Uint(6);
        words[1] = Uint(1);
        words[5] = Uint(3);
        words[7] = Uint(4);
    });
    generate.uses({{packets, ResourceAccess::Write}});
    pipeline.setRasterState({true, false, RasterCompare::Equal, RasterBlend::Additive});
    auto second = pipeline.beginBatch(4, 2, {true, true});
    const std::array<float,8> colors{0,0,1,0, 0,1,0,0};
    second.drawIndirect(mesh, packets, 1, std::span(colors));

    HardwareExecutor executor;
    std::array<std::array<float,4>,8> pixels{};
    HardwareBuffer readback(sizeof(pixels), BufferUsage::StorageBuffer);
    executor << std::move(first) << generate(1,1,1) << std::move(second) << target.copyTo(readback);
    executor.commit().waitForDeferredResources();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)), "multi-pass readback");
    for (size_t i = 0; i < pixels.size(); ++i) {
        expect(pixels[i] == std::array<float,4>{1, float(i % 4 >= 2), 0, 1},
               "GPU-generated range, attachment load, equal depth or additive blend failed");
    }
}

void test_compute_sampling()
{
    using namespace EmbeddedShader;
    using Pixel = std::array<float, 4>;
    const std::array<Pixel, 4> source{{{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1}}};
    HardwareBuffer upload(source, BufferUsage::StorageBuffer);
    std::array<Pixel, 16> base{};
    HardwareBuffer uploadBase(base, BufferUsage::StorageBuffer);
    HardwareImageCreateInfo info(4, 4, ImageFormat::RGBA32_FLOAT);
    info.mipLevels = 2;
    info.usage = ImageUsage::SampledStorageImage;
    HardwareImage input(info);
    HardwareImage result(2, 2, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage);
    HardwareBuffer readback(sizeof(source), BufferUsage::StorageBuffer);
    Texture2D<ktm::fvec4> tex = input, dst = result;
    Sampler sampler;
    auto shader = [&] {
        auto xy = dispatchThreadID()->xy();
        dst[xy] = tex.sample(sampler, (Float2(xy) + Float2(0.5f)) / Float(2));
    };
    CompilerOption options;
    options.compileHLSL = false;
    (void)ComputePipelineObject::compile(shader, ktm::uvec3(2, 2, 1), options);
    HardwareExecutor executor;
    std::array<Pixel, 4> pixels{};
    executor << uploadBase.copyTo(input) << upload.copyTo(input, 0, 0, 1);
    if (getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils().computeDerivativeGroupQuads) {
        // A 2x2 quad over a 4x4 image selects the colored mip.
        ComputePipelineBase pipeline(shader, ktm::uvec3(2, 2, 1), options);
        executor << pipeline(1, 1, 1) << result.copyTo(readback);
        executor.commit().waitForDeferredResources();
        expect(readback.copyToData(pixels.data(), sizeof(pixels)) && pixels == source, "explicit compute sampler mip readback");
    } else {
        std::cerr << "Skipping implicit-LOD readback on a device without compute quad derivatives\n";
    }

    auto mipZero = input[0];
    tex = mipZero;
    ComputePipelineBase load([&] {
        auto xy = dispatchThreadID()->xy();
        dst[xy] = tex[xy];
    }, ktm::uvec3(2, 2, 1), options);
    executor << load(1, 1, 1) << result.copyTo(readback);
    executor.commit().waitForDeferredResources();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)) && pixels == std::array<Pixel, 4>{},
           "the same texture proxy can be used for storage reads in a later pipeline");

    HardwareImageCreateInfo cubeInfo(2, 2, ImageFormat::RGBA32_FLOAT);
    cubeInfo.cube = true;
    cubeInfo.arrayLayers = 6;
    cubeInfo.mipLevels = 2;
    HardwareImage cubeImage(cubeInfo);
    TextureCube<ktm::fvec4> cube = cubeImage;
    ComputePipelineBase sampleCube([&] {
        dst[dispatchThreadID()->xy()] = cube.sampleLevel(sampler, Float3(1, 0, 0), Float(1));
    }, ktm::uvec3(2, 2, 1), options);
    executor << upload.copyTo(cubeImage, 0, 0, 1) << sampleCube(1, 1, 1) << result.copyTo(readback);
    executor.commit().waitForDeferredResources();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)) && std::ranges::all_of(pixels, [&](Pixel p) { return p == source[0]; }),
           "explicit compute cube sampler reads the requested mip");
}

void test_gpu()
{
    std::cerr << "GPU: initializing buffers\n";
    const std::array<uint32_t, 8> input{10, 20, 30, 40, 50, 60, 70, 80};
    const std::array<uint32_t, 8> zeros{};
    HardwareBuffer source(input, BufferUsage::StorageBuffer);
    HardwareBuffer destination(zeros, BufferUsage::StorageBuffer);
    HardwareExecutor executor;
    std::cerr << "GPU: buffer copy/readback\n";
    executor << source.copyTo(destination);
    executor.commit().waitForDeferredResources();
    std::array<uint32_t, 8> output{};
    expect(destination.copyToData(output.data(), sizeof(output)), "buffer readback");
    expect(output == input, "GPU buffer round trip");

    // Temporary source wrapper must remain alive until its queued copy completes.
    std::cerr << "GPU: deferred resource lifetime\n";
    executor << HardwareBuffer(input, BufferUsage::StorageBuffer).copyTo(destination);
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)) && output == input,
           "deferred copy retains temporary resources");

    expect(destination.copyFromData(zeros.data(), sizeof(zeros)), "clear destination");
    executor << source.copyTo(destination, sizeof(uint32_t), 3 * sizeof(uint32_t), 2 * sizeof(uint32_t));
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)), "partial copy readback");
    expect(output == std::array<uint32_t, 8>{0, 0, 0, 20, 30, 0, 0, 0}, "copy preserves byte offsets and size");

    executor << destination.copyTo(destination, 3 * sizeof(uint32_t), 0, 2 * sizeof(uint32_t));
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)), "self copy readback");
    expect(output == std::array<uint32_t, 8>{20, 30, 0, 20, 30, 0, 0, 0}, "disjoint self copy must not deadlock");
    bool rejected = false;
    try { executor << source.copyTo(destination, 0, sizeof(output), sizeof(uint32_t)); }
    catch (const std::out_of_range&) { rejected = true; }
    expect(rejected, "out of bounds copy must be rejected before recording");

    std::cerr << "GPU: image mip/layer round trip\n";
    HardwareImageCreateInfo imageInfo(4, 4, ImageFormat::RGBA8_UINT);
    imageInfo.arrayLayers = 2;
    imageInfo.mipLevels = 2;
    HardwareImage image(imageInfo);
    HardwareImage imageCopy(imageInfo);
    EmbeddedShader::ParseHelper::beginNotInitNode();
    EmbeddedShader::TextureCubeProxy<ktm::fvec4> cube;
    EmbeddedShader::ParseHelper::endNotInitNode();
    HardwareImageCreateInfo cubeInfo(4, 4);
    cubeInfo.cube = true;
    cubeInfo.arrayLayers = 6;
    cube.createResource(cubeInfo);
    expect(cube.resource() == &cube.image() && static_cast<bool>(cube.image()),
           "cube proxy binds its owned Horizon image");
    {
        HardwareImage owner(imageInfo);
        auto view = owner[1];
        owner = HardwareImage{};
        expect(static_cast<bool>(view), "image view retains its allocation after owner destruction");
    }
    expect(destination.copyFromData(zeros.data(), sizeof(zeros)), "clear image destination");
    executor << source.copyTo(image, sizeof(uint32_t), 1, 1);
    executor << image.copyTo(imageCopy, 1, 0, 1, 1);
    executor << imageCopy.copyTo(destination, 0, 1, 2 * sizeof(uint32_t));
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)), "image readback");
    expect(output == std::array<uint32_t, 8>{0, 0, 20, 30, 40, 50, 0, 0},
           "image copies preserve layers, mips and buffer offsets");

    EmbeddedShader::CompilerOption options;
    std::cerr << "GPU: Slang compilation/compute dispatch\n";
    options.compileGLSL = false;
    options.compileHLSL = false;
    EmbeddedShader::ShaderCodeCompiler compiler(
        "[shader(\"compute\")] [numthreads(1,1,1)] void main() {}",
        EmbeddedShader::ShaderStage::ComputeShader, options);
    auto module = compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV);
    expect(!std::get<std::vector<uint32_t>>(module.shaderCode).empty(), "current Slang emits SPIR-V");
    ComputePipelineBase pipeline(module);
    executor << pipeline(1, 1, 1);
    executor.commit();

    std::cerr << "GPU: EDSL compute\n";
    ComputePipelineBase edsl([] {}, ktm::uvec3(1), options);
    executor << edsl(1, 1, 1);
    executor.commit();

    EmbeddedShader::SlangModuleCompileArgs moduleArgs;
    std::cerr << "GPU: generated Slang module\n";
    moduleArgs.moduleName = "legacy_test";
    moduleArgs.sourceLanguage = EmbeddedShader::ShaderLanguage::Slang;
    moduleArgs.shaderCode = "[shader(\"compute\")] [numthreads(1,1,1)] void main() {}";
    auto slangModule = EmbeddedShader::ShaderLanguageConverter::slangModuleCompiler(moduleArgs);
    ComputePipelineBase generated(compileLegacyShaderModule(EmbeddedShader::ShaderStage::ComputeShader, slangModule));
    executor << generated(1, 1, 1);
    executor.commit();
    coronaHardwareCheck(vkDeviceWaitIdle(getHardwareContext().getMainDevice()->deviceManager.getLogicalDevice()));
}
}

int main(int argc, char** argv)
{
#if defined(_MSC_VER) && defined(_DEBUG)
    // Report native assertions to CTest instead of opening an unattended dialog.
    _set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--gpu")
        {
            if (!has_vulkan_14_device())
            {
                std::cout << "SKIP: no Vulkan 1.4 loader/device\n";
                return 77;
            }
            test_gpu();
            test_raster_batch();
            test_raster_sampled_storage_image();
            test_generated_packet_and_attachment_load();
            test_compute_sampling();
        }
        else
            test_cpu();
        std::cout << "Legacy API checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
