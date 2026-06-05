#pragma once

#include <ktm/ktm.h>

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "horizon.h"
#include "hardware_wrapper_vulkan/hardware/execution.h"
#include "common.h"
#include "corona/kernel/core/i_logger.h"

#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"

using namespace Corona::Horizon;

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

// 精简顶点：只保留 VS 实际使用的 position 和 color
struct SimpleVertex
{
    std::array<float, 3> position;
    std::array<float, 3> color;
};

struct DefaultFrameSubmitState
{
    std::mutex mutex;
    SubmitReceipt last_render_receipt;
    uint64_t last_presented_render_serial { 0 };
};

struct DefaultOutputImageFormat
{
    Format format;
    const char* glsl_layout;
    const char* name;
};

constexpr std::array defaultOutputImageFormats {
    DefaultOutputImageFormat { Format::RGBA16_FLOAT, "rgba16f", "RGBA16_FLOAT" },
    DefaultOutputImageFormat { Format::RGBA32_FLOAT, "rgba32f", "RGBA32_FLOAT" },
    DefaultOutputImageFormat { Format::RGBA8_UNORM, "rgba8", "RGBA8_UNORM" },
};

// Vertex attribute proxy: 与 SimpleVertex 一一对应
struct VertexAttributeProxy
{
    EmbeddedShader::Float3 position;
    EmbeddedShader::Float3 color;
};

