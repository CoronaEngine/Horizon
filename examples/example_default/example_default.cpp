#pragma once

#include <ktm/ktm.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "horizon.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "common.h"

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

#include GLSL(shaders/default_vert.glsl)
#include GLSL(shaders/default_frag.glsl)
#include GLSL(shaders/default_compute.glsl)

struct RasterizerStorageBufferObject
{
    uint32_t textureIndex;
    ktm::fmat4x4 model = ktm::rotate3d_axis(ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
    ktm::fmat4x4 view = ktm::look_at_lh(ktm::fvec3(2.0f, 2.0f, 2.0f), ktm::fvec3(0.0f, 0.0f, 0.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
    ktm::fmat4x4 proj = ktm::perspective_lh(ktm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 10.0f);
    ktm::fvec3 viewPos = ktm::fvec3(2.0f, 2.0f, 2.0f);
    ktm::fvec3 lightColor = ktm::fvec3(10.0f, 10.0f, 10.0f);
    ktm::fvec3 lightPos = ktm::fvec3(1.0f, 1.0f, 1.0f);
};

std::span<const std::byte> as_storage_bytes(const RasterizerStorageBufferObject& value)
{
    return std::as_bytes(std::span<const RasterizerStorageBufferObject>(&value, 1));
}

struct SimpleVertex
{
    std::array<float, 3> position;
    std::array<float, 3> color;
};

struct VertexAttributeProxy
{
    EmbeddedShader::Float3 position;
    EmbeddedShader::Float3 color;
};

void run_example_default()
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    constexpr std::size_t TOTAL_WINDOWS = 16;
    std::vector<GLFWwindow*> windows(TOTAL_WINDOWS);
    for (size_t i = 0; i < windows.size(); i++)
    {
        const char* title = (i % 2 == 0) ? "Cabbage Engine [EDSL]" : "Cabbage Engine [GLSL]";
        windows[i] = glfwCreateWindow(1920, 1080, title, nullptr, nullptr);
    }

    std::vector<Corona::Horizon::HardwareImage> finalOutputImages(windows.size());
    std::vector<std::unique_ptr<Corona::Horizon::HardwareExecutor>> renderExecutors;
    for (size_t i = 0; i < windows.size(); ++i)
        renderExecutors.push_back(std::make_unique<Corona::Horizon::HardwareExecutor>());
    std::vector<Corona::Horizon::HardwareExecutor> displayExecutors(windows.size());
    std::array<Corona::Horizon::SubmitReceipt, TOTAL_WINDOWS> latestRenderReceipts;
    std::array<std::mutex, TOTAL_WINDOWS> frameSubmitMutexes;
    for (size_t i = 0; i < finalOutputImages.size(); i++)
    {
        finalOutputImages[i] = Corona::Horizon::HardwareImage(Corona::Horizon::HardwareImageDesc::texture_2d(
            1920, 1080, Corona::Horizon::Format::RGBA16_FLOAT,
            Corona::Horizon::ImageUsageFlags::Storage | Corona::Horizon::ImageUsageFlags::ColorAttachment |
                Corona::Horizon::ImageUsageFlags::Sampled | Corona::Horizon::ImageUsageFlags::TransferSrc |
                Corona::Horizon::ImageUsageFlags::TransferDst,
            "example_default.output"));
        finalOutputImages[i].set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
    }

    uint32_t textureID = loadTexture(defaultTexturePath).descriptorID;

    std::vector<std::vector<Corona::Horizon::HardwareBuffer>> rasterizerStorageBuffers(windows.size());
    std::atomic_bool running = true;

    auto colorOnlyDepthStencil = []() {
        Corona::Horizon::DepthStencilStateDesc depthStencil;
        depthStencil.depth_test_enabled = false;
        depthStencil.depth_write_enabled = false;
        depthStencil.stencil_test_enabled = false;
        return depthStencil;
    };

    auto meshThread = [&](uint32_t threadIndex) {
        std::vector<ktm::fmat4x4> modelMat(20);
        std::vector<RasterizerStorageBufferObject> rasterizerStorageBufferObjects(modelMat.size());
        for (size_t i = 0; i < modelMat.size(); i++)
        {
            modelMat[i] = ktm::translate3d(ktm::fvec3((i % 5) - 2.0f, (i / 5) - 0.5f, 0.0f)) * ktm::scale3d(ktm::fvec3(0.1, 0.1, 0.1)) * ktm::rotate3d_axis(ktm::radians(i * 30.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
            RasterizerStorageBufferObject initialData;
            initialData.textureIndex = textureID;
            initialData.model = modelMat[i];
            rasterizerStorageBuffers[threadIndex].push_back(Corona::Horizon::HardwareBuffer::from_bytes(
                as_storage_bytes(initialData), sizeof(RasterizerStorageBufferObject),
                Corona::Horizon::BufferUsageFlags::TransferSrc | Corona::Horizon::BufferUsageFlags::TransferDst | Corona::Horizon::BufferUsageFlags::Storage));
        }

        auto startTime = std::chrono::high_resolution_clock::now();
        while (running.load())
        {
            float currentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();
            for (size_t i = 0; i < rasterizerStorageBuffers[threadIndex].size(); i++)
            {
                rasterizerStorageBufferObjects[i].textureIndex = textureID;
                rasterizerStorageBufferObjects[i].model = modelMat[i] * ktm::rotate3d_axis(currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                (void)rasterizerStorageBuffers[threadIndex][i].write_bytes(as_storage_bytes(rasterizerStorageBufferObjects[i]));
            }
        }
    };

    std::vector<SimpleVertex> simpleVertices(vertices.size());
    for (size_t i = 0; i < vertices.size(); i++)
    {
        simpleVertices[i].position = vertices[i].position;
        simpleVertices[i].color = vertices[i].color;
    }

    auto transformVerticesForObject = [](const std::vector<SimpleVertex>& src, const ktm::fmat4x4& mvp, std::vector<SimpleVertex>& dst) {
        dst = src;
        for (auto& v : dst)
        {
            ktm::fvec4 clip = mvp * ktm::fvec4(v.position[0], v.position[1], v.position[2], 1.0f);
            float invW = 1.0f / clip[3];
            v.position = {clip[0] * invW, clip[1] * invW, clip[2] * invW};
        }
    };

    auto viewMat = ktm::look_at_lh(ktm::fvec3(2.0f, 2.0f, 2.0f), ktm::fvec3(0.0f, 0.0f, 0.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
    auto projMat = ktm::perspective_lh(ktm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 10.0f);
    auto vpMat = projMat * viewMat;

    constexpr size_t OBJECT_COUNT = 20;
    std::vector<ktm::fmat4x4> baseModelMat(OBJECT_COUNT);
    for (size_t i = 0; i < OBJECT_COUNT; i++)
    {
        baseModelMat[i] = ktm::translate3d(ktm::fvec3(static_cast<float>(i % 5) - 2.0f, static_cast<float>(i / 5) - 0.5f, 0.0f)) * ktm::scale3d(ktm::fvec3(0.1f, 0.1f, 0.1f)) * ktm::rotate3d_axis(ktm::radians(static_cast<float>(i) * 30.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
    }

    // EDSL 路径：C++ lambda 定义 shader，资源自动绑定
    auto renderThreadEDSL = [&](uint32_t threadIndex) {
        using namespace EmbeddedShader;
        using namespace EmbeddedShader::Ast;
        using namespace ktm;

        Texture2D<fvec4> inputImageRGBA16 = finalOutputImages[threadIndex];

        auto acesFilmicToneMapCurve = [&](Float3 x) {
            Float a = 2.51f;
            Float b = 0.03f;
            Float c = 2.43f;
            Float d = 0.59f;
            Float e = 0.14f;
            return clamp((x * (a * x + b)) / (x * (c * x + d) + e), fvec3(0.0f), fvec3(1.0f));
        };

        auto compute = [&] {
            Float4 color = inputImageRGBA16[dispatchThreadID()->xy()];
            inputImageRGBA16[dispatchThreadID()->xy()] = Float4(acesFilmicToneMapCurve(color->xyz()), 1.f);
        };

        auto vsLambda = [&](Aggregate<VertexAttributeProxy> vertex) -> Float4 {
            position() = Float4(vertex->position, 1.0f);
            return Float4(vertex->color, 1.0f);
        };

        auto fsLambda = [&](Float4 interpolatedColor) -> Float4 {
            return interpolatedColor;
        };

        auto rasterizerDesc = Corona::Horizon::RasterizerPipelineDesc::from_edsl(vsLambda, fsLambda);
        rasterizerDesc.set_depth_stencil(colorOnlyDepthStencil());
        Corona::Horizon::RasterizerPipeline rasterizer(std::move(rasterizerDesc));
        rasterizer.bind_output_targets(finalOutputImages[threadIndex]);

        Corona::Horizon::ComputePipeline computer(Corona::Horizon::ComputePipelineDesc::from_edsl(compute, uvec3(8, 8, 1)));

        auto startTime = std::chrono::high_resolution_clock::now();
        Corona::Horizon::HardwareBuffer indexBuffer = Corona::Horizon::HardwareBuffer::index(indices);
        std::vector<Corona::Horizon::HardwareBuffer> vertexBuffers;
        std::vector<std::vector<SimpleVertex>> transformedVertices(OBJECT_COUNT, simpleVertices);
        for (size_t i = 0; i < OBJECT_COUNT; i++)
            vertexBuffers.push_back(Corona::Horizon::HardwareBuffer::vertex(transformedVertices[i]));

        while (running.load())
        {
            float currentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

            rasterizer.clear_records();
            for (size_t i = 0; i < OBJECT_COUNT; i++)
            {
                auto model = baseModelMat[i] * ktm::rotate3d_axis(currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                transformVerticesForObject(simpleVertices, vpMat * model, transformedVertices[i]);
                (void)vertexBuffers[i].write(std::span<const SimpleVertex>(transformedVertices[i].data(), transformedVertices[i].size()));
                rasterizer.record(indexBuffer, vertexBuffers[i]);
            }

            {
                std::lock_guard lock(frameSubmitMutexes[threadIndex]);
                latestRenderReceipts[threadIndex] = *renderExecutors[threadIndex] << rasterizer(1920, 1080) << computer(1920 / 8, 1080 / 8, 1) << Corona::Horizon::submit;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    };

    // GLSL 路径：预编译 SPIR-V + CPU 端 MVP 预变换
    auto renderThreadGLSL = [&](uint32_t threadIndex) {
        Corona::Horizon::RasterizerPipelineDesc rasterizerDesc(
            Corona::Horizon::PipelineShaderDesc::from_slang_module(Corona::Horizon::PipelineShaderStage::Vertex, default_vert_glsl::slangModule),
            Corona::Horizon::PipelineShaderDesc::from_slang_module(Corona::Horizon::PipelineShaderStage::Fragment, default_frag_glsl::slangModule));
        rasterizerDesc.set_depth_stencil(colorOnlyDepthStencil());
        Corona::Horizon::RasterizerPipeline rasterizer(std::move(rasterizerDesc));
        rasterizer[default_frag_glsl::outColor] = finalOutputImages[threadIndex];

        Corona::Horizon::ComputePipeline computer(Corona::Horizon::ComputePipelineDesc(
            Corona::Horizon::PipelineShaderDesc::from_slang_module(Corona::Horizon::PipelineShaderStage::Compute, default_compute_glsl::slangModule),
            ktm::uvec3(8, 8, 1)));
        computer[default_compute_glsl::globalParams::imageID] = finalOutputImages[threadIndex].store_storage_descriptor();

        auto startTime = std::chrono::high_resolution_clock::now();
        Corona::Horizon::HardwareBuffer indexBuffer = Corona::Horizon::HardwareBuffer::index(indices);
        std::vector<Corona::Horizon::HardwareBuffer> vertexBuffers;
        std::vector<std::vector<SimpleVertex>> transformedVertices(OBJECT_COUNT, simpleVertices);
        for (size_t i = 0; i < OBJECT_COUNT; i++)
            vertexBuffers.push_back(Corona::Horizon::HardwareBuffer::vertex(transformedVertices[i]));

        while (running.load())
        {
            float currentTime = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

            rasterizer.clear_records();
            for (size_t i = 0; i < OBJECT_COUNT; i++)
            {
                auto model = baseModelMat[i] * ktm::rotate3d_axis(currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                transformVerticesForObject(simpleVertices, vpMat * model, transformedVertices[i]);
                (void)vertexBuffers[i].write(std::span<const SimpleVertex>(transformedVertices[i].data(), transformedVertices[i].size()));
                rasterizer.record(indexBuffer, vertexBuffers[i]);
            }

            {
                std::lock_guard lock(frameSubmitMutexes[threadIndex]);
                latestRenderReceipts[threadIndex] = *renderExecutors[threadIndex] << rasterizer(1920, 1080) << computer(1920 / 8, 1080 / 8, 1) << Corona::Horizon::submit;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    };

    auto displayThread = [&](uint32_t threadIndex) {
        Corona::Horizon::HardwareDisplayer displayManager(glfwGetWin32Window(windows[threadIndex]));
        while (running.load())
        {
            {
                std::lock_guard lock(frameSubmitMutexes[threadIndex]);
                displayExecutors[threadIndex].wait(latestRenderReceipts[threadIndex]);
                (void)(displayExecutors[threadIndex].stream() << Corona::Horizon::present(displayManager, finalOutputImages[threadIndex]) << Corona::Horizon::commit());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    };

    std::vector<std::thread> meshThreads;
    std::vector<std::thread> renderThreads;
    std::vector<std::thread> displayThreads;
    for (size_t i = 0; i < windows.size(); i++)
    {
        const auto threadIndex = static_cast<uint32_t>(i);
        meshThreads.emplace_back(meshThread, threadIndex);
        if (i % 2 == 0)
            renderThreads.emplace_back(renderThreadEDSL, threadIndex);
        else
            renderThreads.emplace_back(renderThreadGLSL, threadIndex);
        displayThreads.emplace_back(displayThread, threadIndex);
    }

    while (running.load())
    {
        glfwPollEvents();
        for (size_t i = 0; i < windows.size(); i++)
        {
            if (glfwWindowShouldClose(windows[i]))
                running.store(false);
        }
    }

    for (size_t i = 0; i < windows.size(); i++)
    {
        meshThreads[i].join();
        renderThreads[i].join();
        displayThreads[i].join();
    }

    for (size_t i = 0; i < windows.size(); i++)
        glfwDestroyWindow(windows[i]);
    glfwTerminate();
}
