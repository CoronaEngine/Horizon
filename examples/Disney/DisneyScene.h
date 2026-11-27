#pragma once

#include "Horizon.h"

struct DisneyScene {
    static constexpr int rows = 10, columns = 11, slices = 48, stacks = 24;
    HardwareBuffer parameters;
    HardwareImage radiance, irradiance;
    HardwareImage color{800, 600, HardwareImage::Format::RGBA16_FLOAT};
    HardwareImage depth{800, 600, HardwareImage::Format::D32_FLOAT, HardwareImage::Usage::DepthImage};

    explicit DisneyScene(HardwareExecutor& executor);

    void bind(auto& pipeline) const {
        constexpr float yaw = ktm::pi<float>, pitch = 0.42f * ktm::pi<float>;
        const auto eye = 13.5f * ktm::fvec3(ktm::sin(pitch) * ktm::sin(yaw), ktm::cos(pitch), ktm::sin(pitch) * ktm::cos(yaw));
        const auto view = ktm::look_at_lh(eye, ktm::fvec3(0), ktm::fvec3(0, 1, 0));
        auto projection = ktm::perspective_lh(ktm::radians(45.0f), 800.0f / 600.0f, 0.1f, 100.0f);
        projection[1][1] *= -1.0f;
        pipeline.outColor = color;
        pipeline.vsp.proj_view = projection * view;
        pipeline.vsp.camPos = ktm::fvec4(eye, 1);
        pipeline.vsp.lightDir = ktm::fvec4(ktm::normalize(ktm::fvec3(-0.35f, 0.55f, -0.75f)), 0);
        pipeline.vsp.texCubeIndex = radiance.storeSampledDescriptor();
        pipeline.vsp.texCubeIrrIndex = irradiance.storeSampledDescriptor();
        pipeline.draws = parameters;
    }
};
