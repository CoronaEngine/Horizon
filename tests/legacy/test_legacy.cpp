#include "Codegen/ControlFlows.h"
#include SLANG(compute_write.slang)
#include "Horizon.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/TypeAlias.h"
#include "Compiler/ShaderUtils.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceAccess.h"
#include SLANG(examples/Baseline/Slang/baseline_vert.slang)
#include SLANG(examples/Baseline/Slang/baseline_frag.slang)
#include SLANG(examples/Disney/Slang/disney_vert.slang)
#include SLANG(examples/DrawStress/Slang/drawstress_frag.slang)

#include <array>
#include <cstdlib>
#include <utility>
#include <iostream>
#include <iterator>
#include <memory>
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

template<class Error>
bool throws(auto operation)
{
    try { operation(); }
    catch (const Error&) { return true; }
    return false;
}

void test_cpu()
{
    DeviceFeaturesChain original;
    DeviceFeaturesChain copy = original;
    expect(original.getChainHead()->pNext != copy.getChainHead()->pNext, "copied Vulkan features own their chain");
    original = copy;
    expect(original.getChainHead()->pNext != copy.getChainHead()->pNext, "assigned Vulkan features own their chain");
    using namespace EmbeddedShader;
    for (auto [module, stage] : {std::pair{&compute_write_slang.shaderModule, ShaderStage::ComputeShader},
                                 std::pair{&baseline_vert_slang.shaderModule, ShaderStage::VertexShader},
                                 std::pair{&baseline_frag_slang.shaderModule, ShaderStage::FragmentShader}}) {
        const auto* words = std::get_if<std::vector<uint32_t>>(&module->shaderCode);
        expect(words && !words->empty() && words->front() == 0x07230203u, "generated stage contains SPIR-V");
        const auto& entries = module->shaderResources.entryPointInfoPool;
        expect(entries.size() == 1 && entries.front().name == "main" && entries.front().stage == stage,
               "generated reflection identifies the shader entry point and stage");
    }
    const auto& computeResources = compute_write_slang.shaderModule.shaderResources;
    expect(computeResources.pushConstantSize == 2 * sizeof(uint32_t) &&
           std::ranges::any_of(computeResources.bindInfoPool, [](const auto& binding) {
               return binding.variateName == "value" && binding.byteOffset == sizeof(uint32_t);
           }), "generated compute reflection preserves push constant size and member offset");

    int vertexParses = 0, fragmentParses = 0, computeParses = 0;
    auto vertex = [&](Float3 pos) { ++vertexParses; position() = Float4(pos, Float(1)); };
    auto fragment = [&] { ++fragmentParses; return Float4(1, 0, 0, 1); };
    auto stages = RasterPipelineObject::compile(vertex, fragment);
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
    auto variants = RasterPipelineObject::compile(vertexWithDraw, conditionalColor);
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
    expect(!empty && !image, "default resources must be empty");

    ComputePipeline<> pendingCompute;
    pendingCompute.uses({});
    expect(throws<std::runtime_error>([&] { (void)pendingCompute(1); }),
           "a compute placeholder rejects dispatch before a shader is assigned");

    RasterPipeline<> pendingRaster;
    pendingRaster.uses({});
    (void)pendingRaster({}, {}, {}, 0);
    expect(throws<std::logic_error>([&] { (void)pendingRaster({}, {}, {}, 1); }),
           "a raster placeholder rejects a draw before shaders are assigned");
    expect(throws<std::logic_error>([&] { pendingRaster.setDepthImage(image); }) &&
           throws<std::logic_error>([&] { (void)pendingRaster.getDepthImage(); }),
           "a raster placeholder rejects depth access before shaders are assigned");
    expect(throws<std::invalid_argument>([&] { (void)RasterPipeline(vertex, fragment, {}, {}, 0); }),
           "EDSL raster construction rejects zero views");
    expect(throws<std::invalid_argument>([] { (void)RasterPipeline(baseline_vert_slang, baseline_frag_slang, {}, 33); }),
           "Slang raster construction rejects more than 32 views");
    expect(throws<std::invalid_argument>([&] { (void)RasterPipeline(VertexEntry{baseline_vert_slang}, fragment); }),
           "a precompiled vertex stage cannot be paired with an EDSL fragment");
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
void test_indirect_draws(bool forceSingleDraw)
{
    using namespace EmbeddedShader;
    HardwareExecutor executor;
    HardwareExecutor replay;
    HardwareExecutor acrossDisplay;
    HardwareImage target(4, 2, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    target.setClearColor(0, 0, 0, 0);
    std::array<std::array<float,4>,8> pixels{};
    HardwareBuffer readback(sizeof(pixels), HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer splitReadback(sizeof(pixels), HardwareBuffer::Usage::StorageBuffer);
    {
        Texture2D<ktm::fvec4> color = target;
        auto useVertexParameter = std::make_shared<bool>(true);
        auto useFragmentParameter = std::make_shared<bool>(true);
        std::weak_ptr<bool> vertexFlag = useVertexParameter, fragmentFlag = useFragmentParameter;
        auto vs = [flag = std::move(useVertexParameter)](Float2 pos, Float scale) {
            Float w;
            w = Float(1);
            $IF(*flag) { w = rasterDrawParameter<float>(3); };
            position() = Float4(pos, Float(0), w * scale);
            return Float4(0, 0, drawIndex(), 0);
        };
        auto fs = [&, flag = std::move(useFragmentParameter)](Float4 drawColorOffset) {
            Float4 value;
            value = Float4(0, 0, 1, 1);
            $IF(*flag) { value = rasterDrawParameter<ktm::fvec4>(0); };
            color << value + drawColorOffset;
        };
        RasterPipeline<> pipeline;
        pipeline = RasterPipeline(std::move(vs), std::move(fs), {.depthTest = false, .depthWrite = false});
        useVertexParameter = vertexFlag.lock();
        useFragmentParameter = fragmentFlag.lock();
        expect(useVertexParameter && useFragmentParameter, "both raster stages retain their condition captures");
        // One triangle covers the screen, the other covers only the second pixel row.
        const std::array<std::array<float, 3>, 6> vertices{{
            {-1,-1,1}, {3,-1,1}, {-1,3,1}, {-1,0,1}, {3,0,1}, {-1,2,1}
        }};
        HardwareBuffer leftVertices(std::span(vertices).first<3>(), HardwareBuffer::Usage::VertexBuffer);
        HardwareBuffer rightVertices(vertices, HardwareBuffer::Usage::VertexBuffer);
        HardwareBuffer leftIndices(std::array<uint16_t,3>{0,1,2}, HardwareBuffer::Usage::IndexBuffer);
        HardwareBuffer rightIndices(std::array<uint32_t,7>{99,1,2,3,4,5,6}, HardwareBuffer::Usage::IndexBuffer);
        HardwareBuffer leftCommands(std::array{RasterIndirectPacket{.vertexCount=3, .instanceCount=1}},
                                    HardwareBuffer::Usage::RasterIndirect);
        HardwareBuffer rightCommands(std::array{
            RasterIndirectPacket{.vertexCount=3, .instanceCount=2, .firstIndex=1, .baseVertex=-1},
            RasterIndirectPacket{.vertexCount=3, .instanceCount=1, .firstIndex=4, .baseVertex=-1, .parameterWord=4}
        }, HardwareBuffer::Usage::RasterIndirect);
        HardwareBuffer red(std::array<float,4>{1,0,0,1}, HardwareBuffer::Usage::StorageBuffer);
        HardwareBuffer greens(std::array<std::array<float,4>,2>{{{0,1,0,1}, {0,2,0,1}}},
                              HardwareBuffer::Usage::StorageBuffer);
        const auto drawLeft = pipeline(leftVertices, leftIndices, leftCommands, 1, red, ScissorRect{0,0,2,2});
        HardwareBuffer empty;
        executor << drawLeft << empty.copyTo(leftCommands) << drawLeft
                 << pipeline({}, {}, {}, 0)
                 << pipeline(leftVertices, leftIndices, leftCommands, 1, red, ScissorRect{-3,0,2,2})
                 << pipeline(rightVertices, rightIndices, rightCommands, 2, greens, ScissorRect{2,0,2,2});
        replay << drawLeft << target.copyTo(readback);
        *useVertexParameter = false;
        *useFragmentParameter = false;
        // The new shader variant requires a physical split within the same logical pass.
        const auto drawBlue = pipeline(leftVertices, leftIndices, leftCommands, 1, {}, ScissorRect{2,0,1,1});
        executor << drawBlue
                 << pipeline(leftVertices, leftIndices, leftCommands, 1, {}, ScissorRect{3,0,1,2},
                             RasterPass{.loadColor = true}, ktm::uvec2(4, 1));
        replay << drawBlue << target.copyTo(splitReadback);

        HardwareImage displayTarget(4, 2, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
        displayTarget.setClearColor(0, 0, 0, 0);
        color = displayTarget;
        *useVertexParameter = true;
        *useFragmentParameter = true;
        acrossDisplay << pipeline(leftVertices, leftIndices, leftCommands, 1, red, ScissorRect{0,0,2,2})
                      << HardwareDisplayer{}(displayTarget)
                      << pipeline(leftVertices, leftIndices, leftCommands, 1, red, ScissorRect{2,0,2,2})
                      << displayTarget.copyTo(readback);
    }
    executor << target.copyTo(readback);
    auto& multiDraw = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils()
                          .featuresChain.getChainHead()->features.multiDrawIndirect;
    const auto supported = multiDraw;
    if (forceSingleDraw) multiDraw = VK_FALSE;
    executor.commit().wait();
    multiDraw = supported;
    expect(HardwareBackendAccess::state(executor).backend.recordedRasterPassCount == 3,
           "adjacent geometry groups share a pass; switching variants and explicitly loading each start a pass");
    expect(readback.copyToData(pixels.data(), sizeof(pixels)), "raster readback");
    const std::array<std::array<float,4>,8> expected{{
        {1,0,1,1}, {1,0,1,1}, {0,0,5,1}, {0,0,1,1},
        {1,0,1,1}, {1,0,1,1}, {0,2,3,1}, {0,2,3,1}
    }};
    expect(pixels == expected, "repeated and merged draws retain their scissor, packet offsets and draw indices "
           "across a physical split and reset their index at an explicit load");

    replay.commit().wait();
    const std::array<std::array<float,4>,8> expectedReplay{{
        {1,0,0,1}, {1,0,0,1}, {}, {},
        {1,0,0,1}, {1,0,0,1}, {}, {}
    }};
    expect(HardwareBackendAccess::state(replay).backend.recordedRasterPassCount == 2 &&
           readback.copyToData(pixels.data(), sizeof(pixels)) && pixels == expectedReplay,
           "replaying a saved draw clears the target and draws only its original geometry at index zero");
    std::array<std::array<float,4>,8> expectedBlue{};
    expectedBlue[2] = {0,0,1,1};
    expect(splitReadback.copyToData(pixels.data(), sizeof(pixels)) && pixels == expectedBlue,
           "replaying a draw that previously split a pass retains its original clear and draw index");

    acrossDisplay.commit().wait();
    const std::array<std::array<float,4>,8> expectedAfterDisplay{{
        {}, {}, {1,0,0,1}, {1,0,0,1},
        {}, {}, {1,0,0,1}, {1,0,0,1}
    }};
    expect(readback.copyToData(pixels) && pixels == expectedAfterDisplay,
           "draws keep their original attachment after rebinding and the skipped display resets clear and draw index");
}

void test_raster_sampled_storage_image()
{
    using namespace EmbeddedShader;
    using Pixel = std::array<float, 4>;
    const std::array<Pixel, 1> source{{{0.25f, 0.5f, 0.75f, 1.0f}}};
    HardwareImage input(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::SampledStorageImage);
    HardwareImage target(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    HardwareBuffer upload(source, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer readback(sizeof(source), HardwareBuffer::Usage::StorageBuffer);
    Texture2D<ktm::fvec4> texture = input, output = target;
    auto vs = [](Float2 pos) { position() = Float4(pos, Float(0), Float(1)); };
    auto fs = [&] { output << texture.sample(Float2(0.5f)); };
    RasterPipeline pipeline(vs, fs, {.depthTest = false, .depthWrite = false});
    const std::array<std::array<float, 2>, 3> vertices{{{-1, -1}, {3, -1}, {-1, 3}}};
    HardwareBuffer vertexBuffer(vertices, HardwareBuffer::Usage::VertexBuffer);
    HardwareBuffer indexBuffer(std::array<uint16_t, 3>{0, 1, 2}, HardwareBuffer::Usage::IndexBuffer);
    HardwareBuffer commands(std::array{RasterIndirectPacket{.vertexCount=3, .instanceCount=1}},
                            HardwareBuffer::Usage::RasterIndirect);
    HardwareExecutor executor;
    pipeline.uses({{input, ResourceAccess::Read}});
    executor << upload.copyTo(input)
             << pipeline(vertexBuffer, indexBuffer, commands, 1)
             << pipeline(vertexBuffer, indexBuffer, commands, 1)
             << target.copyTo(readback);
    executor.commit().wait();
    auto& backend = HardwareBackendAccess::state(executor).backend;
    expect(backend.recordedRasterPassCount == 1, "two draws sampling the same texture share one render pass");
    std::array<Pixel, 1> pixel{};
    expect(readback.copyToData(pixel.data(), sizeof(pixel)) && pixel == source,
           "fragment UBO handle samples a sampled/storage image from set 0");

    target.setClearColor(0, 1, 0, 1);
    executor << pipeline(vertexBuffer, indexBuffer, commands, 1)
             << pipeline({}, {}, {}, 0, {}, {}, RasterPass{})
             << target.copyTo(readback);
    executor.commit().wait();
    expect(backend.recordedRasterPassCount == 2 &&
           readback.copyToData(pixel.data(), sizeof(pixel)) && pixel[0] == Pixel{0,1,0,1},
           "an empty indirect pass still clears its attachment");

    const auto& features = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils()
                               .featuresChain.getChainHead()->features;
    if (features.fragmentStoresAndAtomics) {
        HardwareBuffer counter(std::array{0.f}, HardwareBuffer::Usage::StorageBuffer);
        ArrayProxy<float> values(counter);
        RasterPipeline increment(vs, [&] {
            Float next;
            next = values[0] + Float(1.f);
            values[0] = next;
            output << Float4(next, 0, 0, 1);
        }, {.depthTest = false, .depthWrite = false});
        for (bool declareAccess : {false, true}) {
            if (declareAccess) increment.uses({{counter, ResourceAccess::ReadWrite}});
            executor << increment(vertexBuffer, indexBuffer, commands, 1)
                     << increment(vertexBuffer, indexBuffer, commands, 1)
                     << target.copyTo(readback);
            executor.commit().wait();
            const Pixel expected{declareAccess ? 4.f : 2.f, 0, 0, 1};
            expect(backend.recordedRasterPassCount == 2 &&
                   readback.copyToData(pixel.data(), sizeof(pixel)) && pixel[0] == expected,
                   "successive fragment storage writes require a pass boundary even without declared accesses");
        }
    }

    HardwareImage layers(HardwareImage::CreateInfo{.width = 2, .height = 2, .format = HardwareImage::Format::RGBA32_FLOAT,
        .usage = HardwareImage::Usage::StorageImage, .arrayLayers = 2, .mipLevels = 2});
    std::array attachments{layers[0][1], layers[0][1], layers[1][1]};
    RasterPipeline additive(vs, fs, {.depthTest = false, .depthWrite = false, .blend = RasterBlend::Additive});
    additive.uses({{input, ResourceAccess::Read}});
    for (auto& attachment : attachments) {
        attachment.setClearColor(0, 0, 0, 0);
        output = attachment;
        executor << additive(vertexBuffer, indexBuffer, commands, 1);
    }
    std::array<Pixel, 2> layerPixels{};
    HardwareBuffer layerReadback(sizeof(layerPixels), HardwareBuffer::Usage::StorageBuffer);
    executor << layers.copyTo(layerReadback, 0, 1) << layers.copyTo(layerReadback, 1, 1, sizeof(Pixel));
    executor.commit().wait();
    expect(backend.recordedRasterPassCount == 2, "independent views of the same layer and mip share a pass");
    expect(layerReadback.copyToData(layerPixels) && layerPixels[0] == Pixel{0.5f, 1.f, 1.5f, 2.f} &&
           layerPixels[1] == source[0], "the first layer receives two draws and the second layer receives one");
}

void test_shared_vertex_stage()
{
    const float positions[][3] = {{-1, -1, 0}, {3, -1, 0}, {-1, 3, 0}};
    HardwareBuffer vertices(positions, HardwareBuffer::Usage::VertexBuffer);
    // The Disney VS fetches two packed 16-bit indices per word.
    HardwareBuffer indices(std::array<uint16_t, 4>{0, 1, 2, 0}, HardwareBuffer::Usage::IndexBuffer);
    HardwareBuffer packets(std::array{RasterIndirectPacket{.vertexCount = 3, .instanceCount = 1}},
                           HardwareBuffer::Usage::RasterIndirect);
    const auto model = ktm::fmat4x4::from_eye();
    HardwareBuffer parameters(sizeof(model), HardwareBuffer::Usage::StorageBuffer, &model);
    HardwareBuffer fieldBuffer(sizeof(uint32_t), HardwareBuffer::Usage::StorageBuffer);

    HardwareImage target(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    RasterPipeline raster(disney_vert_slang, drawstress_frag_slang, {.blend = RasterBlend::Additive});
    raster.outColor = target;
    raster.vsp.proj_view = ktm::fmat4x4::from_eye();
    raster.vsp.camPos = ktm::fvec4(0.25f, 0.5f, 0.75f, 1.f);
    // This VS/FS pair leaves the reflected uint slot available for a descriptor index.
    raster.vsp.texCubeIndex = fieldBuffer;
    std::array<float, 4> pixel{};
    HardwareBuffer readback(sizeof(pixel), HardwareBuffer::Usage::StorageBuffer);
    HardwareExecutor executor;
    executor << raster(vertices, indices, packets, 1, parameters) << target.copyTo(readback);
    executor.commit().wait();
    // At the center of the render target, the Disney view vector equals camPos.xyz.
    expect(readback.copyToData(pixel.data(), sizeof(pixel)) && pixel == std::array{0.25f, 0.5f, 0.75f, 2.f},
           "DrawStress fragment reads the Disney vertex output and adds its alpha to the clear color");
    expect(ResourceManager::get(fieldBuffer)->bindlessIndex >= 0, "a named raster field binds its storage buffer");
}

// Compute supplies the indirect command, color and depth for the second pass.
void test_generated_packet_and_attachment_load(bool explicitDepth)
{
    using namespace EmbeddedShader;
    HardwareImage::CreateInfo targetInfo(4, 2, HardwareImage::Format::RGBA32_FLOAT);
    targetInfo.usage = HardwareImage::Usage::StorageImage;
    targetInfo.mipLevels = 2;
    HardwareImage target(targetInfo);
    Texture2D<ktm::fvec4> output = target;
    auto vs = [] {
        position() = Float4(rasterVertexAttribute<ktm::fvec2>(0), rasterDrawParameter<float>(4), Float(1));
        return Float4(0, 0, instanceIndex(), 0);
    };
    auto fs = [&](Float4 instanceColor) { output << rasterDrawParameter<ktm::fvec4>(0) + instanceColor; };
    RasterPipeline opaque(vs, fs, {.blend = RasterBlend::Opaque});
    RasterPipeline additive(vs, fs, {.depthWrite = false, .compare = RasterCompare::Equal, .blend = RasterBlend::Additive});
    if (explicitDepth) {
        HardwareImage depth(4, 2, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage);
        opaque.setDepthImage(depth);
    }

    const std::array<float,2> vertices[]{{-1,-1},{3,-1},{-1,3},{0,-1},{1,-1},{1,1},{0,1}};
    const std::array<uint16_t,9> indices{0,1,2,3,4,5,3,5,6};
    HardwareExecutor executor;
    HardwareBuffer vertexBuffer;
    executor << vertexBuffer.createDeviceLocal(HardwareBuffer::Usage::VertexBuffer, vertices, std::size(vertices), sizeof(vertices[0]));
    HardwareBuffer indexBuffer = vertexBuffer;
    auto expectInvalidUpload = [&](HardwareBuffer::Usage usage, const void* data, size_t elementSize) {
        try { executor << indexBuffer.createDeviceLocal(usage, data, 1, elementSize); }
        catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("invalid upload was accepted");
    };
    expectInvalidUpload(HardwareBuffer::Usage::IndexBuffer, nullptr, sizeof(uint16_t));
    expectInvalidUpload(HardwareBuffer::Usage::IndexBuffer, indices.data(), 1);
    expectInvalidUpload(HardwareBuffer::Usage::VertexBuffer, vertices, 2);
    HardwareBuffer originalVertices(sizeof(vertices), HardwareBuffer::Usage::StorageBuffer);
    executor << indexBuffer.copyTo(originalVertices);
    if (explicitDepth) {
        executor << indexBuffer.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer, indices.data(), indices.size(), sizeof(indices[0]));
    } else {
        const std::vector<uint32_t> wideIndices(indices.begin(), indices.end());
        executor << indexBuffer.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer, wideIndices.data(), wideIndices.size(), sizeof(wideIndices[0]));
    }
    expect(vertexBuffer.getElementCount() == std::size(vertices) &&
           vertexBuffer.getElementSize() == sizeof(vertices[0]) &&
           indexBuffer.getElementCount() == indices.size() &&
           indexBuffer.getElementSize() == (explicitDepth ? sizeof(uint16_t) : sizeof(uint32_t)),
           "staged raster buffers preserve element counts, vertex stride and index width");
    HardwareBuffer firstCommands(std::array{RasterIndirectPacket{.vertexCount=3, .instanceCount=1}},
                                 HardwareBuffer::Usage::RasterIndirect);
    HardwareBuffer redAndDepth(std::array<float,5>{1,0,0,1,0.5f}, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer packets(std::array<RasterIndirectPacket,1>{}, HardwareBuffer::Usage::RasterIndirect);
    const auto& features = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils()
                               .featuresChain.getChainHead()->features;
    const uint32_t firstInstance = features.drawIndirectFirstInstance ? 7u : 0u;
    HardwareBuffer generatedParameters(3, sizeof(ktm::fvec4), HardwareBuffer::Usage::StorageBuffer);
    EmbeddedShader::ArrayProxy<uint32_t> words(packets);
    EmbeddedShader::ArrayProxy<ktm::fvec4> parameters(generatedParameters);
    executor << opaque(vertexBuffer, indexBuffer, firstCommands, 1, redAndDepth, ScissorRect{0,0,4,1}, {}, ktm::uvec2(4, 2));
    auto sharedDepth = opaque.getDepthImage();
    additive.setDepthImage(sharedDepth);
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
        executor << generate(1);
    }
    std::array<std::array<float,4>,8> pixels{};
    HardwareBuffer readback(sizeof(pixels), HardwareBuffer::Usage::StorageBuffer);
    executor << additive(vertexBuffer, indexBuffer, packets, 1, generatedParameters, {},
                         RasterPass{.loadColor = true, .loadDepth = true})
             << target.copyTo(readback);
    executor.commit().wait();
    std::array<std::array<float,2>, std::size(vertices)> copiedVertices{};
    expect(originalVertices.copyToData(copiedVertices) && std::ranges::equal(copiedVertices, vertices),
           "invalid uploads preserve the vertex data shared by the copied buffer");
    expect(readback.copyToData(pixels.data(), sizeof(pixels)), "multi-pass readback");
    // Only the top row contains the first pass's depth. Both instances add color on its right half.
    const float instanceSum = static_cast<float>(firstInstance + (firstInstance + 1));
    const std::array<float,4> red{1,0,0,1}, blended{1,2,instanceSum,1}, clear{0,0,0,1};
    expect(pixels == std::array{red, red, blended, blended, clear, clear, clear, clear},
           "the additive pipeline loads color and passes Equal only where the opaque pipeline wrote depth");
}

void test_compute_dispatch_snapshots()
{
    std::array<uint32_t, 8> words{};
    HardwareBuffer firstOutput(words, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer secondOutput(words, HardwareBuffer::Usage::StorageBuffer);
    HardwareExecutor executor;
    {
        ComputePipeline original(compute_write_slang);
        ComputePipeline<compute_write_slang_t> other;
        static_assert(std::same_as<decltype(original), ComputePipeline<compute_write_slang_t>>);
        static_assert(!std::is_base_of_v<ComputePipeline<>, decltype(original)>);
        auto moved = std::move(original);
        moved.params.outputIndex = firstOutput;
        moved.params.value = 10u;
        const auto writeFirst = moved.uses({{firstOutput, ResourceAccess::Write}})(2, 1, 2);

        other.params.outputIndex = secondOutput;
        other.params.value = 30u;
        const auto overwrite = other.uses({{secondOutput, ResourceAccess::Write}})(1);

        other = std::move(moved);
        other.params.outputIndex = secondOutput;
        other.params.value = 20u;
        const auto writeSecond = other.uses({{secondOutput, ResourceAccess::Write}})(1, 2, 1);
        other.params.value = 99u;
        other.uses({});

        executor << writeFirst << writeSecond << overwrite;
    }
    const auto& pending = HardwareBackendAccess::state(executor).pending;
    expect(pending.size() == 3, "all three saved dispatches are queued");
    for (const auto& item : pending) {
        const auto& entry = std::get<RecordedCommand>(item);
        ResourceAccessList accesses;
        entry.command->resolveResourceUses(entry.resourceUses, accesses);
        expect(!accesses.conservative && accesses.uses.size() == 1 &&
               accesses.uses.front().declaredAccess == VK_ACCESS_2_SHADER_WRITE_BIT,
               "each saved dispatch retains its explicit write declaration for its bound buffer");
    }
    executor.commit().wait();
    expect(firstOutput.copyToData(words) && words == std::array{10u, 11u, 0u, 0u, 14u, 15u, 0u, 0u},
           "saved Slang dispatch retains its buffer, scalar and X/Z group counts after a pipeline move");
    expect(secondOutput.copyToData(words) && words == std::array{30u, 0u, 22u, 0u, 0u, 0u, 0u, 0u},
           "saved Slang dispatches retain their Y group counts and execute in enqueue order");

    HardwareImage image(1, 1, HardwareImage::Format::RGBA8_UNORM, HardwareImage::Usage::StorageImage);
    ComputePipeline<compute_write_slang_t> write;
    write.params.outputIndex = firstOutput;
    write.params.value = UINT32_MAX;
    write.params.value = image;
    executor << write(1);
    executor.commit().wait();
    expect(firstOutput.copyToData(words) && words[0] == image.storeDescriptor(),
           "a named compute field writes its image descriptor index into the push constants");
}

void test_compute_shader_variants()
{
    using namespace EmbeddedShader;
    std::array<uint32_t, 3> words{};
    HardwareBuffer outputBuffer(words, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer inputBuffer(std::array{42u}, HardwareBuffer::Usage::StorageBuffer);
    HardwareExecutor executor;
    const auto replay = [&] {
        ArrayProxy<uint32_t> input(inputBuffer), output(outputBuffer);
        Uint slot;
        auto flag = std::make_shared<bool>(false);
        std::weak_ptr<bool> capturedFlag = flag;
        ComputePipeline pipeline([&, useInput = std::move(flag)] {
            Uint value;
            value = Uint(10) + slot;
            $IF(*useInput) { value = value + input[0]; };
            output[slot] = value;
        });

        auto useInput = capturedFlag.lock();
        expect(useInput != nullptr, "the EDSL condition retains its shader capture");
        slot = 0u;
        const auto first = pipeline(1);
        *useInput = true;
        slot = 1u;
        const auto withInput = pipeline(1);
        *useInput = false;
        slot = 2u;
        const auto last = pipeline(1);
        executor << first << withInput << last;
        return withInput;
    }();
    executor.commit().wait();
    expect(outputBuffer.copyToData(words) && words == std::array{10u, 53u, 12u},
           "queued false/true/false dispatches retain their shader variant, buffer bindings and slot");

    const uint32_t nextInput = 7;
    expect(inputBuffer.copyFromData(&nextInput, sizeof(nextInput)), "update the input before replay");
    executor << replay;
    executor.commit().wait();
    expect(outputBuffer.copyToData(words) && words == std::array{10u, 18u, 12u},
           "a saved dispatch reuses its shader variant, input buffer and slot after pipeline destruction");
}

void test_compute_sampling()
{
    using namespace EmbeddedShader;
    using Pixel = std::array<float, 4>;
    const std::array<Pixel, 4> source{{{1,0,0,1}, {0,1,0,1}, {0,0,1,1}, {1,1,0,1}}};
    HardwareBuffer upload(source, HardwareBuffer::Usage::StorageBuffer);
    std::array<Pixel, 16> base{};
    HardwareBuffer uploadBase(base, HardwareBuffer::Usage::StorageBuffer);
    HardwareImage::CreateInfo info(4, 4, HardwareImage::Format::RGBA32_FLOAT);
    info.mipLevels = 2;
    info.usage = HardwareImage::Usage::SampledStorageImage;
    HardwareImage input(info);
    HardwareImage result(2, 2, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    HardwareBuffer readback(sizeof(source), HardwareBuffer::Usage::StorageBuffer);
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
        const auto sample = pipeline(1);
        executor << sample << result.copyTo(readback);
        executor.commit().wait();
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
    const auto loadBaseMip = load(1);
    auto mipOne = input[1];
    tex = mipOne;
    const auto loadNextMip = load(1);
    executor << loadBaseMip << result.copyTo(readback);
    executor.commit().wait();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)) && pixels == std::array<Pixel, 4>{},
           "saved compute dispatch reads the original EDSL texture mip");
    executor << loadNextMip << result.copyTo(readback);
    executor.commit().wait();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)) && pixels == source,
           "the next dispatch reads the rebound EDSL texture");

    HardwareImage::CreateInfo cubeInfo(2, 2, HardwareImage::Format::RGBA32_FLOAT);
    cubeInfo.cube = true;
    cubeInfo.arrayLayers = 6;
    cubeInfo.mipLevels = 2;
    HardwareImage cubeImage(cubeInfo);
    TextureCube<ktm::fvec4> cube = cubeImage;
    ComputePipeline sampleCube([&] {
        dst[dispatchThreadID()->xy()] = cube.sampleLevel(sampler, Float3(1, 0, 0), Float(1));
    }, ktm::uvec3(2, 2, 1), options);
    const auto sampleFirst = sampleCube(1);
    executor << upload.copyTo(cubeImage, 0, 0, 1) << sampleFirst << result.copyTo(readback);
    executor.commit().wait();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)) && std::ranges::all_of(pixels, [&](Pixel p) { return p == source[0]; }),
           "explicit compute cube sampler reads the requested mip");

    HardwareImage nextCube(cubeInfo);
    cube = nextCube;
    const auto sampleNext = sampleCube(1);
    executor << upload.copyTo(nextCube, sizeof(Pixel), 0, 1) << sampleNext << result.copyTo(readback);
    executor.commit().wait();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)) && std::ranges::all_of(pixels, [&](Pixel p) { return p == source[1]; }),
           "the next dispatch samples the rebound cubemap");
}