std::string make_default_storage_image_compute_shader(const char* layout)
{
    return std::string(R"GLSL(
#version 450

layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0, )GLSL") + layout + R"GLSL() uniform image2D inputImageRGBA16;

vec3 acesFilmicToneMapCurve(vec3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;

    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec4 color = imageLoad(inputImageRGBA16, ivec2(gl_GlobalInvocationID.xy));
    imageStore(inputImageRGBA16, ivec2(gl_GlobalInvocationID.xy), vec4(acesFilmicToneMapCurve(color.xyz), 1.0));
}
)GLSL";
}

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
    std::vector<GLFWwindow*> windows(TOTAL_WINDOWS);
    for (size_t i = 0; i < windows.size(); i++)
    {
        std::string title = (i % 2 == 0) ? "Cabbage Engine [EDSL]" : "Cabbage Engine [GLSL]";
        windows[i] = glfwCreateWindow(1920, 1080, title.c_str(), nullptr, nullptr);
        if (windows[i] == nullptr)
        {
            std::cerr << "Failed to create Horizon default example window.\n";
            for (GLFWwindow* window : windows)
            {
                if (window != nullptr)
                {
                    glfwDestroyWindow(window);
                }
            }
            glfwTerminate();
            return;
        }
    }

    {
        std::vector<HardwareImage> finalOutputImages;
        std::vector<HardwareExecutor> executors(windows.size());
        std::vector<std::unique_ptr<DefaultFrameSubmitState>> submitStates(windows.size());
        DefaultOutputImageFormat outputImageFormat = defaultOutputImageFormats.front();
        std::string outputImageError;
        for (const DefaultOutputImageFormat& candidate : defaultOutputImageFormats)
        {
            std::vector<HardwareImage> candidateImages(windows.size());
            try
            {
                for (size_t i = 0; i < candidateImages.size(); i++)
                {
                    HardwareImageDesc createInfo =
                        HardwareImageDesc::texture_2d(1920,
                                                      1080,
                                                      candidate.format,
                                                      ImageUsageFlags::Storage | ImageUsageFlags::ColorAttachment |
                                                          ImageUsageFlags::Sampled | ImageUsageFlags::TransferSrc |
                                                          ImageUsageFlags::TransferDst,
                                                      std::string("example_default.output.") + candidate.name + "." + std::to_string(i));

                    candidateImages[i] = HardwareImage(createInfo);
                    candidateImages[i].set_clear_color(0.0f, 0.0f, 0.0f, 1.0f);
                }

                outputImageFormat = candidate;
                finalOutputImages = std::move(candidateImages);
                break;
            }
            catch (const std::exception& error)
            {
                outputImageError = std::string(candidate.name) + ": " + error.what();
            }
        }

        if (finalOutputImages.empty())
        {
            std::cerr << "Failed to create Horizon default output images. " << outputImageError << "\n";
            for (GLFWwindow* window : windows)
            {
                if (window != nullptr)
                {
                    glfwDestroyWindow(window);
                }
            }
            glfwTerminate();
            return;
        }

        const std::string storageImageComputeShader =
            make_default_storage_image_compute_shader(outputImageFormat.glsl_layout);

        for (size_t i = 0; i < windows.size(); i++)
        {
            submitStates[i] = std::make_unique<DefaultFrameSubmitState>();
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
        HardwareImage& texture = textureResult.texture;

        constexpr size_t OBJECT_COUNT = 20;
        std::vector<ktm::fmat4x4> baseModelMat(OBJECT_COUNT);
        for (size_t i = 0; i < OBJECT_COUNT; i++)
        {
            baseModelMat[i] = ktm::translate3d(ktm::fvec3(static_cast<float>(i % 5) - 2.0f,
                                                          static_cast<float>(i / 5) - 0.5f, 0.0f)) *
                              ktm::scale3d(ktm::fvec3(0.1f, 0.1f, 0.1f)) *
                              ktm::rotate3d_axis(ktm::radians(static_cast<float>(i) * 30.0f),
                                                 ktm::fvec3(0.0f, 0.0f, 1.0f));
        }

        std::vector<std::vector<HardwareBuffer>> rasterizerStorageBuffers(windows.size());
        for (std::vector<HardwareBuffer>& storageBuffers : rasterizerStorageBuffers)
        {
            storageBuffers.reserve(OBJECT_COUNT);
            for (size_t i = 0; i < OBJECT_COUNT; i++)
            {
                RasterizerStorageBufferObject initialData;
                initialData.textureIndex = textureID;
                initialData.model = baseModelMat[i];
                const auto initialBytes = std::as_bytes(std::span<const RasterizerStorageBufferObject>(&initialData, 1));
                storageBuffers.push_back(
                    HardwareBuffer::from_bytes(initialBytes,
                                               sizeof(RasterizerStorageBufferObject),
                                               BufferUsageFlags::TransferSrc | BufferUsageFlags::TransferDst |
                                                   BufferUsageFlags::Storage));
            }
        }
        //std::vector<HardwareBuffer> computeStorageBuffers(windows.size());

        std::atomic_bool running = true;
        std::mutex errorMutex;
        std::exception_ptr firstWorkerError;
        std::string firstWorkerErrorMessage;

        auto captureWorkerError = [&](const char* label, uint32_t threadIndex, std::exception_ptr errorPtr) {
            std::string message;
            try
            {
                if (errorPtr)
                {
                    std::rethrow_exception(errorPtr);
                }
            }
            catch (const std::exception& error)
            {
                message = std::string(label) + "[" + std::to_string(threadIndex) + "]: " + error.what();
            }
            catch (...)
            {
                message = std::string(label) + "[" + std::to_string(threadIndex) + "]: unknown exception";
            }

            {
                std::lock_guard<std::mutex> lock(errorMutex);
                if (!firstWorkerError)
                {
                    firstWorkerError = std::move(errorPtr);
                    firstWorkerErrorMessage = std::move(message);
                }
            }
            running.store(false);
        };

        auto runWorker = [&](const char* label, uint32_t threadIndex, auto& body) {
            try
            {
                body(threadIndex);
            }
            catch (...)
            {
                captureWorkerError(label, threadIndex, std::current_exception());
            }
        };

        auto meshThread = [&](uint32_t threadIndex) {
            //CFW_LOG_INFO("Mesh thread {} started...", threadIndex);

            //ComputeStorageBufferObject computeUniformData(windows.size());
            //computeStorageBuffers[threadIndex] = HardwareBuffer(sizeof(ComputeStorageBufferObject), BufferUsage::StorageBuffer);

            std::vector<RasterizerStorageBufferObject> rasterizerStorageBufferObjects(OBJECT_COUNT);
            std::vector<HardwareBuffer>& storageBuffers = rasterizerStorageBuffers[threadIndex];

            auto startTime = std::chrono::high_resolution_clock::now();
            uint64_t frameCount = 0;

            while (running.load())
            {
                // 等待上一帧显示完成（或初始状态）
                /*meshSemaphores[threadIndex]->acquire();
                if (!running.load()) break;*/

                float currentTime = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - startTime).count();

                for (size_t i = 0; i < storageBuffers.size(); i++)
                {
                    // rasterizerUniformBufferObject[i].textureIndex = texture[0][0].storeDescriptor();
                    rasterizerStorageBufferObjects[i].textureIndex = textureID;
                    rasterizerStorageBufferObjects[i].model = baseModelMat[i] * ktm::rotate3d_axis(currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                    (void)storageBuffers[i].write_bytes(
                        std::as_bytes(std::span<const RasterizerStorageBufferObject>(&rasterizerStorageBufferObjects[i], 1)));
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
                                             const ktm::fmat4x4& mvp) -> std::vector<SimpleVertex> {
            auto dst = src;
            for (auto& v : dst)
            {
                ktm::fvec4 clip = mvp * ktm::fvec4(v.position[0], v.position[1], v.position[2], 1.0f);
                float invW = 1.0f / clip[3];
                v.position = { clip[0] * invW, clip[1] * invW, clip[2] * invW };
            }
            return dst;
        };

        // Shared camera constants (same as RasterizerStorageBufferObject defaults)
        auto viewMat = ktm::look_at_lh(ktm::fvec3(2.0f, 2.0f, 2.0f),
                                       ktm::fvec3(0.0f, 0.0f, 0.0f),
                                       ktm::fvec3(0.0f, 0.0f, 1.0f));
        auto projMat = ktm::perspective_lh(ktm::radians(45.0f), 1920.0f / 1080.0f, 0.1f, 10.0f);
        auto vpMat = projMat * viewMat;

        // =====================================================================
        // renderThreadEDSL: EDSL 路径 — C++ lambda 定义 shader，自动绑定资源
        // =====================================================================
        auto renderThreadEDSL = [&](uint32_t threadIndex) {
            using namespace EmbeddedShader;
            using namespace EmbeddedShader::Ast;
            using namespace ktm;

            auto spirvOnlyOptions = [] {
                EmbeddedShader::CompilerOption options;
                options.enableBindless = false;
                options.compileGLSL = false;
                options.compileHLSL = false;
                options.compileDXIL = false;
                options.compileDXBC = false;
                return options;
            };

            // Texture2D proxy 声明时直接绑定已有 HardwareImage
            Texture2D<fvec4> inputImageRGBA16 = finalOutputImages[threadIndex];

            // EDSL compute shader: ACES filmic tone mapping
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

            // EDSL vertex shader: pass-through (MVP 已在 CPU 端完成)
            auto vsLambda = [&](Aggregate<VertexAttributeProxy> vertex) -> Float4 {
                position() = Float4(vertex->position, 1.0f);
                return Float4(vertex->color, 1.0f);
            };

            // EDSL fragment shader: 直接输出插值后的顶点颜色
            auto fsLambda = [&](Float4 interpolatedColor) -> Float4 {
                return interpolatedColor;
            };

            // 从 lambda 创建管线，bindOutputTargets 自动绑定 render target
            EdslPipelineOptions rasterizerOptions;
            rasterizerOptions.compiler = spirvOnlyOptions();
            RasterizerPipelineDesc rasterizerDesc =
                RasterizerPipelineDesc::from_edsl(vsLambda, fsLambda, rasterizerOptions);
            DepthStencilStateDesc noDepth;
            noDepth.depth_test_enabled = false;
            noDepth.depth_write_enabled = false;
            rasterizerDesc.set_depth_stencil(noDepth);
            RasterizerPipeline rasterizer(std::move(rasterizerDesc));
            rasterizer.bind_output_targets(inputImageRGBA16);

            // 从 lambda 创建 compute 管线；当前 ComputePipeline 使用显式 storage image binding。
            EdslPipelineOptions computeOptions;
            computeOptions.compiler = spirvOnlyOptions();
            computeOptions.auto_bind = false;
            ComputePipeline computer(ComputePipelineDesc::from_edsl(compute, uvec3(8, 8, 1), computeOptions));
            computer.bind_storage_image(0, finalOutputImages[threadIndex]);

            auto startTime = std::chrono::high_resolution_clock::now();

            while (running.load())
            {
                float currentTime = std::chrono::duration<float, std::chrono::seconds::period>(
                                        std::chrono::high_resolution_clock::now() - startTime)
                                        .count();
                HardwareBuffer indexBuffer = HardwareBuffer::index(indices);

                rasterizer.clear_records();
                for (size_t i = 0; i < baseModelMat.size(); i++)
                {
                    auto model = baseModelMat[i] * ktm::rotate3d_axis(
                                                       currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                    auto transformed = transformVerticesForObject(simpleVertices, vpMat * model);
                    HardwareBuffer vertexBuffer = HardwareBuffer::vertex(transformed);
                    rasterizer.record(indexBuffer, vertexBuffer);
                }

                rasterizer(1920, 1080);
                computer(1920 / 8, 1080 / 8, 1);
                DefaultFrameSubmitState& submitState = *submitStates[threadIndex];
                std::lock_guard submitLock(submitState.mutex);
                submitState.last_render_receipt = executors[threadIndex].stream()
                    << rasterizer.command_batch()
                    << computer.command_batch()
                    << commit();
            }
        };

        // =====================================================================
        // renderThreadGLSL: 手写 GLSL 路径 — 预编译 SPIR-V + CPU MVP 预变换
        // =====================================================================
        auto renderThreadGLSL = [&](uint32_t threadIndex) {
            // 简化后的 shader 无需 push constant / UBO，仅做 pass-through
            RasterizerPipelineDesc rasterizerDesc =
                RasterizerPipelineDesc::from_spirv(default_vert_glsl::spirv, default_frag_glsl::spirv);
            DepthStencilStateDesc noDepth;
            noDepth.depth_test_enabled = false;
            noDepth.depth_write_enabled = false;
            rasterizerDesc.set_depth_stencil(noDepth);
            RasterizerPipeline rasterizer(std::move(rasterizerDesc));

            // 绑定 render target
            rasterizer[default_frag_glsl::outColor] = finalOutputImages[threadIndex];

            // compute 管线保持不变
            EmbeddedShader::CompilerOption glslCompilerOptions;
            glslCompilerOptions.enableBindless = false;
            glslCompilerOptions.compileGLSL = false;
            glslCompilerOptions.compileHLSL = false;
            glslCompilerOptions.compileDXIL = false;
            glslCompilerOptions.compileDXBC = false;
            ComputePipeline computer(ComputePipelineDesc::from_source(storageImageComputeShader,
                                                                      EmbeddedShader::ShaderLanguage::GLSL,
                                                                      glslCompilerOptions));
            computer.bind_storage_image(0, finalOutputImages[threadIndex]);

            auto startTime = std::chrono::high_resolution_clock::now();

            while (running.load())
            {
                float currentTime = std::chrono::duration<float, std::chrono::seconds::period>(
                                        std::chrono::high_resolution_clock::now() - startTime)
                                        .count();
                HardwareBuffer indexBuffer = HardwareBuffer::index(indices);

                rasterizer.clear_records();
                for (size_t i = 0; i < baseModelMat.size(); i++)
                {
                    auto model = baseModelMat[i] * ktm::rotate3d_axis(
                                                       currentTime * ktm::radians(90.0f), ktm::fvec3(0.0f, 0.0f, 1.0f));
                    auto transformed = transformVerticesForObject(simpleVertices, vpMat * model);
                    HardwareBuffer vertexBuffer = HardwareBuffer::vertex(transformed);
                    rasterizer.record(indexBuffer, vertexBuffer);
                }

                rasterizer(1920, 1080);
                computer(1920 / 8, 1080 / 8, 1);
                DefaultFrameSubmitState& submitState = *submitStates[threadIndex];
                std::lock_guard submitLock(submitState.mutex);
                submitState.last_render_receipt = executors[threadIndex].stream()
                    << rasterizer.command_batch()
                    << computer.command_batch()
                    << commit();
            }
        };

        auto displayThread = [&](uint32_t threadIndex) {
            //CFW_LOG_INFO("Display thread {} started...", threadIndex);

            HardwareDisplayer displayManager = HardwareDisplayer(glfwGetWin32Window(windows[threadIndex]));
            HardwareExecutor displayExecutor;

            auto startTime = std::chrono::high_resolution_clock::now();
            uint64_t frameCount = 0;

            while (running.load())
            {
                // 等待渲染提交完成
                // displaySemaphores[threadIndex]->acquire();
                // if (!running.load()) break;

                float time = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - startTime).count();
                // CFW_LOG_INFO("Display thread {} frame {} at {:.3f}s", threadIndex, frameCount, time);

                DefaultFrameSubmitState& submitState = *submitStates[threadIndex];
                {
                    std::lock_guard submitLock(submitState.mutex);
                    if (submitState.last_render_receipt.serial != 0 &&
                        submitState.last_render_receipt.serial != submitState.last_presented_render_serial)
                    {
                        displayExecutor.wait(submitState.last_render_receipt);
                        submitState.last_presented_render_serial = submitState.last_render_receipt.serial;
                    }

                    auto receipt = displayExecutor.stream()
                        << present(displayManager, finalOutputImages[threadIndex])
                        << commit();
                    (void)receipt;
                }
                ++frameCount;

                // 通知 Mesh 线程开始下一帧
                // meshSemaphores[threadIndex]->release();
            }
            // meshSemaphores[threadIndex]->release();

            //CFW_LOG_INFO("Display thread {} ended.", threadIndex);
        };

        std::vector<std::thread> meshThreads;
        std::vector<std::thread> renderThreads;
        std::vector<std::thread> displayThreads;

        for (size_t i = 0; i < windows.size(); i++)
        {
            const uint32_t threadIndex = static_cast<uint32_t>(i);
            meshThreads.emplace_back([&, threadIndex] {
                runWorker("mesh", threadIndex, meshThread);
            });
            if (i % 2 == 0)
                renderThreads.emplace_back([&, threadIndex] {
                    runWorker("render-edsl", threadIndex, renderThreadEDSL);
                });
            else
                renderThreads.emplace_back([&, threadIndex] {
                    runWorker("render-glsl", threadIndex, renderThreadGLSL);
                });
            displayThreads.emplace_back([&, threadIndex] {
                runWorker("display", threadIndex, displayThread);
            });
        }

        while (running.load())
        {
            glfwPollEvents();
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

        if (firstWorkerError)
        {
            std::cerr << "Horizon default example stopped after worker error: " << firstWorkerErrorMessage << "\n";
        }
    }

    for (size_t i = 0; i < windows.size(); i++)
    {
        glfwDestroyWindow(windows[i]);
    }

    glfwTerminate();
}
