#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

enum class BackendMode : uint8_t
{
    AllEDSL,
    AllGLSL,
    Alternating
};

// 运行时配置：用于选择场景，并控制队列与线程节奏。
struct RuntimeConfig
{
    // 场景注册名。
    std::string scenario = "default";
    //std::string scenario = "triangle";
    //std::string scenario = "texture";

    std::size_t queue_depth = 10;                         // 有界队列的容量。
    uint32_t window_width = 800;                          // EDSL/GLSL 输出窗口宽度。
    uint32_t window_height = 600;                         // EDSL/GLSL 输出窗口高度。
    uint32_t window_count = 4;                            // 输出窗口数量。
    BackendMode backend_mode = BackendMode::Alternating;  // 窗口后端分配模式。
    uint32_t max_fps = 0;                                 // Mesh 线程帧率上限，0 表示不限制。
    bool enable_compare_stats = true;                     // 是否周期打印对比统计信息。
    //uint32_t glslArtificialDelayMs = 0;                 // GLSL 线程人工延迟（用于背压测试）。
    bool show_help = false;                               // 是否打印帮助信息（--help / -h）。
};

// 解析命令行参数并返回 RuntimeConfig，同时做基础合法性归一化。
RuntimeConfig parse_runtime_config(int argc, char **argv);

// 生成运行参数帮助文本。
std::string runtime_config_usage(std::string_view exe_name);
