#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "Horizon.h"
#include "drop_oldest_queue.h"
#include "runtime_config.h"
#include "scenario.h"
#include "scenario_registry.h"

#include "1_default_test/default_scenario.h"
#include "2_triangle_test/triangle_scenario.h"
#include "3_texture_test/texture_scenario.h"

struct WindowStats
{
    std::atomic<uint64_t> mesh_frames_produced{0};
    std::atomic<uint64_t> render_frames{0};
    std::atomic<uint64_t> display_loop_ticks{0};
    std::atomic<uint64_t> displayed_frames{0};
    std::atomic<uint64_t> latency_us_total{0};
    std::atomic<uint64_t> latest_frame_id{0};
};

struct WindowContext
{
    WindowContext(std::size_t window_index, Backend selected_backend, std::size_t queue_depth)
        : index(window_index), backend(selected_backend), mesh_to_render(queue_depth), render_to_display(queue_depth)
    {
    }

    std::size_t index{0};
    Backend backend{Backend::EDSL};
    GLFWwindow *window{nullptr};
    HardwareImage output_image;
    HardwareExecutor executor;
    std::unique_ptr<ScenarioHooks> scenario;

    DropOldestQueue<MeshFrame> mesh_to_render;
    DropOldestQueue<RenderFrame> render_to_display;

    WindowStats stats;
    std::thread mesh_worker;
    std::thread render_worker;
    std::thread display_worker;
};

Backend resolve_backend(std::size_t window_index, BackendMode mode)
{
    switch (mode)
    {
    case BackendMode::AllEDSL:
        return Backend::EDSL;
    case BackendMode::AllGLSL:
        return Backend::GLSL;
    case BackendMode::Alternating:
    default:
        return (window_index % 2 == 0) ? Backend::EDSL : Backend::GLSL;
    }
}

const char *backend_name(Backend backend)
{
    return (backend == Backend::EDSL) ? "EDSL" : "GLSL";
}

// 基础自检：验证 drop-oldest 队列的核心行为是否符合预期。
bool run_queue_self_test()
{
    DropOldestQueue<int> queue(3);
    for (int i = 0; i < 10; ++i)
    {
        if (!queue.push(i))
        {
            return false;
        }
    }

    if (queue.dropped_count() != 7)
    {
        return false;
    }

    auto latest = queue.try_pop_all_latest();

    if (!latest.has_value() || latest.value() != 9)
    {
        return false;
    }

    queue.close();
    auto maybe_value = queue.pop_wait();
    return !maybe_value.has_value();
}

// 打印当前已注册场景列表，便于命令行选择。
void print_available_scenarios()
{
    auto names = list_scenarios();
    std::ostringstream oss;
    oss << "Available scenarios: ";
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
        {
            oss << ", ";
        }
        oss << names[i];
    }
    std::cout << oss.str() << '\n';
}

