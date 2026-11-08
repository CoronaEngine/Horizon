#pragma once
#include "Horizon_old.h"
#include <memory>
struct GLFWwindow;

struct LegacyFrameOutput {
    HardwareImage image;
    HardwareExecutor* executor;
};

// Legacy always clears attachments: draw UI into transparent RGBA16F, then
// composite into a separate image. This adapter changes no RHI interfaces.
class HorizonImGuiLayerOld {
public:
    HorizonImGuiLayerOld(GLFWwindow*, uint32_t width, uint32_t height);
    ~HorizonImGuiLayerOld();
    HorizonImGuiLayerOld(const HorizonImGuiLayerOld&) = delete;
    HorizonImGuiLayerOld& operator=(const HorizonImGuiLayerOld&) = delete;
    void new_frame();
    LegacyFrameOutput draw_overlay(HardwareImage& scene, HardwareExecutor& scene_ready);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
