#pragma once
#include "legacy_common.h"
#include <memory>

void run_example_edsl_ibl_old();

// Adapter lets both shader paths execute the exact same legacy scene loop.
class IblEdslOldPipeline {
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
    struct Draw {
        glm::mat4 model{};
        glm::vec4 params0{};
        glm::vec4 misc{};
        uint32_t texCubeIndex{}, texCubeIrrIndex{}; // handwritten ABI, resources bound below
    } vpc;
    HardwareImage outColor;
    IblEdslOldPipeline();
    ~IblEdslOldPipeline();
    void bind_textures(HardwareImage&, HardwareImage&);
    void prepare_draws();
    void setDepthImage(HardwareImage&);
    void record(const HardwareBuffer&, const HardwareBuffer&, const DrawIndexedParams&);
    RasterizerPipelineBase& operator()(uint16_t width, uint16_t height);
private:
    struct State;
    std::unique_ptr<State> state_;
};
