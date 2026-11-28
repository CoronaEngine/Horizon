#include "Codegen/ControlFlows.h"
#include SLANG(compute_write.slang)
#include "Horizon.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/TypeAlias.h"
#include "Compiler/ShaderUtils.h"
#include "Compiler/ShaderLanguageConverter.h"
#include "HardwareWrapperVulkan/HardwareVulkan/HardwareExecutorVulkan.h"
#include "HardwareWrapperVulkan/HardwareVulkan/ResourceAccess.h"
#include SLANG(mesh_task.slang)
#include SLANG(examples/Baseline/Mesh/baseline_mesh.slang)
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
#include <string>
#include <string_view>
#include <spirv-tools/libspirv.hpp>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace
{
using Raster = RasterPipeline<>;

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

EmbeddedShader::ShaderCodeModule compileBufferAccess(std::string body, uint32_t descriptorCount = 0)
{
    using namespace EmbeddedShader;
    using Compiler = ShaderLanguageConverter;
    const auto global = Compiler::getGlobalSession();
    slang::TargetDesc target{};
    target.format = SLANG_SPIRV;
    target.profile = global->findProfile("spirv_1_6");
    std::array targets{target};
    auto session = Compiler::createSession(global, targets, {});
    auto library = Compiler::slangModuleCompiler({.moduleName = "access_test", .shaderCode = R"(
        struct Params { uint index; uint alternate; };
        [[vk::push_constant]] ConstantBuffer<Params> params;
        [[vk::binding(0, 1)]] RWStructuredBuffer<uint> buffers[)" +
        (descriptorCount ? std::to_string(descriptorCount) : "") + R"(];
        [shader("compute")] [numthreads(1, 1, 1)]
        void main(uint3 id : SV_DispatchThreadID) {
    )" + body + "}", .sourceLanguage = ShaderLanguage::Slang});
    auto module = Compiler::loadModule(session, library);
    auto entry = Compiler::findEntryPoint(module, "main");
    std::array<slang::IComponentType*, 2> components{module.get(), entry.get()};
    auto program = Compiler::link(session, components);
    SlangCompileResult result;
    Compiler::fillCompileResult(result, program, 0, SLANG_SPIRV, true);
    return {std::move(result.binaryTargets.at(ShaderLanguage::SpirV)),
            std::move(result.reflections.at(ShaderLanguage::SpirV))};
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
    expect(std::ranges::any_of(computeResources.bindInfoPool, [](const auto& binding) {
        return binding.variateName == "outputs" && binding.access == Ast::AccessPermissions::ReadAndWrite;
    }), "Slang reflection preserves the declared access of an RW buffer");
    using Baseline = RasterPipeline<baseline_vert_slang_t, baseline_frag_slang_t>;
    static_assert(requires(Baseline& pipeline) { pipeline.combinedTextureSamplerHandles[0]; });

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
    Texture2D<ktm::fvec4> mixed;
    bool filter = true;
    expect(throws<std::invalid_argument>([&] {
        (void)ComputePipelineObject::compile([&] {
            const auto xy = dispatchThreadID()->xy();
            $IF(filter) { target[xy] = mixed.sample(Float2(0.5f)); }
            $ELSE { target[xy] = mixed[xy] + Float4(0.f); };
        });
    }), "even mutually exclusive host branches need separate proxies for sampling and indexed image access");
    ArrayProxy<Texture2D<ktm::fvec4>> indirectTextures;
    expect(throws<std::invalid_argument>([&] {
        (void)ComputePipelineObject::compile([&] {
            const auto xy = dispatchThreadID()->xy();
            target[xy] = indirectTextures[0][xy];
        });
    }), "an image handle obtained from GPU buffer contents cannot be inferred as a direct EDSL binding");
    ArrayProxy<uint32_t> lazyInput, lazyOutput;
    auto lazy = ComputePipelineObject::compile([&] {
        auto value = lazyInput[0] + Uint(1);
        $IF(filter) { lazyOutput[0] = value; };
    });
    for (bool enabled : {false, true, false}) {
        filter = enabled;
        lazy.updateAutoBind(lazy.compute->getCurrentConditionInfo());
        expect(lazy.autoBindEntries.size() == (enabled ? 2 : 0),
               "a deferred arithmetic read belongs to the host branch that consumes it");
    }
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
    expect(throws<std::runtime_error>([&] { (void)pendingCompute(1); }),
           "a compute placeholder rejects dispatch before a shader is assigned");

    RasterPipeline<> pendingRaster;
    (void)pendingRaster({}, {}, {}, 0);
    expect(throws<std::logic_error>([&] { (void)pendingRaster({}, {}, {}, 1); }),
           "a raster placeholder rejects a draw before shaders are assigned");
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

void test_mesh_draws(auto mesh, uint32_t groupCount)
{
    using namespace EmbeddedShader;
    using Pixel = std::array<float, 4>;
    constexpr Pixel clear{0, 0, 0, 0}, blue{0, 0, 1, 1}, red{1, 0, 0, 1};
    std::array<Pixel, 8> pixels{};
    HardwareExecutor executor;
    HardwareImage color(4, 2, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    HardwareImage depth(4, 2, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage);
    HardwareBuffer readback(sizeof(pixels), HardwareBuffer::Usage::StorageBuffer);
    color.setClearColor(0, 0, 0, 0);
    auto readColor = [&] {
        executor << color.copyTo(readback);
        executor.commit().wait();
        expect(readback.copyToData(pixels), "mesh color readback");
    };

    using Packet = MeshPipeline<>::IndirectPacket;
    auto& caps = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils();
    const bool multiDraw = caps.featuresChain.getChainHead()->features.multiDrawIndirect &&
                           caps.supportedProperties.properties.limits.maxDrawIndirectCount >= 2;
    const uint32_t drawCount = multiDraw ? 2 : 1;
    HardwareBuffer groups(std::array<Packet, 2>{}, HardwareBuffer::Usage::RasterIndirect);
    HardwareBuffer indirectSource(std::array{Packet{multiDraw ? 0u : groupCount, 1, 1}, Packet{groupCount, 1, 1}},
                                  HardwareBuffer::Usage::StorageBuffer);
    executor << indirectSource.copyTo(groups);
    // Only the last indexed triangle covers the screen, in the partially filled second mesh workgroup.
    using Vertex = std::array<float, 5>;
    HardwareBuffer meshVertices(std::array{Vertex{-1, -1, 0.6f, 0, 0}, Vertex{3, -1, 0.6f, 0, 0},
                                          Vertex{-1, 3, 0.6f, 0, 0}}, HardwareBuffer::Usage::StorageBuffer);
    std::array<uint32_t, 33 * 3> meshIndices{};
    meshIndices[97] = 1;
    meshIndices[98] = 2;
    HardwareBuffer meshIndexBuffer(meshIndices, HardwareBuffer::Usage::StorageBuffer);
    HardwareImage texture(1, 1, HardwareImage::Format::RGBA32_FLOAT);
    executor << texture.copyFrom(blue.data());
    mesh.geometry[0] = meshVertices;
    mesh.geometry[1] = meshIndexBuffer;
    mesh.vp.proj = ktm::fmat4x4::from_eye();
    mesh.vp.view = ktm::fmat4x4::from_eye();
    mesh.vp.texIndex = texture.storeSampledDescriptor();
    mesh.outColor = color;
    expect(throws<std::invalid_argument>([&] { (void)mesh(groups, 3); }), "mesh count cannot exceed packet buffer");
    expect(throws<std::invalid_argument>([&] { (void)mesh(meshVertices, 1); }), "mesh requires indirect packets");

    if constexpr (requires { mesh.task.groupCount; })
    {
        mesh.task.groupCount = 0u;
        const auto empty = mesh(groups, drawCount, depth);
        mesh.task.groupCount = 2u;
        executor << empty;
        readColor();
        expect(std::ranges::all_of(pixels, [&](const Pixel& pixel) { return pixel == clear; }),
               "a saved task command retains its zero mesh group count");
    }
    const auto meshDraw = mesh(groups, drawCount, depth);
    if (multiDraw)
    {
        executor << mesh(groups, 1, depth);
        readColor();
        expect(std::ranges::all_of(pixels, [&](const Pixel& pixel) { return pixel == clear; }),
               "the first indirect packet launches zero workgroups");
    }
    else
        expect(throws<std::invalid_argument>([&] { (void)mesh(groups, 2); }), "mesh rejects unsupported multi-draw");
    executor << meshDraw;
    readColor();
    expect(std::ranges::all_of(pixels, [&](const Pixel& pixel) { return pixel == blue; }),
           "the GPU-written indirect packet reaches the last textured triangle");
    executor << meshDraw << mesh({}, 0, depth, {}, Raster::Pass{});
    readColor();
    expect(std::ranges::all_of(pixels, [&](const Pixel& pixel) { return pixel == clear; }),
           "an explicit empty mesh pass clears the preceding draw");

    // Draw near red pixels on the left, then the farther blue triangle across the entire image.
    Texture2D<ktm::fvec4> out = color;
    auto vs = [] { position() = Float4(rasterVertexAttribute<ktm::fvec2>(0), rasterDrawParameter<float>(0), Float(1)); };
    auto fs = [&] { out << Float4(1, 0, 0, 1); };
    RasterPipeline raster(vs, fs, {.blend = Raster::Blend::Opaque});
    HardwareBuffer vertices(std::array{ktm::fvec2(-1, -1), ktm::fvec2(3, -1), ktm::fvec2(-1, 3)},
                            HardwareBuffer::Usage::VertexBuffer);
    HardwareBuffer indices(std::array<uint32_t, 3>{0, 1, 2}, HardwareBuffer::Usage::IndexBuffer);
    HardwareBuffer triangle(std::array{Raster::IndirectPacket{.vertexCount = 3, .instanceCount = 1}},
                            HardwareBuffer::Usage::RasterIndirect);
    HardwareBuffer nearZ(std::array{0.25f}, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer farZ(std::array{0.8f}, HardwareBuffer::Usage::StorageBuffer);
    executor << raster(vertices, indices, triangle, 1, nearZ, depth, Raster::ScissorRect{0, 0, 2, 2}) << meshDraw;
    readColor();
    for (size_t i = 0; i < pixels.size(); ++i)
        expect(pixels[i] == (i % 4 < 2 ? red : blue), "mesh loads the depth and color written by a raster draw");

    executor << meshDraw << raster(vertices, indices, triangle, 1, farZ, depth);
    readColor();
    expect(std::ranges::all_of(pixels, [&](const Pixel& pixel) { return pixel == blue; }),
           "raster loads mesh depth and rejects a farther triangle");
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
        pipeline = RasterPipeline(std::move(vs), std::move(fs));
        useVertexParameter = vertexFlag.lock();
        useFragmentParameter = fragmentFlag.lock();
        expect(useVertexParameter && useFragmentParameter, "both raster stages retain their condition captures");
        // One triangle covers the screen, the other covers only the second pixel row.
        const std::array<std::array<float, 3>, 6> vertices{{
            {-1,-1,1}, {3,-1,1}, {-1,3,1}, {-1,0,1}, {3,0,1}, {-1,2,1}
        }};
        HardwareBuffer leftVertices{std::span(vertices).first<3>(), HardwareBuffer::Usage::VertexBuffer};
        HardwareBuffer rightVertices(vertices, HardwareBuffer::Usage::VertexBuffer);
        HardwareBuffer leftIndices(std::array<uint16_t,3>{0,1,2}, HardwareBuffer::Usage::IndexBuffer);
        HardwareBuffer rightIndices(std::array<uint32_t,7>{99,1,2,3,4,5,6}, HardwareBuffer::Usage::IndexBuffer);
        HardwareBuffer leftCommands(std::array{Raster::IndirectPacket{.vertexCount=3, .instanceCount=1}},
                                    HardwareBuffer::Usage::RasterIndirect);
        HardwareBuffer rightCommands(std::array{
            Raster::IndirectPacket{.vertexCount=3, .instanceCount=2, .firstIndex=1, .baseVertex=-1},
            Raster::IndirectPacket{.vertexCount=3, .instanceCount=1, .firstIndex=4, .baseVertex=-1, .parameterWord=4}
        }, HardwareBuffer::Usage::RasterIndirect);
        HardwareBuffer red(std::array<float,4>{1,0,0,1}, HardwareBuffer::Usage::StorageBuffer);
        HardwareBuffer greens(std::array<std::array<float,4>,2>{{{0,1,0,1}, {0,2,0,1}}},
                              HardwareBuffer::Usage::StorageBuffer);
        const auto drawLeft = pipeline(leftVertices, leftIndices, leftCommands, 1, red, {}, Raster::ScissorRect{0,0,2,2});
        HardwareBuffer empty;
        executor << drawLeft << empty.copyTo(leftCommands) << drawLeft
                 << pipeline({}, {}, {}, 0)
                 << pipeline(leftVertices, leftIndices, leftCommands, 1, red, {}, Raster::ScissorRect{-3,0,2,2})
                 << pipeline(rightVertices, rightIndices, rightCommands, 2, greens, {}, Raster::ScissorRect{2,0,2,2});
        replay << drawLeft << target.copyTo(readback);
        *useVertexParameter = false;
        *useFragmentParameter = false;
        // The new shader variant requires a physical split within the same logical pass.
        const auto drawBlue = pipeline(leftVertices, leftIndices, leftCommands, 1, {}, {}, Raster::ScissorRect{2,0,1,1});
        executor << drawBlue
                 << pipeline(leftVertices, leftIndices, leftCommands, 1, {}, {}, Raster::ScissorRect{3,0,1,2},
                             Raster::Pass{.loadColor = true}, ktm::uvec2(4, 1));
        replay << drawBlue << target.copyTo(splitReadback);

        HardwareImage displayTarget(4, 2, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
        displayTarget.setClearColor(0, 0, 0, 0);
        color = displayTarget;
        *useVertexParameter = true;
        *useFragmentParameter = true;
        acrossDisplay << pipeline(leftVertices, leftIndices, leftCommands, 1, red, {}, Raster::ScissorRect{0,0,2,2})
                      << HardwareDisplayer{}(displayTarget)
                      << pipeline(leftVertices, leftIndices, leftCommands, 1, red, {}, Raster::ScissorRect{2,0,2,2})
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

std::vector<ResolvedResourceUse> commandAccesses(const RecordedCommand& recorded)
{
    ResourceAccessList accesses;
    recorded.command->resolveResourceUses(accesses);
    return std::move(accesses.uses);
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
    Texture2D<ktm::fvec4> texture = input, storage = input, output = target;
    auto vs = [](Float2 pos) { position() = Float4(pos, Float(0), Float(1)); };
    auto fs = [&] { output << (texture.sample(Float2(0.5f)) + storage[Uint2(0, 0)]) * Float(0.5f); };
    RasterPipeline pipeline(vs, fs);
    const std::array<std::array<float, 2>, 3> vertices{{{-1, -1}, {3, -1}, {-1, 3}}};
    HardwareBuffer vertexBuffer(vertices, HardwareBuffer::Usage::VertexBuffer);
    HardwareBuffer indexBuffer(std::array<uint16_t, 3>{0, 1, 2}, HardwareBuffer::Usage::IndexBuffer);
    HardwareBuffer commands(std::array{Raster::IndirectPacket{.vertexCount=3, .instanceCount=1}},
                            HardwareBuffer::Usage::RasterIndirect);
    HardwareExecutor executor;
    HardwareImage albedo(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    HardwareImage normal(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    Texture2D<ktm::fvec4> albedoTexture = albedo, normalTexture = normal, shaded = input;
    ComputePipeline shade([&] {
        const auto xy = dispatchThreadID()->xy();
        shaded[xy] = (albedoTexture[xy] + normalTexture[xy]) * Float(0.5f);
    });
    executor << upload.copyTo(albedo) << upload.copyTo(normal) << shade(1);
    const auto accesses = commandAccesses(std::get<RecordedCommand>(HardwareBackendAccess::state(executor).pending.back()));
    auto hasAccess = [&](const HardwareImage& image, VkAccessFlags2 access) {
        return std::ranges::any_of(accesses, [&](const auto& use) {
            return use.image == ResourceManager::get(image) && use.access == access;
        });
    };
    expect(accesses.size() == 3 && hasAccess(albedo, VK_ACCESS_2_SHADER_READ_BIT) &&
           hasAccess(normal, VK_ACCESS_2_SHADER_READ_BIT) && hasAccess(input, VK_ACCESS_2_SHADER_WRITE_BIT),
           "storage image loads and stores infer two reads and one write");
    executor << pipeline(vertexBuffer, indexBuffer, commands, 1)
             << pipeline(vertexBuffer, indexBuffer, commands, 1)
             << target.copyTo(readback);
    executor.commit().wait();
    auto& backend = HardwareBackendAccess::state(executor).backend;
    expect(backend.recordedRasterPassCount == 1, "two draws sampling the same texture share one render pass");
    std::array<Pixel, 1> pixel{};
    expect(readback.copyToData(pixel.data(), sizeof(pixel)) && pixel == source,
           "the fragment shader samples and storage-loads the same image through separate proxies");

    target.setClearColor(0, 1, 0, 1);
    executor << pipeline(vertexBuffer, indexBuffer, commands, 1)
             << pipeline({}, {}, {}, 0, {}, {}, {}, Raster::Pass{})
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
        });
        executor << increment(vertexBuffer, indexBuffer, commands, 1)
                 << increment(vertexBuffer, indexBuffer, commands, 1)
                 << target.copyTo(readback);
        executor.commit().wait();
        expect(backend.recordedRasterPassCount == 2 && readback.copyToData(pixel.data(), sizeof(pixel)) &&
               pixel[0] == Pixel{2.f, 0, 0, 1},
               "successive fragment storage writes split passes and the second draw sees the first write");
    }

    HardwareImage layers(HardwareImage::CreateInfo{.width = 2, .height = 2, .format = HardwareImage::Format::RGBA32_FLOAT,
        .usage = HardwareImage::Usage::StorageImage, .arrayLayers = 2, .mipLevels = 2});
    std::array attachments{layers[0][1], layers[0][1], layers[1][1]};
    RasterPipeline additive(vs, fs, {.blend = Raster::Blend::Additive});
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
    HardwareBuffer packets(std::array{Raster::IndirectPacket{.vertexCount = 3, .instanceCount = 1}},
                           HardwareBuffer::Usage::RasterIndirect);
    const auto model = ktm::fmat4x4::from_eye();
    HardwareBuffer parameters(sizeof(model), HardwareBuffer::Usage::StorageBuffer, &model);
    HardwareBuffer fieldBuffer(sizeof(uint32_t), HardwareBuffer::Usage::StorageBuffer);

    HardwareImage target(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    RasterPipeline raster(disney_vert_slang, drawstress_frag_slang, {.blend = Raster::Blend::Additive});
    raster.outColor = target;
    raster.vsp.proj_view = ktm::fmat4x4::from_eye();
    raster.vsp.camPos = ktm::fvec4(0.25f, 0.5f, 0.75f, 1.f);
    // This VS/FS pair leaves the reflected uint slot available for a descriptor index.
    raster.vsp.texCubeIndex = fieldBuffer.storeReadDescriptor();
    std::array<float, 4> pixel{};
    HardwareBuffer readback(sizeof(pixel), HardwareBuffer::Usage::StorageBuffer);
    HardwareExecutor executor;
    HardwareImage depthLayers(1, 1, HardwareImage::Format::D16_UNORM, HardwareImage::Usage::DepthImage, 2);
    const auto draw = raster(vertices, indices, packets, 1, parameters, depthLayers[1]);
    depthLayers = {};
    executor << draw << draw << target.copyTo(readback);
    executor.commit().wait();
    // At the center of the render target, the Disney view vector equals camPos.xyz.
    expect(readback.copyToData(pixel.data(), sizeof(pixel)) && pixel == std::array{0.25f, 0.5f, 0.75f, 2.f},
           "the saved Slang draw retains its depth view and the equal-depth replay adds no second contribution");
    expect(ResourceManager::get(fieldBuffer)->bindlessIndex >= 0, "a named raster field binds its storage buffer");
}

// Compute supplies the indirect command, color and depth for the second pass.
void test_generated_packet_and_attachment_load(bool use16BitIndices)
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
    RasterPipeline opaque(vs, fs, {.blend = Raster::Blend::Opaque});
    RasterPipeline additive(vs, fs, {.depthWrite = false, .compare = Raster::Compare::Equal, .blend = Raster::Blend::Additive});
    HardwareImage depth(4, 2, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage);

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
    if (use16BitIndices) {
        executor << indexBuffer.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer, indices.data(), indices.size(), sizeof(indices[0]));
    } else {
        const std::vector<uint32_t> wideIndices(indices.begin(), indices.end());
        executor << indexBuffer.createDeviceLocal(HardwareBuffer::Usage::IndexBuffer, wideIndices.data(), wideIndices.size(), sizeof(wideIndices[0]));
    }
    expect(vertexBuffer.getElementCount() == std::size(vertices) &&
           vertexBuffer.getElementSize() == sizeof(vertices[0]) &&
           indexBuffer.getElementCount() == indices.size() &&
           indexBuffer.getElementSize() == (use16BitIndices ? sizeof(uint16_t) : sizeof(uint32_t)),
           "staged raster buffers preserve element counts, vertex stride and index width");
    HardwareBuffer firstCommands(std::array{Raster::IndirectPacket{.vertexCount=3, .instanceCount=1}},
                                 HardwareBuffer::Usage::RasterIndirect);
    HardwareBuffer redAndDepth(std::array<float,5>{1,0,0,1,0.5f}, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer packets(std::array<Raster::IndirectPacket,1>{}, HardwareBuffer::Usage::RasterIndirect);
    const auto& features = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils()
                               .featuresChain.getChainHead()->features;
    const uint32_t firstInstance = features.drawIndirectFirstInstance ? 7u : 0u;
    HardwareBuffer generatedParameters(3, sizeof(ktm::fvec4), HardwareBuffer::Usage::StorageBuffer);
    EmbeddedShader::ArrayProxy<uint32_t> words(packets);
    EmbeddedShader::ArrayProxy<ktm::fvec4> parameters(generatedParameters);
    executor << opaque(vertexBuffer, indexBuffer, firstCommands, 1, redAndDepth, depth, Raster::ScissorRect{0,0,4,1}, {}, ktm::uvec2(4, 2));
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
        executor << generate(1);
    }
    std::array<std::array<float,4>,8> pixels{};
    HardwareBuffer readback(sizeof(pixels), HardwareBuffer::Usage::StorageBuffer);
    executor << additive(vertexBuffer, indexBuffer, packets, 1, generatedParameters, depth, {},
                         Raster::Pass{.loadColor = true, .loadDepth = true})
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

    HardwareBuffer farBlue(std::array<float,5>{0,0,1,1,0.75f}, HardwareBuffer::Usage::StorageBuffer);
    executor << opaque(vertexBuffer, indexBuffer, firstCommands, 1, redAndDepth)
             << opaque(vertexBuffer, indexBuffer, firstCommands, 1, farBlue);
    const auto& draw = std::get<RecordedCommand>(HardwareBackendAccess::state(executor).pending.back());
    ResourceAccessList accesses;
    draw.command->resolveResourceUses(accesses);
    expect(std::ranges::none_of(accesses.uses, [](const auto& use) { return use.range.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT; }),
           "a draw without depth has no depth attachment access");
    executor << target.copyTo(readback);
    executor.commit().wait();
    const std::array<float,4> blue{0,0,1,1};
    expect(readback.copyToData(pixels) && std::ranges::all_of(pixels, [&](const auto& pixel) { return pixel == blue; }),
           "without a depth attachment the later, farther triangle covers the earlier, nearer triangle");
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
        moved.params.outputIndex = firstOutput.storeWriteDescriptor();
        moved.params.value = 10u;
        const auto writeFirst = moved(2, 1, 2);

        other.params.outputIndex = secondOutput.storeWriteDescriptor();
        other.params.value = 30u;
        const auto overwrite = other(1);

        other = std::move(moved);
        other.params.outputIndex = secondOutput.storeWriteDescriptor();
        other.params.value = 20u;
        const auto writeSecond = other(1, 2, 1);
        other.params.value = 99u;

        executor << writeFirst << writeSecond << overwrite;
    }
    const auto& pending = HardwareBackendAccess::state(executor).pending;
    expect(pending.size() == 3, "all three saved dispatches are queued");
    for (size_t i = 0; i < pending.size(); ++i) {
        const auto accesses = commandAccesses(std::get<RecordedCommand>(pending[i]));
        const auto& expected = i == 0 ? firstOutput : secondOutput;
        expect(accesses.size() == 1 && accesses.front().buffer == ResourceManager::get(expected)->bufferHandle &&
               accesses.front().access == VK_ACCESS_2_SHADER_WRITE_BIT,
               "each saved dispatch infers a write to its original bound buffer");
    }
    executor.commit().wait();
    expect(firstOutput.copyToData(words) && words == std::array{10u, 11u, 0u, 0u, 14u, 15u, 0u, 0u},
           "saved Slang dispatch retains its buffer, scalar and X/Z group counts after a pipeline move");
    expect(secondOutput.copyToData(words) && words == std::array{30u, 0u, 22u, 0u, 0u, 0u, 0u, 0u},
           "saved Slang dispatches retain their Y group counts and execute in enqueue order");

    HardwareImage image(1, 1, HardwareImage::Format::RGBA8_UNORM, HardwareImage::Usage::SampledStorageImage, 2);
    auto layer = image[1];
    ComputePipeline<compute_write_slang_t> write;
    write.params.outputIndex = firstOutput.storeWriteDescriptor();
    write.params.value = layer.storeSampledDescriptor();
    executor << write(1);
    const auto sampled = commandAccesses(std::get<RecordedCommand>(pending.back()));
    expect(std::ranges::any_of(sampled, [&](const auto& use) {
        return use.image == ResourceManager::get(layer) && use.range.baseArrayLayer == 1 &&
               use.range.layerCount == 1 && use.access == VK_ACCESS_2_SHADER_READ_BIT;
    }), "a sampled descriptor retains its image view and read access");
    write.params.value = 17u;
    executor << write(1);
    const auto accesses = commandAccesses(std::get<RecordedCommand>(pending.back()));
    expect(accesses.size() == 1 && accesses.front().buffer == ResourceManager::get(firstOutput)->bufferHandle,
           "overwriting a descriptor with an ordinary scalar clears the old resource binding");
    executor.commit().wait();
    expect(firstOutput.copyToData(words) && words[0] == 17u, "a scalar overwrite reaches the shader unchanged");

    write.params.outputIndex = 7u;
    expect(throws<std::invalid_argument>([&] { write.outputs[7] = layer.storeSampledDescriptor(); }),
           "a sampled image cannot occupy a fixed storage buffer slot");
    write.outputs[7] = secondOutput;
    write.params.value = 23u;
    executor << write(1);
    const auto fixed = commandAccesses(std::get<RecordedCommand>(pending.back()));
    expect(fixed.size() == 1 && fixed.front().buffer == ResourceManager::get(secondOutput)->bufferHandle &&
           fixed.front().access == (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT),
           "a generated direct RW buffer field uses the access declared by Slang");
    executor.commit().wait();
    expect(secondOutput.copyToData(words) && words[0] == 23u, "the shader can select a bound fixed slot by number");
}

template<const EmbeddedShader::ShaderCodeModule& Module>
struct TestComputeStage {
    static constexpr auto shaderStage = EmbeddedShader::ShaderStage::ComputeShader;
    static constexpr auto& shaderModule = Module;
};

void test_mixed_descriptor_slots()
{
    using namespace EmbeddedShader;
    using Bind = ShaderCodeModule::ShaderResources::BindType;
    HardwareBuffer destination(std::array{0u}, HardwareBuffer::Usage::StorageBuffer);
    HardwareBuffer source(std::array{41u}, HardwareBuffer::Usage::StorageBuffer);
    // Make the fixed read slot coincide with the destination's actual global descriptor.
    auto* native = ResourceManager::get(destination);
    const uint32_t slot = native->resourceManager->storeDescriptor(*native);
    static const auto module = compileBufferAccess(
        "buffers[params.index][0] = buffers[" + std::to_string(slot) + "][0] + 1;");
    ComputePipeline<> pipeline(ComputeEntry{TestComputeStage<module>{}});
    BoundField<ComputePipeline<>> index(&pipeline, 0, sizeof(uint32_t), Bind::pushConstantMembers, 0);
    BoundField<ComputePipeline<>> fixed(&pipeline, 0, 0, Bind::storageBuffer, 0, 1, 0, 0);
    index = destination.storeWriteDescriptor();
    fixed[slot] = source.storeReadDescriptor();
    expect(throws<std::invalid_argument>([&] { (void)index[0]; }), "a push constant cannot bind a descriptor array element");
    BoundField<ComputePipeline<>> bounded(&pipeline, 0, 0, Bind::storageBuffer, 0, 1, 0, 2);
    expect(throws<std::out_of_range>([&] { (void)bounded[2]; }), "finite descriptor arrays enforce the reflected extent");
    const auto command = pipeline(1);
    HardwareExecutor executor;
    for (const uint32_t value : {41u, 8u}) {
        expect(source.copyFromData(&value, sizeof(value)), "update the fixed slot before replay");
        executor << command;
        executor.commit().wait();
        std::array<uint32_t, 1> result{};
        expect(destination.copyToData(result) && result[0] == value + 1 &&
               source.copyToData(result) && result[0] == value,
               "fixed reads and managed writes retain distinct resources through a private descriptor collision");
    }
    const auto detached = [&] {
        auto original = HardwareBuffer(std::array{50u}, HardwareBuffer::Usage::StorageBuffer).storeReadDescriptor();
        auto copy = original;
        original = source.storeReadDescriptor();
        const auto moved = std::move(copy);
        expect(throws<std::invalid_argument>([&] { fixed[slot] = copy; }), "a moved-from descriptor rejects binding");
        fixed[slot] = moved;
        return pipeline(1);
    }();
    fixed[slot] = source.storeReadDescriptor();
    executor << detached;
    executor.commit().wait();
    std::array<uint32_t, 1> result{};
    expect(destination.copyToData(result) && result[0] == 51, "a saved command retains its buffer after all descriptor copies expire");

    BoundField<ComputePipeline<>> readAlias(&pipeline, 0, 0, Bind::storageBuffer, 0, 1, 0, 0,
                                            Ast::AccessPermissions::ReadAndWrite, true);
    auto writeAlias = readAlias;
    const auto read = source.storeReadDescriptor();
    readAlias[slot] = read;
    writeAlias[slot] = source.storeWriteDescriptor();
    executor << pipeline(1);
    const auto aliases = commandAccesses(std::get<RecordedCommand>(HardwareBackendAccess::state(executor).pending.back()));
    expect(std::ranges::any_of(aliases, [&](const auto& use) {
        return use.buffer == ResourceManager::get(source)->bufferHandle &&
               use.access == (VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
    }), "assigning either name of a shared fixed slot preserves access through all aliases");
    fixed[slot] = read;
    executor << pipeline(1);
    const auto reused = commandAccesses(std::get<RecordedCommand>(HardwareBackendAccess::state(executor).pending.back()));
    expect(std::ranges::any_of(reused, [&](const auto& use) {
        return use.buffer == ResourceManager::get(source)->bufferHandle && use.access == VK_ACCESS_2_SHADER_READ_BIT;
    }), "reusing a descriptor after an alias binding preserves its original read access");
    executor.commit().wait();
}

void test_bounded_descriptor_replay()
{
    using namespace EmbeddedShader;
    using Bind = ShaderCodeModule::ShaderResources::BindType;
    static const auto module = compileBufferAccess(
        "uint old; InterlockedAdd(buffers[params.index][0], buffers[params.alternate][0], old);", 2);
    ComputePipeline<> pipeline(ComputeEntry{TestComputeStage<module>{}});
    BoundField<ComputePipeline<>> target(&pipeline, 0, sizeof(uint32_t), Bind::pushConstantMembers, 0);
    BoundField<ComputePipeline<>> source(&pipeline, sizeof(uint32_t), sizeof(uint32_t), Bind::pushConstantMembers, 0);

    // At least one of three live global slots cannot index the shader's two-element table.
    std::array<HardwareBuffer, 3> buffers;
    for (auto& buffer : buffers) {
        buffer = HardwareBuffer(std::array{2u}, HardwareBuffer::Usage::StorageBuffer);
        auto* native = ResourceManager::get(buffer);
        (void)native->resourceManager->storeDescriptor(*native);
    }
    std::ranges::sort(buffers, {}, [](const auto& buffer) { return ResourceManager::get(buffer)->bindlessIndex; });
    auto& a = buffers.back();
    auto& b = buffers.front();
    const uint32_t initial = 3;
    expect(b.copyFromData(&initial, sizeof(initial)), "initialize the second atomic operand");
    target = a.storeReadWriteDescriptor();
    source = b.storeReadDescriptor();
    const auto forward = pipeline(1);
    target = b.storeReadWriteDescriptor();
    source = a.storeReadDescriptor();
    const auto reverse = pipeline(1);

    HardwareExecutor executor;
    executor << forward << reverse << forward;
    const auto& pending = HardwareBackendAccess::state(executor).pending;
    for (size_t i = 0; i < pending.size(); ++i) {
        const auto uses = commandAccesses(std::get<RecordedCommand>(pending[i]));
        const auto has = [&](const HardwareBuffer& buffer, VkAccessFlags2 access) {
            return std::ranges::any_of(uses, [&](const auto& use) {
                return use.buffer == ResourceManager::get(buffer)->bufferHandle &&
                       use.stages == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT && use.access == access;
            });
        };
        expect(uses.size() == 2 && has(i == 1 ? a : b, VK_ACCESS_2_SHADER_READ_BIT) &&
               has(i == 1 ? b : a, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT),
               "saved atomic dispatches keep the direction and identity of both descriptors");
    }
    executor.commit().wait();
    std::array<uint32_t, 1> word{};
    expect(a.copyToData(word) && word[0] == 13 && b.copyToData(word) && word[0] == 8,
           "finite descriptor tables remap managed indices and synchronize write/read reversals on replay");
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
    const auto& pending = HardwareBackendAccess::state(executor).pending;
    for (size_t i = 0; i < pending.size(); ++i) {
        const auto accesses = commandAccesses(std::get<RecordedCommand>(pending[i]));
        const bool readsInput = i == 1;
        expect(accesses.size() == (readsInput ? 2 : 1) &&
               std::ranges::any_of(accesses, [&](const auto& use) {
                   return use.buffer == ResourceManager::get(outputBuffer)->bufferHandle && use.access == VK_ACCESS_2_SHADER_WRITE_BIT;
               }) &&
               (!readsInput || std::ranges::any_of(accesses, [&](const auto& use) {
                   return use.buffer == ResourceManager::get(inputBuffer)->bufferHandle && use.access == VK_ACCESS_2_SHADER_READ_BIT;
               })), "the saved access list follows the selected EDSL variant");
    }
    executor.commit().wait();
    expect(outputBuffer.copyToData(words) && words == std::array{10u, 53u, 12u},
           "queued false/true/false dispatches retain their shader variant, buffer bindings and slot");

    const uint32_t nextInput = 7;
    expect(inputBuffer.copyFromData(&nextInput, sizeof(nextInput)), "update the input before replay");
    executor << replay;
    executor.commit().wait();
    expect(outputBuffer.copyToData(words) && words == std::array{10u, 18u, 12u},
           "a saved dispatch reuses its shader variant, input buffer and slot after pipeline destruction");

    HardwareBuffer counter(std::array{3u}, HardwareBuffer::Usage::StorageBuffer);
    ArrayProxy<uint32_t> total(counter), output(outputBuffer), input(inputBuffer);
    ComputePipeline branched([&] {
        Uint index = dispatchThreadID()->x;
        $IF(index == Uint(0)) { compute_write_slang.accumulateValue(total[0], output[index]); }
        $ELSE { output[index] = input[0]; };
    });
    executor << branched(2);
    const auto gpuBranches = commandAccesses(std::get<RecordedCommand>(pending.back()));
    const auto has = [&](HardwareBuffer& buffer, VkAccessFlags2 access) {
        return std::ranges::any_of(gpuBranches, [&](const auto& use) {
            return use.buffer == ResourceManager::get(buffer)->bufferHandle && use.access == access;
        });
    };
    expect(gpuBranches.size() == 3 && has(counter, VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT) &&
           has(outputBuffer, VK_ACCESS_2_SHADER_WRITE_BIT) && has(inputBuffer, VK_ACCESS_2_SHADER_READ_BIT),
           "GPU branches capture both paths and imported Slang out/inout arguments retain their access");
    executor.commit().wait();
    expect(outputBuffer.copyToData(words) && words == std::array{5u, 7u, 12u},
           "both GPU paths write through the captured bindings");
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
        void resolveResourceUses(ResourceAccessList&) override {}
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
    const auto dispatch = edsl(8);
    executor << dispatch;
    executor.commit().wait();
    expect(destination.copyToData(output) && output == std::array{10u, 21u, 32u, 43u, 54u, 65u, 76u, 87u},
           "an EDSL resource read only by an imported helper is bound at dispatch");

    std::cerr << "GPU: generated raster fields after move\n";
    RasterPipeline raster(baseline_vert_slang, baseline_frag_slang);
    using BaselineRaster = RasterPipeline<baseline_vert_slang_t, baseline_frag_slang_t>;
    static_assert(std::same_as<decltype(raster), BaselineRaster>);
    static_assert(std::same_as<decltype(&BaselineRaster::vp), decltype(raster.vp) BaselineRaster::*>);
    static_assert(std::same_as<decltype(&BaselineRaster::outColor), decltype(raster.outColor) BaselineRaster::*>);
    static_assert(!std::is_base_of_v<Raster, BaselineRaster>);
    HardwareImage first(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    HardwareImage second(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::StorageImage);
    second.setClearColor(0.75f, 0.f, 0.f, 1.f);
    HardwareImage texture(1, 1, HardwareImage::Format::RGBA32_FLOAT, HardwareImage::Usage::SampledStorageImage);
    const std::array texel{0.25f, 0.f, 0.f, 1.f};
    executor << texture.copyFrom(texel.data());
    using Vertex = std::array<float, 5>; // xyz, uv in the Baseline shader.
    const std::array triangle{Vertex{-1, -1, 0, 0, 0}, Vertex{3, -1, 0, 0, 0}, Vertex{-1, 3, 0, 0, 0}};
    HardwareBuffer vertices(triangle, HardwareBuffer::Usage::VertexBuffer);
    HardwareBuffer indices(std::array<uint32_t, 3>{0, 1, 2}, HardwareBuffer::Usage::IndexBuffer);
    HardwareBuffer packets(std::array{Raster::IndirectPacket{.vertexCount = 3, .instanceCount = 1}}, HardwareBuffer::Usage::RasterIndirect);
    const auto model = ktm::fmat4x4::from_eye();
    HardwareBuffer drawParameters(sizeof(model), HardwareBuffer::Usage::StorageBuffer, &model);

    auto movedRaster = std::move(raster);
    movedRaster.vp.texIndex = texture.storeSampledDescriptor();
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

    raster.vp.view = ktm::fmat4x4::from_eye();
    raster.vp.texIndex = 3u;
    raster.combinedTextureSamplerHandles[3] = texture;
    executor << raster(vertices, indices, packets, 1, drawParameters) << second.copyTo(readback);
    executor.commit().wait();
    expect(readback.copyToData(pixels.data(), sizeof(pixels[0])) && pixels[0] == texel,
           "a reflected sampler field binds a sampled/storage image to the fixed sampling slot");
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
        const std::string_view mode = argc == 2 ? argv[1] : "";
        if (mode == "--gpu" || mode == "--mesh" || mode == "--task-mesh")
        {
            if (!has_vulkan_14_device())
            {
                std::cout << "SKIP: no Vulkan 1.4 loader/device\n";
                return 77;
            }
            if (mode != "--gpu")
            {
                const auto& features = getHardwareContext().getMainDevice()->deviceManager.getFeaturesUtils().meshShaderFeatures;
                if (!features.meshShader || (mode == "--task-mesh" && !features.taskShader))
                {
                    std::cout << "SKIP: selected Vulkan device lacks the requested mesh stages\n";
                    return 77;
                }
                if (mode == "--mesh")
                    test_mesh_draws(MeshPipeline(baseline_mesh_slang, baseline_frag_slang), 2);
                else
                    test_mesh_draws(MeshPipeline(mesh_task_slang, baseline_mesh_slang, baseline_frag_slang), 1);
                std::cout << "Mesh pipeline GPU checks passed\n";
                return 0;
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
            test_mixed_descriptor_slots();
            test_bounded_descriptor_replay();
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
