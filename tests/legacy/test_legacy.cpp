#include "Horizon.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/TypeAlias.h"
#include "Compiler/ShaderUtils.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"

#include <array>
#include <cstdlib>
#include <utility>
#include <iostream>
#include <iterator>
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

template<EmbeddedShader::ShaderStage stage>
struct GeneratedStage
{
    static inline EmbeddedShader::SlangModule slangModule;
    static constexpr size_t uniformBufferBlockSize = stage == EmbeddedShader::ShaderStage::FragmentShader ? sizeof(float) : 0;

    template<class Pipeline>
    struct Bindings
    {
        explicit Bindings(Pipeline* pipeline)
            : output(pipeline, 0, sizeof(uint32_t), EmbeddedShader::ShaderCodeModule::ShaderResources::pushConstantMembers, 0) {}
        EmbeddedShader::BoundField<Pipeline> output;
    };

    template<class Pipeline>
    struct ResourceBindings
    {
        struct { EmbeddedShader::BoundField<Pipeline> view; } vp;
        explicit ResourceBindings(Pipeline* pipeline)
            : vp{{pipeline, 0, sizeof(float), EmbeddedShader::ShaderCodeModule::ShaderResources::uniformBufferMembers, 0}} {}
    };

    template<class Pipeline>
    struct OutputBindings
    {
        EmbeddedShader::BoundField<Pipeline> outColor;
        explicit OutputBindings(Pipeline* pipeline)
            : outColor(pipeline, 0, 0, EmbeddedShader::ShaderCodeModule::ShaderResources::stageOutputs, 0) {}
    };
};

