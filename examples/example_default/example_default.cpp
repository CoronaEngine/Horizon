#pragma once

#include <ktm/ktm.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "horizon.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "common.h"
#include "corona/kernel/core/i_logger.h"

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

namespace H = Corona::Horizon;

// 通过 CMake helicon_compile_shaders 自动编译生成的 shader 反射头文件
// eDSL 路径不再依赖 GLSL 反射头文件，render target 通过 bindRenderTarget 自动绑定
#include GLSL(shaders/default_vert.glsl)
#include GLSL(shaders/default_frag.glsl)
#include GLSL(shaders/default_compute.glsl)

// storage buffer (used by mesh thread, retained for compatibility)
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

// 精简顶点：只保留 VS 实际使用的 position 和 color
struct SimpleVertex
{
    std::array<float, 3> position;
    std::array<float, 3> color;
};

// Vertex attribute proxy: 与 SimpleVertex 一一对应
struct VertexAttributeProxy
{
    EmbeddedShader::Float3 position;
    EmbeddedShader::Float3 color;
};

void run_example_default()
{
    // Corona::Kernel::CoronaLogger::get_logger()->set_log_level(quill::LogLevel::TraceL3);
    //  setupSignalHandlers();

    // 运行压缩纹理测试（可选）
    // testCompressedTextures();

    //CFW_LOG_INFO("Starting main application...");

    if (glfwInit() < 0)
    {
        return;
    }

    //CFW_LOG_INFO("Main thread started...");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // 每个 pair 创建 2 个窗口: 偶数索引 = EDSL, 奇数索引 = GLSL
    constexpr std::size_t DEMO_PAIR_COUNT = 1;
    constexpr std::size_t TOTAL_WINDOWS = DEMO_PAIR_COUNT * 2;
    std::vector<GLFWwindow *> windows(TOTAL_WINDOWS);
    for (size_t i = 0; i < windows.size(); i++)
    {
        std::string title = (i % 2 == 0) ? "Cabbage Engine [EDSL]" : "Cabbage Engine [GLSL]";
        windows[i] = glfwCreateWindow(1920, 1080, title.c_str(), nullptr, nullptr);
    }

    std::exception_ptr workerException;

    {
        std::vector<H::HardwareImage> finalOutputImages(windows.size());
        // 默认构造的 HardwareExecutor 已自动解析主设备上对应能力的队列，
        // 无需手写 QueueResolver。仅在需要固定/自定义队列调度时才传入 resolver。
        std::vector<std::unique_ptr<H::HardwareExecutor>> renderExecutors;
        renderExecutors.reserve(windows.size());
        for (size_t i = 0; i < windows.size(); ++i)
            renderExecutors.push_back(std::make_unique<H::HardwareExecutor>());
        std::vector<H::HardwareExecutor> displayExecutors(windows.size());
        std::array<H::SubmitReceipt, TOTAL_WINDOWS> latestRenderReceipts;
        std::array<std::mutex, TOTAL_WINDOWS> frameSubmitMutexes;
        for (size_t i = 0; i < finalOutputImages.size(); i++)
        {
            finalOutputImages[i] = H::HardwareImage(
                H::HardwareImageDesc::texture_2d(1920,
                                                 1080,
                                                 H::Format::RGBA16_FLOAT,
                                                 H::ImageUsageFlags::Storage | H::ImageUsageFlags::ColorAttachment |
                                                     H::ImageUsageFlags::Sampled | H::ImageUsageFlags::TransferSrc |
                                                     H::ImageUsageFlags::TransferDst,
                                                 "example_default.output"));
            finalOutputImages[i].set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
        }

        // HardwareBuffer normalBuffer = HardwareBuffer(normals, BufferUsage::VertexBuffer);
        // HardwareBuffer uvBuffer = HardwareBuffer(uvs, BufferUsage::VertexBuffer);
        // HardwareBuffer colorBuffer = HardwareBuffer(colors, BufferUsage::VertexBuffer);

        // 纹理加载 - 选择以下任一方式
        // 方式1: 加载普通纹理
        auto textureResult = loadTexture(defaultTexturePath);

        // 方式2: 加载BC1压缩纹理
        // auto textureResult = loadCompressedTexture(defaultTexturePath, true);

        // 方式3: 加载带有 mipmap 和 array layers 的纹理
        // auto textureResult = loadTextureWithMipmapAndLayers(defaultTexturePath, 2, 5, 1, 0);

        if (!textureResult.success)
        {
            //CFW_LOG_ERROR("Failed to load texture, exiting...");
            for (size_t i = 0; i < windows.size(); i++)
            {
                glfwDestroyWindow(windows[i]);
            }
            glfwTerminate();
            return;
        }

        uint32_t textureID = textureResult.descriptorID;
        H::HardwareImage texture = textureResult.texture;

        std::vector<std::vector<H::HardwareBuffer>> rasterizerStorageBuffers(windows.size());
        //std::vector<HardwareBuffer> computeStorageBuffers(windows.size());

        std::atomic_bool running = true;

        std::mutex workerExceptionMutex;

        auto stopWithWorkerException = [&](std::exception_ptr exception) {
            {
                std::lock_guard lock(workerExceptionMutex);
                if (!workerException)
                    workerException = std::move(exception);
            }
            running.store(false);
        };

        auto colorOnlyDepthStencil = []() {
            H::DepthStencilStateDesc depthStencil;
            depthStencil.depth_test_enabled = false;
            depthStencil.depth_write_enabled = false;
            depthStencil.stencil_test_enabled = false;
            return depthStencil;
        };

        auto meshThread = [&](uint32_t threadIndex) {
            try
            {
            //CFW_LOG_INFO("Mesh thread {} started...", threadIndex);

            //ComputeStorageBufferObject computeUniformData(windows.size());
            //computeStorageBuffers[threadIndex] = HardwareBuffer(sizeof(ComputeStorageBufferObject), BufferUsage::StorageBuffer);

            std::vector<ktm::fmat4x4> modelMat(20);
            std::vector<RasterizerStorageBufferObject> rasterizerStorageBufferObjects(modelMat.size());
            for (size_t i = 0; i < modelMat.size(); i++)
            {
                modelMat[i] = (ktm::translate3d(ktm::fvec3((i % 5) - 2.0f, (i / 5) - 0.5f, 0.0f)) * ktm::scale3d(ktm::fvec3(0.1, 0.1, 0.1)) * ktm::rotate3d_axis(ktm::radians(i * 30.0f), ktm::fvec3(0.0f, 0.0f, 1.0f)));
                RasterizerStorageBufferObject initialData;
                initialData.textureIndex = textureID;
                initialData.model = modelMat[i];
                rasterizerStorageBuffers[threadIndex].push_back(
                    H::HardwareBuffer::from_bytes(as_storage_bytes(initialData),
                                                  sizeof(RasterizerStorageBufferObject),
                                                  H::BufferUsageFlags::TransferSrc | H::BufferUsageFlags::TransferDst | H::BufferUsageFlags::Storage));
            }

            auto startTime = std::chrono::high_resolution_clock::now();
            uint64_t frameCount = 0;

            while (running.load())
            {
                // 等待上一帧显示完成（或初始状态）
                /*meshSemaphores[threadIndex]->acquire();
                if (!running.load()) break;*/

                float currentTime = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - startTime).count();

                for (size_t i = 0; i < rasterizerStorageBuffers[threadIndex].size(); i++)
                {
                    // rasterizerUniformBufferObject[i].textureIndex = texture[0][0].storeDescriptor();
                    rasterizerStorageBufferObjects[i].textureIndex = textureID;
                    rasterizerStorageBufferObjects[i].model = modelMat[i] * ktm::rotate3d_axis(currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                    (void)rasterizerStorageBuffers[threadIndex][i].write_bytes(as_storage_bytes(rasterizerStorageBufferObjects[i]));
                }

                //computeUniformData.imageID = finalOutputImages[threadIndex].storeDescriptor();
                //computeStorageBuffers[threadIndex].copyFromData(&computeUniformData, sizeof(computeUniformData));

                ++frameCount;

                // 通知渲染线程可以开始
                // renderSemaphores[threadIndex]->release();
            }
            // 退出时释放后续信号量，防止死锁
            // renderSemaphores[threadIndex]->release();

            //CFW_LOG_INFO("Mesh thread {} ended.", threadIndex);
            }
            catch (...)
            {
                stopWithWorkerException(std::current_exception());
            }
        };

        // =====================================================================
        // renderThreadEDSL: EDSL 路径 — C++ lambda 定义 shader，自动绑定资源
        // =====================================================================
        // CPU-side MVP pre-transform helper (shared by EDSL and GLSL paths)
        // Replicates the same model matrices as the mesh thread, applies
        // view/proj and perspective divide so that the VS just does:
        //   gl_Position = vec4(inPosition, 1.0);
        // =====================================================================
        // 从 CubeData 中提取 position+color
        std::vector<SimpleVertex> simpleVertices(vertices.size());
        for (size_t i = 0; i < vertices.size(); i++)
        {
            simpleVertices[i].position = vertices[i].position;
            simpleVertices[i].color = vertices[i].color;
        }

        auto transformVerticesForObject = [](const std::vector<SimpleVertex>& src,
                                             const ktm::fmat4x4& mvp,
                                             std::vector<SimpleVertex>& dst)
        {
            dst = src;
            for (auto& v : dst)
            {
                ktm::fvec4 clip = mvp * ktm::fvec4(v.position[0], v.position[1], v.position[2], 1.0f);
                float invW = 1.0f / clip[3];
                v.position = {clip[0] * invW, clip[1] * invW, clip[2] * invW};
            }
        };

        // Shared camera constants (same as RasterizerStorageBufferObject defaults)
        auto viewMat = ktm::look_at_lh(ktm::fvec3(2.0f, 2.0f, 2.0f),
                                        ktm::fvec3(0.0f, 0.0f, 0.0f),
                                        ktm::fvec3(0.0f, 0.0f, 1.0f));
        auto projMat = ktm::perspective_lh(ktm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 10.0f);
        auto vpMat = projMat * viewMat;

        // Base model matrices (same formula as mesh thread)
        constexpr size_t OBJECT_COUNT = 20;
        std::vector<ktm::fmat4x4> baseModelMat(OBJECT_COUNT);
        for (size_t i = 0; i < OBJECT_COUNT; i++)
        {
            baseModelMat[i] = ktm::translate3d(ktm::fvec3(static_cast<float>(i % 5) - 2.0f,
                                                           static_cast<float>(i / 5) - 0.5f, 0.0f))
                            * ktm::scale3d(ktm::fvec3(0.1f, 0.1f, 0.1f))
                            * ktm::rotate3d_axis(ktm::radians(static_cast<float>(i) * 30.0f),
                                                 ktm::fvec3(0.0f, 0.0f, 1.0f));
        }

        // =====================================================================
        // renderThreadEDSL: EDSL 路径 — C++ lambda 定义 shader，自动绑定资源
        // =====================================================================
        auto renderThreadEDSL = [&](uint32_t threadIndex) {
            try
            {
            using namespace EmbeddedShader;
            using namespace EmbeddedShader::Ast;
            using namespace ktm;

            // Texture2D proxy 声明时直接绑定已有 HardwareImage
            Texture2D<fvec4> inputImageRGBA16 = finalOutputImages[threadIndex];

            // EDSL compute shader: ACES filmic tone mapping
            auto acesFilmicToneMapCurve = [&](Float3 x)
            {
                Float a = 2.51f;
                Float b = 0.03f;
                Float c = 2.43f;
                Float d = 0.59f;
                Float e = 0.14f;

                return clamp((x * (a * x + b)) / (x * (c * x + d) + e), fvec3(0.0f), fvec3(1.0f));
            };

            auto compute = [&]
            {
                Float4 color = inputImageRGBA16[dispatchThreadID()->xy()];
                inputImageRGBA16[dispatchThreadID()->xy()] = Float4(acesFilmicToneMapCurve(color->xyz()), 1.f);
            };

            // EDSL vertex shader: pass-through (MVP 已在 CPU 端完成)
            auto vsLambda = [&](Aggregate<VertexAttributeProxy> vertex) -> Float4
            {
                position() = Float4(vertex->position, 1.0f);
                return Float4(vertex->color, 1.0f);
            };

            // EDSL fragment shader: 直接输出插值后的顶点颜色
            auto fsLambda = [&](Float4 interpolatedColor) -> Float4
            {
                return interpolatedColor;
            };

            // 从 lambda 创建管线，bindOutputTargets 自动绑定 render target
            auto rasterizerDesc = H::RasterizerPipelineDesc::from_edsl(vsLambda, fsLambda);
            rasterizerDesc.set_depth_stencil(colorOnlyDepthStencil());
            H::RasterizerPipeline rasterizer(std::move(rasterizerDesc));
            rasterizer.bind_output_targets(finalOutputImages[threadIndex]);

            // 从 lambda 创建 compute 管线，auto-bind 资源
            H::ComputePipeline computer(H::ComputePipelineDesc::from_edsl(compute, uvec3(8, 8, 1)));

            auto startTime = std::chrono::high_resolution_clock::now();
            H::HardwareBuffer indexBuffer = H::HardwareBuffer::index(indices);
            std::vector<H::HardwareBuffer> vertexBuffers;
            std::vector<std::vector<SimpleVertex>> transformedVertices(OBJECT_COUNT, simpleVertices);
            vertexBuffers.reserve(OBJECT_COUNT);
            for (size_t i = 0; i < OBJECT_COUNT; i++)
                vertexBuffers.push_back(H::HardwareBuffer::vertex(transformedVertices[i]));

            while (running.load())
            {
                float currentTime = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - startTime).count();

                rasterizer.clear_records();
                for (size_t i = 0; i < OBJECT_COUNT; i++)
                {
                    auto model = baseModelMat[i] * ktm::rotate3d_axis(
                        currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                    transformVerticesForObject(simpleVertices, vpMat * model, transformedVertices[i]);
                    (void)vertexBuffers[i].write(std::span<const SimpleVertex>(transformedVertices[i].data(), transformedVertices[i].size()));
                    rasterizer.record(indexBuffer, vertexBuffers[i]);
                }

                {
                    std::lock_guard lock(frameSubmitMutexes[threadIndex]);
                    latestRenderReceipts[threadIndex] = renderExecutors[threadIndex]->stream()
                        << rasterizer(1920, 1080).command_batch()
                        << computer(1920 / 8, 1080 / 8, 1).command_batch()
                        << H::commit();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            }
            catch (...)
            {
                stopWithWorkerException(std::current_exception());
            }
        };

        // =====================================================================
        // renderThreadGLSL: 手写 GLSL 路径 — 预编译 SPIR-V + CPU MVP 预变换
        // =====================================================================
        auto renderThreadGLSL = [&](uint32_t threadIndex) {
            try
            {
            // 简化后的 shader 无需 push constant / UBO，仅做 pass-through
            H::RasterizerPipelineDesc rasterizerDesc(
                H::PipelineShaderDesc::from_slang_module(H::PipelineShaderStage::Vertex, default_vert_glsl::slangModule),
                H::PipelineShaderDesc::from_slang_module(H::PipelineShaderStage::Fragment, default_frag_glsl::slangModule));
            rasterizerDesc.set_depth_stencil(colorOnlyDepthStencil());
            H::RasterizerPipeline rasterizer(std::move(rasterizerDesc));

            // 绑定 render target
            rasterizer[default_frag_glsl::outColor] = finalOutputImages[threadIndex];

            // compute 管线保持不变
            H::ComputePipeline computer(H::ComputePipelineDesc(
                H::PipelineShaderDesc::from_slang_module(H::PipelineShaderStage::Compute, default_compute_glsl::slangModule),
                ktm::uvec3(8, 8, 1)));
            uint32_t computeImageDescriptorID = finalOutputImages[threadIndex].store_storage_descriptor();
            computer[default_compute_glsl::globalParams::imageID] = computeImageDescriptorID;

            auto startTime = std::chrono::high_resolution_clock::now();
            H::HardwareBuffer indexBuffer = H::HardwareBuffer::index(indices);
            std::vector<H::HardwareBuffer> vertexBuffers;
            std::vector<std::vector<SimpleVertex>> transformedVertices(OBJECT_COUNT, simpleVertices);
            vertexBuffers.reserve(OBJECT_COUNT);
            for (size_t i = 0; i < OBJECT_COUNT; i++)
                vertexBuffers.push_back(H::HardwareBuffer::vertex(transformedVertices[i]));

            while (running.load())
            {
                float currentTime = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - startTime).count();

                rasterizer.clear_records();
                for (size_t i = 0; i < OBJECT_COUNT; i++)
                {
                    auto model = baseModelMat[i] * ktm::rotate3d_axis(
                        currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                    transformVerticesForObject(simpleVertices, vpMat * model, transformedVertices[i]);
                    (void)vertexBuffers[i].write(std::span<const SimpleVertex>(transformedVertices[i].data(), transformedVertices[i].size()));
                    rasterizer.record(indexBuffer, vertexBuffers[i]);
                }

                {
                    std::lock_guard lock(frameSubmitMutexes[threadIndex]);
                    latestRenderReceipts[threadIndex] = renderExecutors[threadIndex]->stream()
                        << rasterizer(1920, 1080).command_batch()
                        << computer(1920 / 8, 1080 / 8, 1).command_batch()
                        << H::commit();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
            }
            catch (...)
            {
                stopWithWorkerException(std::current_exception());
            }
        };

        auto displayThread = [&](uint32_t threadIndex) {
            try
            {
            //CFW_LOG_INFO("Display thread {} started...", threadIndex);

            H::HardwareDisplayer displayManager = H::HardwareDisplayer(glfwGetWin32Window(windows[threadIndex]));

            auto startTime = std::chrono::high_resolution_clock::now();
            uint64_t frameCount = 0;

            while (running.load())
            {
                float time = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - startTime).count();
                // CFW_LOG_INFO("Display thread {} frame {} at {:.3f}s", threadIndex, frameCount, time);

                {
                    std::lock_guard lock(frameSubmitMutexes[threadIndex]);
                    displayExecutors[threadIndex].wait(latestRenderReceipts[threadIndex]);
                    // DLSS-style insertion point: display consumes the rendered image,
                    // then present copies it into the swapchain.
                    (void)(displayExecutors[threadIndex].stream()
                        << H::present(displayManager, finalOutputImages[threadIndex])
                        << H::commit());
                }
                ++frameCount;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));

                // 通知 Mesh 线程开始下一帧
                // meshSemaphores[threadIndex]->release();
            }
            // meshSemaphores[threadIndex]->release();

            //CFW_LOG_INFO("Display thread {} ended.", threadIndex);
            }
            catch (...)
            {
                stopWithWorkerException(std::current_exception());
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
            {
                std::lock_guard lock(workerExceptionMutex);
                if (workerException)
                {
                    running.store(false);
                    break;
                }
            }
            for (size_t i = 0; i < windows.size(); i++)
            {
                if (glfwWindowShouldClose(windows[i]))
                {
                    running.store(false);
                    break;
                }
            }
        }

        for (size_t i = 0; i < windows.size(); i++)
        {
            if (meshThreads[i].joinable())
                meshThreads[i].join();
            if (renderThreads[i].joinable())
                renderThreads[i].join();
            if (displayThreads[i].joinable())
                displayThreads[i].join();
        }
    }

    for (size_t i = 0; i < windows.size(); i++)
    {
        glfwDestroyWindow(windows[i]);
    }

    glfwTerminate();

    if (workerException)
        std::rethrow_exception(workerException);

}
