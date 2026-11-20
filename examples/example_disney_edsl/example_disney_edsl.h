#pragma once
#include "legacy_common.h"
#include <memory>

// Adapter lets both shader paths execute the exact same legacy scene loop.
class DisneyEdslPipeline {
public:
    struct Shared {
        glm::mat4 proj_view{};
        glm::mat4 envMtx{};
        glm::vec4 camPos{};
        glm::vec4 flags{};
        glm::vec4 lightDir{};
        glm::vec4 lightCol{};
        glm::vec4 exposurePad{};
    } vsp;
    HardwareImage outColor;
    DisneyEdslPipeline();
    ~DisneyEdslPipeline();
    void bind_textures(HardwareImage&, HardwareImage&);
    void setDepthImage(HardwareImage&);
    void setRasterState(RasterState);
    RasterBatch beginBatch(uint16_t width, uint16_t height);
private:
    struct State;
    std::unique_ptr<State> state_;
};
