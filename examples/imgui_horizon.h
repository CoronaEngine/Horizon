#pragma once

#include <cstdint>
#include <memory>

#include "horizon.h"

struct GLFWwindow;

// 基于 Horizon 公共绘制 API 的 ImGui 渲染层（不含任何引擎内嵌 UI 代码）。
// UI 画进 example 自己的输出图像，再随示例原有的 present 流程上屏。
// 约束：ImGui 单 context，
//
// 每帧用法（示例原有的 wait + present + commit 保持不动，在其上方插一行）：
//   ui.new_frame();
//   ImGui::ShowDemoWindow();      // 任意 UI
//   ...
//   ui.draw_overlay(display_executor, final_output_image, render_receipt);   // ← 插入的一行
//   display_executor.wait(render_receipt);
//   (void)(display_executor.stream() << horizon::present(display, final_output_image)
//                                    << horizon::commit());
class HorizonImGuiLayer
{
public:
    HorizonImGuiLayer(GLFWwindow* window, uint32_t target_width, uint32_t target_height);
    ~HorizonImGuiLayer();
    HorizonImGuiLayer(const HorizonImGuiLayer&) = delete;
    HorizonImGuiLayer& operator=(const HorizonImGuiLayer&) = delete;

    void new_frame();


    void draw_overlay(horizon::HardwareExecutor& display_executor,
                      horizon::HardwareImage& target,
                      const horizon::SubmitReceipt& scene_receipt);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