void test_present_retry()
{
    HardwareExecutor executor;
    auto& pending = HardwareBackendAccess::state(executor).pending;
    using Display = std::remove_cvref_t<decltype(*std::get<1>(pending.front()))>;
    struct RetryDisplay final : Display {
        using Display::Display;
        mutable int attempts{};
        void submit(HardwareExecutorVulkan& producer) const override {
            // Exercise the same boundary as a failed present after the GPU work was submitted.
            producer.commit();
            if (++attempts == 1) throw std::runtime_error("injected presentation failure");
        }
    };

    const std::array value{7u}, zero{0u}, sentinel{19u};
    HardwareBuffer source(value, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer prefix(zero, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer suffix(zero, HardwareBuffer::Usage::StorageBuffer);
    const auto display = std::make_shared<RetryDisplay>(nullptr, HardwareImage{});
    executor << source.copyTo(prefix);
    pending.emplace_back(display);
    executor << source.copyTo(suffix);

    bool failed = false;
    try { executor.commit(); } catch (const std::runtime_error&) { failed = true; }
    executor.wait();
    std::array<uint32_t, 1> result{};
    expect(failed && display->attempts == 1 && prefix.copyToData(result) && result == value &&
           suffix.copyToData(result) && result == zero,
           "failure during presentation leaves its prefix submitted and its suffix pending");

    expect(prefix.copyFromData(sentinel.data(), sizeof(sentinel)), "mark the submitted prefix before retry");
    executor.commit().wait();
    expect(display->attempts == 2 && prefix.copyToData(result) && result == sentinel &&
           suffix.copyToData(result) && result == value,
           "retry presents again and submits the suffix without repeating the prefix");
}

void test_gpu()
{
    std::cerr << "GPU: initializing buffers\n";
    const std::array<uint32_t, 8> input{10, 20, 30, 40, 50, 60, 70, 80};
    const std::array<uint32_t, 8> zeros{};
    HardwareBuffer source(input, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer destination(zeros, HardwareBuffer::Usage::StorageBuffer);
    HardwareExecutor executor;
    auto& backend = HardwareBackendAccess::state(executor).backend;
    auto& pending = HardwareBackendAccess::state(executor).pending;
    HardwareBuffer emptyBuffer;
    HardwareImage emptyImage;
    executor << emptyBuffer.copyTo(emptyBuffer) << emptyBuffer.copyTo(emptyImage)
             << emptyImage.copyTo(emptyImage) << emptyImage.copyTo(emptyBuffer) << emptyImage.copyFrom(nullptr);
    expect(pending.empty(), "empty resources and a null image upload record no GPU work");
    const auto emptyUpload = emptyBuffer.createDeviceLocal(HardwareBuffer::Usage::StorageBuffer, nullptr, 0, sizeof(uint32_t));
    executor << emptyUpload;
    expect(!emptyBuffer && emptyBuffer.getElementCount() == 0 &&
           emptyBuffer.getElementSize() == sizeof(uint32_t) && pending.empty(),
           "a zero-element device-local buffer preserves its dimensions without recording an upload");
    std::array<uint32_t, 8> output{};
    {
        HardwareBuffer intermediate(zeros, HardwareBuffer::Usage::StorageBuffer);
        executor << source.copyTo(intermediate);
        executor.wait().wait(nullptr).wait(&executor);
        expect(!backend.getSubmissionPoint() && pending.size() == 1,
               "waiting before the first commit leaves the copy queued");

        const auto expectCompletedCopy = [&](HardwareExecutor& waited, const auto& expected) {
            expect(&waited == &executor && backend.deferredReleaseQueue.empty() && pending.size() == 1 &&
                   intermediate.copyToData(output) && output == expected,
                   "a host wait completes the submitted copy and preserves the next queued copy");
        };
        executor.commit();
        executor << destination.copyTo(intermediate);
        expectCompletedCopy(executor.wait(), input);
        executor.commit();
        executor << source.copyTo(intermediate);
        expectCompletedCopy(executor.wait(nullptr), zeros);
        executor.commit();
        executor << destination.copyTo(intermediate);
        expectCompletedCopy(executor.wait(&executor), input);

        executor.commit();
        HardwareExecutor consumer;
        HardwareBuffer readback(input, HardwareBuffer::Usage::StorageBuffer);
        consumer.wait(&executor) << intermediate.copyTo(readback);
        expect(!backend.deferredReleaseQueue.empty() && !HardwareBackendAccess::state(consumer).backend.getSubmissionPoint(),
               "waiting on another executor leaves its submitted work retained and the consumer unsubmitted");
        consumer.commit().wait();
        expect(readback.copyToData(output) && output == zeros, "the consumer reads the producer's latest copy");
    }
    {
        HardwareDisplayer display;
        HardwareImage target(4, 2, HardwareImage::Format::RGBA8_UNORM);
        HardwareBuffer first(zeros, HardwareBuffer::Usage::StorageBuffer);
        HardwareBuffer second(input, HardwareBuffer::Usage::StorageBuffer);
        HardwareExecutor producer, consumer;
        const auto frame = display(target);
        {
            HardwareDisplayer moved(std::move(display));
            producer << source.copyTo(target) << frame << target.copyTo(first)
                     << moved(emptyImage) << display(target) << target.copyFrom(zeros.data())
                     << frame << target.copyTo(second);
        }
        expect(!HardwareBackendAccess::state(producer).backend.getSubmissionPoint(),
               "enqueueing saved and immediate display commands does not submit");
        producer.commit();
        consumer.wait(&producer) << frame;
        consumer.commit();
        expect(HardwareBackendAccess::state(consumer).backend.getSubmissionPoint().value != 0,
               "a display-only commit publishes its dependency even when the frame is skipped");
        executor.wait(&consumer) << first.copyTo(destination);
        executor.commit().wait();
        expect(destination.copyToData(output) && output == input && second.copyToData(output) && output == zeros,
               "copies around skipped frames execute in order across replay and a display-only consumer commit");
    }
    std::cerr << "GPU: buffer copy/readback\n";
    {
        const auto copy = [&] {
            HardwareBuffer upload(input, HardwareBuffer::Usage::StorageBuffer);
            HardwareBuffer alias = upload;
            upload = destination;
            HardwareBuffer moved = std::move(alias);
            expect(!alias, "moving a buffer clears the source handle");
            return moved.copyTo(destination);
        }();
        executor << copy;
        HardwareExecutor moved(std::move(executor));
        moved.commit();
        executor = std::move(moved);
        executor.wait();
        expect(destination.copyToData(output.data(), sizeof(output)), "buffer readback");
        expect(output == input, "executor moves preserve queued copies and their submission");

        std::cerr << "GPU: deferred resource lifetime\n";
        expect(destination.copyFromData(zeros.data(), sizeof(zeros)), "clear destination before reusing copy");
        executor << copy;
    }
    executor.commit().wait();
    expect(destination.copyToData(output.data(), sizeof(output)) && output == input,
           "a reused copy retains its temporary source after all command handles are released");

    expect(destination.copyFromData(zeros.data(), sizeof(zeros)), "clear destination");
    executor << source.copyTo(destination, sizeof(uint32_t), 3 * sizeof(uint32_t), 2 * sizeof(uint32_t));
    executor.commit().wait();
    expect(destination.copyToData(output.data(), sizeof(output)), "partial copy readback");
    expect(output == std::array<uint32_t, 8>{0, 0, 0, 20, 30, 0, 0, 0}, "copy preserves byte offsets and size");

    executor << destination.copyTo(destination, 3 * sizeof(uint32_t), 0, 2 * sizeof(uint32_t));
    executor.commit().wait();
    expect(destination.copyToData(output.data(), sizeof(output)), "self copy readback");
    expect(output == std::array<uint32_t, 8>{20, 30, 0, 20, 30, 0, 0, 0}, "disjoint self copy must not deadlock");
    bool rejected = false;
    try { executor << source.copyTo(destination, 0, sizeof(output), sizeof(uint32_t)); }
    catch (const std::out_of_range&) { rejected = true; }
    expect(rejected, "out of bounds copy must be rejected before recording");

    std::cerr << "GPU: image mip/layer round trip\n";
    const std::array<uint32_t, 2> layers{0x11223344, 0x55667788};
    HardwareImage layered(1, 1, HardwareImage::Format::RGBA8_UINT, HardwareImage::Usage::SampledImage, 2);
    executor << layered.copyFrom(&layers[0], 0) << layered.copyFrom(&layers[1], 1);
    executor << layered.copyTo(destination, 0) << layered.copyTo(destination, 1, 0, sizeof(uint32_t));
    executor.commit().wait();
    std::array<uint32_t, 2> copiedLayers{};
    expect(destination.copyToData(copiedLayers.data(), sizeof(copiedLayers)) && copiedLayers == layers,
           "explicit uploads preserve both array layers in one submission");
    VkFormatProperties bc1{};
    vkGetPhysicalDeviceFormatProperties(getHardwareContext().getMainDevice()->deviceManager.getPhysicalDevice(),
                                        VK_FORMAT_BC1_RGB_UNORM_BLOCK, &bc1);
    if (bc1.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) {
        std::cerr << "GPU: BC1 upload/readback\n";
        const std::array<uint32_t, 4> blocks{1, 2, 3, 4};
        HardwareImage compressed(5, 3, HardwareImage::Format::BC1_RGB_UNORM);
        executor << compressed.copyFrom(blocks.data()) << compressed.copyTo(destination);
        executor.commit().wait();
        std::array<uint32_t, 4> copiedBlocks{};
        expect(destination.copyToData(copiedBlocks.data(), sizeof(copiedBlocks)) && copiedBlocks == blocks,
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
    const auto previous = backend.getSubmissionPoint();
    const auto waitCount = backend.waitSemaphores.size();
    backend.wait(previous).record(&retry);
    try { backend.commit(); } catch (const std::runtime_error&) {}
    expect(backend.commandList.size() == 1 && backend.getSubmissionPoint().queue == previous.queue &&
           backend.getSubmissionPoint().value == previous.value && backend.waitSemaphores.size() == waitCount,
           "failed recording preserves the previous completion point and does not duplicate waits");
    retry.fail = false;
    backend.commit();
    executor.wait();
    expect(backend.commandList.empty(), "retry consumes the recorded command");

    HardwareBuffer prefix(zeros, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer suffix(zeros, HardwareBuffer::Usage::StorageBuffer);
    HardwareImage frame(4, 2, HardwareImage::Format::RGBA8_UNORM);
    executor << source.copyTo(prefix) << HardwareDisplayer{}(frame);
    retry.fail = true;
    pending.emplace_back(RecordedCommand{retry});
    executor << source.copyTo(suffix);
    bool failed = false;
    try { executor.commit(); } catch (const std::runtime_error&) { failed = true; }
    executor.wait();
    expect(failed && prefix.copyToData(output) && output == input && suffix.copyToData(output) && output == zeros,
           "a skipped frame submits its prefix even if recording the suffix fails");
    expect(prefix.copyFromData(zeros.data(), sizeof(zeros)), "reset the completed prefix before retry");
    retry.fail = false;
    executor.commit().wait();
    expect(prefix.copyToData(output) && output == zeros && suffix.copyToData(output) && output == input,
           "retry submits the suffix exactly once and leaves the completed prefix alone");

    HardwareImage::CreateInfo imageInfo(4, 4, HardwareImage::Format::RGBA8_UINT);
    imageInfo.arrayLayers = 2;
    imageInfo.mipLevels = 2;
    HardwareImage image(imageInfo);
    HardwareImage view;
    {
        HardwareImage imageCopy(imageInfo);
        executor << source.copyTo(image, sizeof(uint32_t), 0, 1);
        executor << image.copyTo(imageCopy, 0, 1, 1, 1);
        auto mip = imageCopy[1][1];
        view = mip;
        mip = imageCopy[0][1];
        executor << mip.copyFrom(zeros.data());
        executor.commit().wait();
    }
    const auto readView = view.copyTo(destination, 0, 0, 2 * sizeof(uint32_t));
    view = {};
    for (int run = 0; run < 2; ++run) {
        expect(destination.copyFromData(zeros.data(), sizeof(zeros)), "clear the destination before each view copy");
        executor << readView;
        executor.commit().wait();
        expect(destination.copyToData(output) && output == std::array<uint32_t, 8>{0, 0, 20, 30, 40, 50, 0, 0},
               "the saved copy retains the original layer and mip after all its image handles are released");
    }

    executor << image.copyFrom(input.data(), 0, 1) << image.copyTo(destination, 0, 1);
    executor.commit().wait();
    std::array<uint32_t, 4> mipPixels{};
    expect(destination.copyToData(mipPixels.data(), sizeof(mipPixels)) &&
           std::ranges::equal(mipPixels, std::span(input).first(mipPixels.size())),
           "staged upload retains its buffer and writes the selected image mip");

    std::cerr << "GPU: EDSL compute\n";
    EmbeddedShader::CompilerOption options;
    options.compileGLSL = false;
    options.compileHLSL = false;
    EmbeddedShader::ArrayProxy<uint32_t> importedInput(source), importedOutput(destination);
    ComputePipeline<> edsl;
    edsl = ComputePipeline([&] {
        EmbeddedShader::Uint index = EmbeddedShader::dispatchThreadID()->x;
        importedOutput[index] = compute_write_slang.addIndex(importedInput[index], index);
    }, ktm::uvec3(1), options);
    const auto dispatch = edsl.uses({{source, ResourceAccess::Read}, {destination, ResourceAccess::Write}})(8);
    executor << dispatch;
    executor.commit().wait();
    expect(destination.copyToData(output) && output == std::array{10u, 21u, 32u, 43u, 54u, 65u, 76u, 87u},
           "an EDSL resource read only by an imported helper is bound at dispatch");

    std::cerr << "GPU: generated raster fields after move\n";
    RasterPipeline raster(baseline_vert_slang, baseline_frag_slang);
    using Raster = RasterPipeline<baseline_vert_slang_t, baseline_frag_slang_t>;
    static_assert(std::same_as<decltype(raster), Raster>);
    static_assert(std::same_as<decltype(&Raster::vp), decltype(raster.vp) Raster::*>);
    static_assert(std::same_as<decltype(&Raster::outColor), decltype(raster.outColor) Raster::*>);
    static_assert(!std::is_base_of_v<RasterPipeline<>, Raster>);
    HardwareImage first(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    HardwareImage second(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    second.setClearColor(0.75f, 0.f, 0.f, 1.f);
    HardwareImage texture(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::SampledImage);
    const std::array texel{0.25f, 0.f, 0.f, 1.f};
    executor << texture.copyFrom(texel.data());
    using Vertex = std::array<float, 5>; // xyz, uv in the Baseline shader.
    const std::array triangle{Vertex{-1, -1, 0, 0, 0}, Vertex{3, -1, 0, 0, 0}, Vertex{-1, 3, 0, 0, 0}};
    HardwareBuffer vertices(triangle, HardwareBuffer::Usage::VertexBuffer);
    HardwareBuffer indices(std::array<uint32_t, 3>{0, 1, 2}, HardwareBuffer::Usage::IndexBuffer);
    HardwareBuffer packets(std::array{RasterIndirectPacket{.vertexCount = 3, .instanceCount = 1}}, HardwareBuffer::Usage::RasterIndirect);
    const struct { ktm::fmat4x4 model; uint32_t textureIndex; } draw{ktm::fmat4x4::from_eye(), texture.storeDescriptor()};
    HardwareBuffer drawParameters(sizeof(draw), HardwareBuffer::Usage::StorageBuffer, &draw);

    auto movedRaster = std::move(raster);
    movedRaster.uses({{texture, ResourceAccess::Read}});
    movedRaster.outColor = first;
    movedRaster.vp.proj = ktm::fmat4x4::from_eye();
    movedRaster.vp.view = ktm::fmat4x4::from_eye();
    const auto firstDraw = movedRaster(vertices, indices, packets, 1, drawParameters);
    raster = std::move(movedRaster);
    raster.outColor = second;
    raster.vp.view = ktm::translate3d(ktm::fvec3(8.f, 0.f, 0.f));
    std::array<std::array<float, 4>, 2> pixels{};
    HardwareBuffer readback(sizeof(pixels), HardwareBuffer::Usage::StorageBuffer);
    executor << firstDraw << raster(vertices, indices, packets, 1, drawParameters)
             << first.copyTo(readback) << second.copyTo(readback, 0, 0, sizeof(pixels[0]));
    executor.commit().wait();
    expect(readback.copyToData(pixels.data(), sizeof(pixels)) &&
           pixels[0] == texel && pixels[1] == std::array{0.75f, 0.f, 0.f, 1.f},
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
            test_present_retry();
            test_indirect_draws(false);
            test_indirect_draws(true);
            test_raster_sampled_storage_image();
            test_shared_vertex_stage();
            test_generated_packet_and_attachment_load(true);
            test_generated_packet_and_attachment_load(false);
            test_compute_dispatch_snapshots();
            test_compute_shader_variants();
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