void test_cpu()
{
    DeviceFeaturesChain original;
    DeviceFeaturesChain copy = original;
    expect(original.getChainHead()->pNext != copy.getChainHead()->pNext, "copied Vulkan features own their chain");
    original = copy;
    expect(original.getChainHead()->pNext != copy.getChainHead()->pNext, "assigned Vulkan features own their chain");
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
    expect(!displayer.present({}, 0, 0, ImageFormat::RGBA8_UNORM), "an empty pixel frame is skipped");
    const std::array<uint32_t, 1> pixel{0xff00ff00u};
    bool rejected = false;
    try { displayer.present(pixel, 2, 1, ImageFormat::RGBA8_UNORM); }
    catch (const std::invalid_argument&) { rejected = true; }
    expect(rejected, "pixel count must match the extent");
    rejected = false;
    try { displayer.present(pixel, 1, 1, ImageFormat::RGBA16_FLOAT); }
    catch (const std::invalid_argument&) { rejected = true; }
    expect(rejected, "pixel presentation rejects a format incompatible with RGBA8 data");
    expect(!empty && !image, "default resources must be empty");
    expect(!CopyCommand{} && !image.copyFrom(nullptr), "default command and null upload must be empty");
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

// Submit after the pipeline, geometry, draw parameters and output proxies go out of scope.
void test_raster_batch(bool forceSingleDraw)
{
    using namespace EmbeddedShader;
    HardwareExecutor executor;
    HardwareImage target(4, 2, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage);
    {
        HardwareImage reflectedTarget(4, 2, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage);
        Texture2D<ktm::fvec4> color = reflectedTarget, overrideColor = target;
        bool useDrawColor = true;
        auto vs = [&](Float2 pos, Float scale) {
            Float w;
            w = Float(1);
            $IF(useDrawColor) { w = rasterDrawParameter<float>(3); };
            position() = Float4(pos, Float(0), w * scale);
            return Float4(0, 0, drawIndex(), 0);
        };
        auto fs = [&](Float4 drawColorOffset) {
            Float4 value;
            value = Float4(0, 0, 1, 1);
            $IF(useDrawColor) { value = rasterDrawParameter<ktm::fvec4>(0); };
            color << value + drawColorOffset;
        };
        RasterizerPipeline pipeline(vs, fs);
        pipeline.bindRenderTarget(0, overrideColor);
        pipeline.setDepthEnabled(false);
        auto batch = pipeline.beginBatch(4, 2);
        // One triangle covers the screen, the other covers only the second pixel row.
        const std::array<std::array<float, 3>, 6> vertices{{
            {-1,-1,1}, {3,-1,1}, {-1,3,1}, {-1,0,1}, {3,0,1}, {-1,2,1}
        }};
        HardwareBuffer leftVertices(std::span(vertices).first<3>(), BufferUsage::VertexBuffer);
        HardwareBuffer rightVertices(vertices, BufferUsage::VertexBuffer);
        HardwareBuffer leftIndices(std::array<uint16_t,3>{0,1,2}, BufferUsage::IndexBuffer);
        HardwareBuffer rightIndices(std::array<uint32_t,7>{99,1,2,3,4,5,6}, BufferUsage::IndexBuffer);
        HardwareBuffer leftCommands(std::array{RasterIndirectPacket{.vertexCount=3, .instanceCount=1}},
                                    BufferUsage::RasterIndirect);
        HardwareBuffer rightCommands(std::array{
            RasterIndirectPacket{.vertexCount=3, .instanceCount=2, .firstIndex=1, .baseVertex=-1},
            RasterIndirectPacket{.vertexCount=3, .instanceCount=1, .firstIndex=4, .baseVertex=-1, .parameterWord=4}
        }, BufferUsage::RasterIndirect);
        HardwareBuffer red(std::array<float,4>{1,0,0,1}, BufferUsage::StorageBuffer);
        HardwareBuffer greens(std::array<std::array<float,4>,2>{{{0,1,0,1}, {0,2,0,1}}},
                              BufferUsage::StorageBuffer);
        batch.drawIndirect(leftVertices, leftIndices, leftCommands, 1, red, ScissorRect{0,0,2,2});
        batch.drawIndirect({}, {}, {}, 0);
        batch.drawIndirect(leftVertices, leftIndices, leftCommands, 1, red, ScissorRect{-3,0,2,2});
        batch.drawIndirect(rightVertices, rightIndices, rightCommands, 2, greens, ScissorRect{2,0,2,2});
        useDrawColor = false;
        auto switched = pipeline.beginBatch({.loadColor = true});
        switched.drawIndirect(leftVertices, leftIndices, leftCommands, 1, {}, ScissorRect{3,0,1,2});
        executor << std::move(batch) << std::move(switched);
    }
    std::array<std::array<float,4>,8> pixels{};
    HardwareBuffer readback(sizeof(pixels), BufferUsage::StorageBuffer);
    executor << target.copyTo(readback);
    auto& multiDraw = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils()
                          .featuresChain.getChainHead()->features.multiDrawIndirect;
    const auto supported = multiDraw;
    if (forceSingleDraw) multiDraw = VK_FALSE;
    executor.commit().waitForDeferredResources();
    multiDraw = supported;
    expect(readback.copyToData(pixels.data(), sizeof(pixels)), "raster readback");
    const std::array<std::array<float,4>,8> expected{{
        {1,0,0,1}, {1,0,0,1}, {0,1,1,1}, {0,0,1,1},
        {1,0,0,1}, {1,0,0,1}, {0,2,2,1}, {0,0,1,1}
    }};
    expect(pixels == expected, "indirect groups must honor scissor, local packet and parameter offsets, "
           "batch-wide draw index and the captured shader variant");
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
    HardwareBuffer vertexBuffer(vertices, BufferUsage::VertexBuffer);
    HardwareBuffer indexBuffer(std::array<uint16_t, 3>{0, 1, 2}, BufferUsage::IndexBuffer);
    HardwareBuffer commands(std::array{RasterIndirectPacket{.vertexCount=3, .instanceCount=1}},
                            BufferUsage::RasterIndirect);
    auto batch = pipeline.beginBatch(1, 1);
    batch.drawIndirect(vertexBuffer, indexBuffer, commands, 1);
    HardwareExecutor executor;
    executor << upload.copyTo(input) << std::move(batch) << target.copyTo(readback);
    executor.commit().waitForDeferredResources();
    std::array<Pixel, 1> pixel{};
    expect(readback.copyToData(pixel.data(), sizeof(pixel)) && pixel == source,
           "fragment UBO handle samples a sampled/storage image from set 0");

    target.setClearColor(0, 1, 0, 1);
    auto clear = pipeline.beginBatch(1, 1);
    clear.drawIndirect({}, {}, {}, 0);
    executor << std::move(clear) << target.copyTo(readback);
    executor.commit().waitForDeferredResources();
    expect(readback.copyToData(pixel.data(), sizeof(pixel)) && pixel[0] == Pixel{0,1,0,1},
           "an empty indirect pass still clears its attachment");
}

// Compute supplies the indirect command, color and depth for the second pass.
void test_generated_packet_and_attachment_load(bool explicitDepth)
{
    using namespace EmbeddedShader;
    HardwareImageCreateInfo targetInfo(4, 2, ImageFormat::RGBA32_FLOAT);
    targetInfo.usage = ImageUsage::StorageImage;
    targetInfo.mipLevels = 2;
    HardwareImage target(targetInfo);
    HardwareImage depth(4, 2, ImageFormat::D32_FLOAT, ImageUsage::DepthImage);
    Texture2D<ktm::fvec4> output = target;
    auto vs = [] {
        position() = Float4(rasterVertexAttribute<ktm::fvec2>(0), rasterDrawParameter<float>(4), Float(1));
        return Float4(0, 0, instanceIndex(), 0);
    };
    auto fs = [&](Float4 instanceColor) { output << rasterDrawParameter<ktm::fvec4>(0) + instanceColor; };
    RasterizerPipeline pipeline(vs, fs);
    if (explicitDepth) pipeline.setDepthImage(depth);
    pipeline.setRasterState({true, true, RasterCompare::Less, RasterBlend::Opaque});

    const std::array<float,2> vertices[]{{-1,-1},{3,-1},{-1,3},{0,-1},{1,-1},{1,1},{0,1}};
    const std::array<uint16_t,9> indices{0,1,2,3,4,5,3,5,6};
    HardwareExecutor upload;
    HardwareBuffer vertexBuffer;
    vertexBuffer.createDeviceLocal(upload, BufferUsage::VertexBuffer, vertices, std::size(vertices), sizeof(vertices[0]));
    const auto vertexID = vertexBuffer.getBufferID();
    HardwareBuffer indexBuffer = vertexBuffer;
    auto expectInvalidUpload = [&](BufferUsage usage, const void* data, size_t elementSize) {
        try { indexBuffer.createDeviceLocal(upload, usage, data, 1, elementSize); }
        catch (const std::invalid_argument&) {
            expect(indexBuffer.getBufferID() == vertexID, "invalid upload preserves the original buffer");
            return;
        }
        throw std::runtime_error("invalid upload was accepted");
    };
    expectInvalidUpload(BufferUsage::IndexBuffer, nullptr, sizeof(uint16_t));
    expectInvalidUpload(BufferUsage::IndexBuffer, indices.data(), 1);
    expectInvalidUpload(BufferUsage::VertexBuffer, vertices, 2);
    if (explicitDepth) {
        indexBuffer.createDeviceLocal(upload, BufferUsage::IndexBuffer, indices.data(), indices.size(), sizeof(indices[0]));
    } else {
        const std::vector<uint32_t> wideIndices(indices.begin(), indices.end());
        indexBuffer.createDeviceLocal(upload, BufferUsage::IndexBuffer, wideIndices.data(), wideIndices.size(), sizeof(wideIndices[0]));
    }
    upload.commit().waitForDeferredResources();
    expect(vertexBuffer.getBufferID() == vertexID && indexBuffer.getBufferID() != vertexID,
           "recreating one buffer handle preserves the allocation held by its copy");
    expect(vertexBuffer.getElementCount() == std::size(vertices) &&
           vertexBuffer.getElementSize() == sizeof(vertices[0]) &&
           indexBuffer.getElementCount() == indices.size() &&
           indexBuffer.getElementSize() == (explicitDepth ? sizeof(uint16_t) : sizeof(uint32_t)),
           "staged raster buffers preserve element counts, vertex stride and index width");
    auto first = pipeline.beginBatch(4, 2);
    HardwareBuffer firstCommands(std::array{RasterIndirectPacket{.vertexCount=3, .instanceCount=1}},
                                 BufferUsage::RasterIndirect);
    HardwareBuffer redAndDepth(std::array<float,5>{1,0,0,1,0.5f}, BufferUsage::StorageBuffer);
    first.drawIndirect(vertexBuffer, indexBuffer, firstCommands, 1, redAndDepth);
    HardwareBuffer packets(std::array<RasterIndirectPacket,1>{}, BufferUsage::RasterIndirect);
    const auto& features = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils()
                               .featuresChain.getChainHead()->features;
    const uint32_t firstInstance = features.drawIndirectFirstInstance ? 7u : 0u;
    HardwareBuffer generatedParameters(3, sizeof(ktm::fvec4), BufferUsage::StorageBuffer);
    EmbeddedShader::ArrayProxy<uint32_t> words(packets);
    EmbeddedShader::ArrayProxy<ktm::fvec4> parameters(generatedParameters);
    HardwareExecutor executor;
    executor << std::move(first);
    {
        ComputePipeline generate([&] {
            words[0] = Uint(6);
            words[1] = Uint(2);
            words[3] = Uint(firstInstance);
            words[5] = Uint(3);
            words[7] = Uint(4);
            parameters[0] = Float4(0, 0, 1, 0);
            parameters[1] = Float4(0, 1, 0, 0);
            parameters[2] = Float4(0.5f, 0, 0, 0);
        });
        generate.uses({{packets, ResourceAccess::Write}, {generatedParameters, ResourceAccess::Write}});
        executor << generate(1,1,1);
    }
    pipeline.setRasterState({true, false, RasterCompare::Equal, RasterBlend::Additive});
    auto second = pipeline.beginBatch(4, 2, {true, true});
    second.drawIndirect(vertexBuffer, indexBuffer, packets, 1, generatedParameters);

    std::array<std::array<float,4>,8> pixels{};
    HardwareBuffer readback(sizeof(pixels), BufferUsage::StorageBuffer);
    executor << std::move(second) << target.copyTo(readback);
    executor.commit().waitForDeferredResources();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)), "multi-pass readback");
    // Both instances contribute green and their absolute instance indices to blue.
    const float instanceSum = static_cast<float>(firstInstance + (firstInstance + 1));
    const std::array<float,4> left{1,0,0,1}, right{1,2,instanceSum,1};
    for (size_t i = 0; i < pixels.size(); ++i) {
        expect(pixels[i] == (i % 4 < 2 ? left : right),
               "GPU-generated command, instance indices, color, depth or attachment load failed");
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
        ComputePipeline pipeline(shader, ktm::uvec3(2, 2, 1), options);
        executor << pipeline(1, 1, 1) << result.copyTo(readback);
        executor.commit().waitForDeferredResources();
        expect(readback.copyToData(pixels.data(), sizeof(pixels)) && pixels == source, "explicit compute sampler mip readback");
    } else {
        std::cerr << "Skipping implicit-LOD readback on a device without compute quad derivatives\n";
    }

    auto mipZero = input[0];
    tex = mipZero;
    ComputePipeline load([&] {
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
    ComputePipeline sampleCube([&] {
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
    auto* backend = reinterpret_cast<HardwareExecutorVulkan*>(executor.getExecutorID());
    HardwareBuffer emptyBuffer;
    HardwareImage emptyImage;
    executor << emptyBuffer.copyTo(emptyBuffer) << emptyBuffer.copyTo(emptyImage)
             << emptyImage.copyTo(emptyImage) << emptyImage.copyTo(emptyBuffer);
    expect(backend->commandList.empty(), "copies between empty resources must not record GPU work");
    {
        HardwareDisplayer display;
        HardwareImage target(4, 2, ImageFormat::RGBA8_UNORM);
        executor << source.copyTo(target);
        expect(!display.present(executor, target) && backend->commandList.empty() && backend->getSubmissionPoint(),
               "headless presentation commits pending work even when the frame is skipped");
        expect(!display.present(input, 4, 2, ImageFormat::RGBA8_UNORM), "headless pixel presentation skips the frame");
        HardwareDisplayer moved(std::move(display));
        expect(!moved.present(input, 4, 2, ImageFormat::RGBA8_SRGB), "a moved displayer accepts a new pixel format");
        display = std::move(moved);
        expect(!display.present(input, 2, 4, ImageFormat::RGBA8_SRGB), "a moved displayer accepts a new pixel extent");
    }
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
    std::array<uint32_t, 2> layers{0x11223344, 0x55667788};
    HardwareImage initialized(1, 1, ImageFormat::RGBA8_UINT, ImageUsage::SampledImage, 2, layers.data());
    executor << initialized.copyTo(destination, 1);
    executor.commit().waitForDeferredResources();
    expect(destination.copyToData(output.data(), sizeof(output)) && output[0] == layers[1],
           "initial image upload retains staging and copies the second array layer");
    VkFormatProperties bc1{};
    vkGetPhysicalDeviceFormatProperties(getHardwareContext().getMainDevice()->deviceManager.getPhysicalDevice(),
                                        VK_FORMAT_BC1_RGB_UNORM_BLOCK, &bc1);
    if (bc1.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
        auto blocks = std::array<uint32_t,4>{1,2,3,4};
        HardwareImage compressed(5, 3, ImageFormat::BC1_RGB_UNORM, ImageUsage::SampledImage, 1, blocks.data());
        executor << compressed.copyTo(destination);
        executor.commit().waitForDeferredResources();
        expect(destination.copyToData(output.data(), sizeof(output)) && std::ranges::equal(blocks, std::span(output).first(4)),
               "odd-sized BC1 upload rounds up to two whole blocks");
    }
    struct RetryCommand : CommandRecordVulkan {
        bool fail = true;
        ExecutorType getExecutorType() override { return ExecutorType::Graphics; }
        void resolveResourceUses(std::span<const ResourceUse>, ResourceAccessList&) override {}
        void commitCommand(HardwareExecutorVulkan&) override {
            if (fail) throw std::runtime_error("injected recording failure");
        }
    } retry;
    const auto previous = backend->getSubmissionPoint();
    const auto waitCount = backend->waitSemaphores.size();
    backend->wait(previous).record(&retry);
    try { backend->commit(); } catch (const std::runtime_error&) {}
    expect(backend->commandList.size() == 1 && backend->getSubmissionPoint().queue == previous.queue &&
           backend->getSubmissionPoint().value == previous.value && backend->waitSemaphores.size() == waitCount,
           "failed recording preserves the previous completion point and does not duplicate waits");
    retry.fail = false;
    backend->commit().waitForAllDeferredResources();
    expect(backend->commandList.empty(), "retry consumes the recorded command");
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

    executor << imageCopy.copyFrom(input.data(), 0, 1) << imageCopy.copyTo(destination, 0, 1);
    executor.commit().waitForDeferredResources();
    std::array<uint32_t, 4> mipPixels{};
    expect(destination.copyToData(mipPixels.data(), sizeof(mipPixels)) &&
           std::ranges::equal(mipPixels, std::span(input).first(mipPixels.size())),
           "staged upload retains its buffer and writes the selected image mip");

    EmbeddedShader::CompilerOption options;
    std::cerr << "GPU: Slang compilation/compute dispatch\n";
    options.compileGLSL = false;
    options.compileHLSL = false;
    EmbeddedShader::ShaderCodeCompiler compiler(
        "[shader(\"compute\")] [numthreads(1,1,1)] void main() {}",
        EmbeddedShader::ShaderStage::ComputeShader, options);
    auto module = compiler.getShaderCode(EmbeddedShader::ShaderLanguage::SpirV);
    expect(!std::get<std::vector<uint32_t>>(module.shaderCode).empty(), "current Slang emits SPIR-V");
    ComputePipeline pipeline(module);
    executor << pipeline(1, 1, 1);
    executor.commit();

    std::cerr << "GPU: EDSL compute\n";
    ComputePipeline edsl([] {}, ktm::uvec3(1), options);
    executor << edsl(1, 1, 1);
    executor.commit();

    EmbeddedShader::SlangModuleCompileArgs moduleArgs;
    std::cerr << "GPU: generated Slang module\n";
    moduleArgs.moduleName = "legacy_test";
    moduleArgs.sourceLanguage = EmbeddedShader::ShaderLanguage::Slang;
    moduleArgs.shaderCode = R"(
        struct Parameters { uint outputIndex; };
        [[vk::push_constant]] ConstantBuffer<Parameters> params;
        [[vk::binding(0, 1)]] RWStructuredBuffer<uint> outputs[];
        [shader("compute")] [numthreads(1,1,1)]
        void main() { outputs[params.outputIndex][0] = 47; }
    )";
    using Compute = GeneratedStage<EmbeddedShader::ShaderStage::ComputeShader>;
    Compute::slangModule = EmbeddedShader::ShaderLanguageConverter::slangModuleCompiler(moduleArgs);
    ComputePipeline generated(Compute{});
    static_assert(std::same_as<decltype(generated), ComputePipeline<Compute>>);
    static_assert(!std::is_base_of_v<ComputePipeline<>, decltype(generated)>);
    auto moved = std::move(generated);
    moved.output = destination;
    executor << moved(1);
    generated = std::move(moved);
    generated.output = source;
    executor << generated(1);
    executor.commit().waitForDeferredResources();
    uint32_t afterConstruction{}, afterAssignment{};
    expect(destination.copyToData(&afterConstruction, sizeof(afterConstruction)) && afterConstruction == 47 &&
           source.copyToData(&afterAssignment, sizeof(afterAssignment)) && afterAssignment == 47,
           "generated fields bind independent outputs after move construction and assignment");

    std::cerr << "GPU: generated raster fields after move\n";
    using Vertex = GeneratedStage<EmbeddedShader::ShaderStage::VertexShader>;
    using Fragment = GeneratedStage<EmbeddedShader::ShaderStage::FragmentShader>;
    moduleArgs.moduleName = "legacy_vertex";
    moduleArgs.shaderCode = R"(
        [shader("vertex")] float4 main(uint id : SV_VertexID) : SV_Position {
            return float4(float2((id << 1) & 2, id & 2) * 2.0 - 1.0, 0, 1);
        }
    )";
    Vertex::slangModule = EmbeddedShader::ShaderLanguageConverter::slangModuleCompiler(moduleArgs);
    moduleArgs.moduleName = "legacy_fragment";
    moduleArgs.shaderCode = R"(
        struct Parameters { float view; };
        [[vk::binding(0, 3)]] ConstantBuffer<Parameters> vp;
        [shader("fragment")] void main(out float4 outColor : SV_Target0) {
            outColor = float4(vp.view, 0, 0, 1);
        }
    )";
    Fragment::slangModule = EmbeddedShader::ShaderLanguageConverter::slangModuleCompiler(moduleArgs);
    RasterizerPipeline raster(Vertex{}, Fragment{});
    static_assert(std::same_as<decltype(raster), RasterizerPipeline<Vertex, Fragment>>);
    static_assert(!std::is_base_of_v<RasterizerPipeline<>, decltype(raster)>);
    HardwareImage first(1, 1, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage);
    HardwareImage second(1, 1, ImageFormat::RGBA32_FLOAT, ImageUsage::StorageImage);
    HardwareBuffer vertices(std::array<float, 3>{}, BufferUsage::VertexBuffer);
    HardwareBuffer indices(std::array<uint32_t, 3>{0, 1, 2}, BufferUsage::IndexBuffer);
    HardwareBuffer packets(std::array{RasterIndirectPacket{.vertexCount = 3, .instanceCount = 1}}, BufferUsage::RasterIndirect);

    auto movedRaster = std::move(raster);
    movedRaster.outColor = first;
    movedRaster.vp.view = 0.25f;
    auto firstBatch = movedRaster.beginBatch();
    firstBatch.drawIndirect(vertices, indices, packets, 1);
    raster = std::move(movedRaster);
    raster.outColor = second;
    raster.vp.view = 0.75f;
    auto secondBatch = raster.beginBatch();
    secondBatch.drawIndirect(vertices, indices, packets, 1);
    std::array<std::array<float, 4>, 2> pixels{};
    HardwareBuffer readback(sizeof(pixels), BufferUsage::StorageBuffer);
    executor << std::move(firstBatch) << std::move(secondBatch)
             << first.copyTo(readback) << second.copyTo(readback, 0, 0, sizeof(pixels[0]));
    executor.commit().waitForDeferredResources();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)) &&
           pixels[0] == std::array{0.25f, 0.f, 0.f, 1.f} && pixels[1] == std::array{0.75f, 0.f, 0.f, 1.f},
           "moved raster fields bind each attachment and snapshot its uniform value");
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
            test_raster_batch(false);
            test_raster_batch(true);
            test_raster_sampled_storage_image();
            test_generated_packet_and_attachment_load(true);
            test_generated_packet_and_attachment_load(false);
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
