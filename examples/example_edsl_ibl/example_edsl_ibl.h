#pragma once
#include "legacy_common.h"
#include <memory>

void run_example_edsl_ibl();

// Adapter lets both shader paths execute the exact same legacy scene loop.
class IblEdslPipeline {
public:
    struct Shared {
        glm::mat4 proj_view{};
        glm::mat4 skyEnvMtx{};
        glm::mat4 envMtx{};
        glm::vec4 camPos{};
        glm::vec4 flags{};
        glm::vec4 rgbDiff{};
        glm::vec4 rgbSpec{};
        glm::vec4 lightDir{};
        glm::vec4 lightCol{};
    } vsp;
    HardwareImage outColor;
    IblEdslPipeline();
    ~IblEdslPipeline();
    void bind_textures(HardwareImage&, HardwareImage&);
    void setDepthImage(HardwareImage&);
    RasterBatch beginBatch(uint16_t width, uint16_t height);
private:
    struct State;
    std::unique_ptr<State> state_;
};
