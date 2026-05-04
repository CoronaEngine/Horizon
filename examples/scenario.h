#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "Horizon.h"
#include "runtime_config.h"

// 全局统一时钟类型，线程间通过它计算延迟/FPS。
using Clock = std::chrono::steady_clock;

enum class Backend : uint8_t
{
    EDSL,
    GLSL
};

struct MeshFrame
{
    uint64_t frame_id{0};                // 单调递增帧号，由 mesh 线程生成。
    Clock::time_point timestamp{};       // mesh 产出该帧的时间戳。
    std::shared_ptr<const void> payload; // 只读共享载荷，供两个 render 线程消费。
};

struct RenderFrame
{
    uint64_t frame_id{0};                    // 对应的 mesh 帧号。
    Backend backend{Backend::EDSL};          // EDSL 或 GLSL。
    HardwareImage *output_image{nullptr};    // 最终显示的图像资源。
    HardwareExecutor *executor_ref{nullptr}; // 对应提交器，display 线程会先 wait 再 present。
    Clock::time_point submit_timestamp{};    // render 提交完成时间，用于统计延迟。
};

// 场景钩子：将“线程编排”与“shader/data 逻辑”解耦。
class ScenarioHooks
{
  public:
    virtual ~ScenarioHooks() = default;

    // 返回场景名（用于日志与调试）。
    virtual std::string name() const = 0;

    // 场景初始化：创建/绑定场景内部资源。
    virtual bool init(const RuntimeConfig &config,
                      Backend backend,
                      const HardwareImage &output,
                      std::string &error_message) = 0;

    // Mesh 钩子：生产一帧共享载荷，后续由渲染线程消费。
    virtual std::shared_ptr<const void> mesh_tick(uint64_t frame_id,
                                                  Clock::time_point now,
                                                  std::string &error_message) = 0;

    // 渲染钩子：按后端消费 MeshFrame，并向 executor 录制/提交命令。
    virtual bool render_tick(const MeshFrame &mesh_frame,
                             Backend backend,
                             HardwareExecutor &executor,
                             const HardwareImage &output_image,
                             std::string &error_message) = 0;

    // Display 钩子：在 display 线程每次呈现后触发，可用于场景级统计或调试。
    virtual void display_tick(const RenderFrame &render_frame) = 0;

    // 场景资源释放。
    virtual void shutdown() = 0;
};
