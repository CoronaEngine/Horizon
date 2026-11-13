#pragma once
#include "Horizon_old.h"
#include <memory>
struct GLFWwindow;

struct LegacyFrameOutput {
    HardwareImage image;
    HardwareExecutor* executor;
};

// Load the scene attachment and alpha-blend the UI in a second pass.
class HorizonImGuiLayer {
public:
    HorizonImGuiLayer(GLFWwindow*, uint32_t width, uint32_t height);
    ~HorizonImGuiLayer();
    HorizonImGuiLayer(const HorizonImGuiLayer&) = delete;
    HorizonImGuiLayer& operator=(const HorizonImGuiLayer&) = delete;
    void new_frame();
    LegacyFrameOutput draw_overlay(HardwareImage& scene, HardwareExecutor& scene_ready);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