int main(int argc, char **argv)
{
    RuntimeConfig config;

    try
    {
        config = parse_runtime_config(argc, argv);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to parse runtime config: " << e.what() << '\n';
        std::cerr << runtime_config_usage(argc > 0 ? argv[0] : "HorizonExamples");
        return -1;
    }

    if (config.show_help)
    {
        std::cout << runtime_config_usage(argc > 0 ? argv[0] : "HorizonExamples");
        return 0;
    }

    if (!run_queue_self_test())
    {
        std::cerr << "DropOldestQueue self-test failed, aborting.\n";
        return -1;
    }

    register_default_scenario();
    register_triangle_scenario();
    register_texture_scenario();

    if (glfwInit() < 0)
    {
        std::cerr << "glfwInit failed.\n";
        return -1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    std::vector<std::unique_ptr<WindowContext>> windows;
    windows.reserve(config.window_count);

    auto destroy_windows_and_terminate = [&] {
        for (auto &ctx : windows)
        {
            if (ctx->window != nullptr)
            {
                glfwDestroyWindow(ctx->window);
                ctx->window = nullptr;
            }
        }
        glfwTerminate();
    };

    auto shutdown_scenarios = [&] {
        for (auto &ctx : windows)
        {
            if (ctx->scenario)
            {
                ctx->scenario->shutdown();
            }
        }
    };

    for (uint32_t i = 0; i < config.window_count; ++i)
    {
        Backend backend = resolve_backend(i, config.backend_mode);
        auto ctx = std::make_unique<WindowContext>(i, backend, config.queue_depth);

        std::string title = "Cabbage Engine [" + std::string(backend_name(backend)) + "] #" + std::to_string(i);
        ctx->window = glfwCreateWindow(static_cast<int>(config.window_width),
                                       static_cast<int>(config.window_height),
                                       title.c_str(),
                                       nullptr,
                                       nullptr);
        if (ctx->window == nullptr)
        {
            std::cerr << "Failed to create GLFW window #" << i << ".\n";
            destroy_windows_and_terminate();
            return -1;
        }

        HardwareImageCreateInfo create_info;
        create_info.width = config.window_width;
        create_info.height = config.window_height;
        create_info.format = ImageFormat::RGBA16_FLOAT;
        create_info.usage = ImageUsage::StorageImage;
        create_info.arrayLayers = 1;
        create_info.mipLevels = 1;
        ctx->output_image = HardwareImage(create_info);

        windows.push_back(std::move(ctx));
        WindowContext *window_ctx = windows.back().get();

        window_ctx->scenario = create_scenario(config.scenario, config);
        if (!window_ctx->scenario)
        {
            std::cerr << "Unknown scenario: " << config.scenario << '\n';
            print_available_scenarios();
            destroy_windows_and_terminate();
            return -1;
        }

        std::string scenario_error;
        if (!window_ctx->scenario->init(config, backend, window_ctx->output_image, scenario_error))
        {
            std::cerr << "Scenario init failed for window #" << i << ": " << scenario_error << '\n';
            shutdown_scenarios();
            destroy_windows_and_terminate();
            return -1;
        }
    }

    std::atomic_bool running{true};
    std::atomic_bool has_error{false};
    std::mutex error_mutex;
    std::string error_message;

    auto request_stop = [&] {
        running.store(false);
        for (auto &ctx : windows)
        {
            ctx->mesh_to_render.close();
            ctx->render_to_display.close();
        }
    };

    auto set_error_and_stop = [&](const std::string &thread_name, std::size_t window_index, const std::string &message) {
        bool expected = false;
        if (has_error.compare_exchange_strong(expected, true))
        {
            std::lock_guard<std::mutex> lock(error_mutex);
            error_message = thread_name + "(window " + std::to_string(window_index) + "): " + message;
        }
        request_stop();
    };

    for (auto &ctx_ptr : windows)
    {
        WindowContext *ctx = ctx_ptr.get();

        ctx->mesh_worker = std::thread([&, ctx] {
            try
            {
                uint64_t frame_id = 0;
                const auto frame_interval = (config.max_fps > 0) ? std::chrono::microseconds(1'000'000 / config.max_fps)
                                                                 : std::chrono::microseconds(0);

                while (running.load())
                {
                    auto frame_begin = Clock::now();
                    std::string mesh_error;

                    MeshFrame frame;
                    frame.frame_id = ++frame_id;
                    frame.timestamp = frame_begin;
                    frame.payload = ctx->scenario->mesh_tick(frame.frame_id, frame.timestamp, mesh_error);

                    if (!frame.payload)
                    {
                        set_error_and_stop("MeshThread", ctx->index, mesh_error.empty() ? "mesh_tick returned empty payload" : mesh_error);
                        break;
                    }

                    if (!ctx->mesh_to_render.push(std::move(frame)))
                    {
                        break;
                    }

                    ctx->stats.mesh_frames_produced.fetch_add(1, std::memory_order_relaxed);

                    if (frame_interval.count() > 0)
                    {
                        auto elapsed = Clock::now() - frame_begin;
                        if (elapsed < frame_interval)
                        {
                            std::this_thread::sleep_for(frame_interval - elapsed);
                        }
                    }
                }
            }
            catch (const std::exception &e)
            {
                set_error_and_stop("MeshThread", ctx->index, e.what());
            }
            ctx->mesh_to_render.close();
        });

        ctx->render_worker = std::thread([&, ctx] {
            try
            {
                while (running.load() || !ctx->mesh_to_render.is_closed_and_empty())
                {
                    auto mesh_frame = ctx->mesh_to_render.pop_wait();
                    if (!mesh_frame.has_value())
                    {
                        break;
                    }

                    std::string render_error;
                    if (!ctx->scenario->render_tick(mesh_frame.value(),
                                                    ctx->backend,
                                                    ctx->executor,
                                                    ctx->output_image,
                                                    render_error))
                    {
                        set_error_and_stop("RenderThread", ctx->index, render_error.empty() ? "render_tick failed" : render_error);
                        break;
                    }

                    RenderFrame render_frame;
                    render_frame.frame_id = mesh_frame->frame_id;
                    render_frame.backend = ctx->backend;
                    render_frame.output_image = &ctx->output_image;
                    render_frame.executor_ref = &ctx->executor;
                    render_frame.submit_timestamp = Clock::now();
                    if (!ctx->render_to_display.push(std::move(render_frame)))
                    {
                        break;
                    }

                    ctx->stats.render_frames.fetch_add(1, std::memory_order_relaxed);
                }
            }
            catch (const std::exception &e)
            {
                set_error_and_stop("RenderThread", ctx->index, e.what());
            }
            ctx->render_to_display.close();
        });

        ctx->display_worker = std::thread([&, ctx] {
            try
            {
                HardwareDisplayer displayer(glfwGetWin32Window(ctx->window));
                std::optional<RenderFrame> latest_frame;

                while (running.load() || !ctx->render_to_display.is_closed_and_empty())
                {
                    bool got_new_frame = false;

                    auto latest = ctx->render_to_display.try_pop_all_latest();
                    if (latest.has_value())
                    {
                        latest_frame = std::move(*latest);
                        got_new_frame = true;
                    }

                    auto now = Clock::now();

                    if (latest_frame.has_value() &&
                        latest_frame->output_image != nullptr &&
                        latest_frame->executor_ref != nullptr)
                    {
                        displayer.wait(*latest_frame->executor_ref) << *latest_frame->output_image;
                        ctx->scenario->display_tick(latest_frame.value());

                        if (got_new_frame)
                        {
                            ctx->stats.latest_frame_id.store(latest_frame->frame_id, std::memory_order_relaxed);
                            auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(now - latest_frame->submit_timestamp).count();
                            ctx->stats.latency_us_total.fetch_add(static_cast<uint64_t>(latency_us), std::memory_order_relaxed);
                            ctx->stats.displayed_frames.fetch_add(1, std::memory_order_relaxed);
                        }
                    }

                    ctx->stats.display_loop_ticks.fetch_add(1, std::memory_order_relaxed);
                    if (!got_new_frame)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            }
            catch (const std::exception &e)
            {
                set_error_and_stop("DisplayThread", ctx->index, e.what());
            }
        });
    }

    auto last_stat_tick = Clock::now();
    std::vector<uint64_t> prev_mesh_frames(windows.size(), 0);
    std::vector<uint64_t> prev_render_frames(windows.size(), 0);
    std::vector<uint64_t> prev_display_ticks(windows.size(), 0);

    while (running.load())
    {
        glfwPollEvents();
        for (const auto &ctx : windows)
        {
            if (glfwWindowShouldClose(ctx->window))
            {
                request_stop();
                break;
            }
        }

        if (config.enable_compare_stats)
        {
            auto now = Clock::now();
            if (now - last_stat_tick >= std::chrono::seconds(1))
            {
                uint64_t total_mesh_fps = 0;
                uint64_t total_render_fps = 0;
                uint64_t total_display_loop_fps = 0;
                uint64_t total_displayed_frames = 0;
                uint64_t total_latency_us = 0;
                uint64_t total_mesh_to_render_drops = 0;
                uint64_t total_render_to_display_drops = 0;
                uint32_t edsl_windows = 0;
                uint32_t glsl_windows = 0;

                for (std::size_t i = 0; i < windows.size(); ++i)
                {
                    const auto &ctx = windows[i];
                    if (ctx->backend == Backend::EDSL)
                    {
                        ++edsl_windows;
                    }
                    else
                    {
                        ++glsl_windows;
                    }

                    uint64_t mesh_frames = ctx->stats.mesh_frames_produced.load(std::memory_order_relaxed);
                    uint64_t render_frames = ctx->stats.render_frames.load(std::memory_order_relaxed);
                    uint64_t display_ticks = ctx->stats.display_loop_ticks.load(std::memory_order_relaxed);

                    total_mesh_fps += (mesh_frames - prev_mesh_frames[i]);
                    total_render_fps += (render_frames - prev_render_frames[i]);
                    total_display_loop_fps += (display_ticks - prev_display_ticks[i]);

                    prev_mesh_frames[i] = mesh_frames;
                    prev_render_frames[i] = render_frames;
                    prev_display_ticks[i] = display_ticks;

                    total_displayed_frames += ctx->stats.displayed_frames.load(std::memory_order_relaxed);
                    total_latency_us += ctx->stats.latency_us_total.load(std::memory_order_relaxed);
                    total_mesh_to_render_drops += ctx->mesh_to_render.dropped_count();
                    total_render_to_display_drops += ctx->render_to_display.dropped_count();
                }

                double avg_latency_ms = (total_displayed_frames == 0) ? 0.0
                                                                      : static_cast<double>(total_latency_us) / 1000.0 / static_cast<double>(total_displayed_frames);

                // std::cout << "[Stats] windows=" << windows.size()
                //           << " backend(edsl=" << edsl_windows
                //           << ", glsl=" << glsl_windows << ")"
                //           << " meshFPS=" << total_mesh_fps
                //           << " renderFPS=" << total_render_fps
                //           << " displayLoopFPS=" << total_display_loop_fps
                //           << " drops(mesh->render=" << total_mesh_to_render_drops
                //           << ", render->display=" << total_render_to_display_drops << ")"
                //           << " avgLatencyMs=" << avg_latency_ms
                //           << '\n';

                last_stat_tick = now;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    request_stop();

    for (auto &ctx : windows)
    {
        if (ctx->mesh_worker.joinable())
        {
            ctx->mesh_worker.join();
        }
        if (ctx->render_worker.joinable())
        {
            ctx->render_worker.join();
        }
        if (ctx->display_worker.joinable())
        {
            ctx->display_worker.join();
        }
    }

    shutdown_scenarios();
    destroy_windows_and_terminate();

    if (has_error.load())
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        std::cerr << "Fatal error: " << error_message << '\n';
        return -1;
    }

    return 0;
}
