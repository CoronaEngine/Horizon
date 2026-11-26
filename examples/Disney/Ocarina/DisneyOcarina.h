#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "rhi/resources/shader.h"
#include "rhi/resources/texture.h"

#include <cstdint>
#include <vector>

class DisneyOcarina final : public ExampleBase {
public:
    explicit DisneyOcarina(void* native_window);
    ~DisneyOcarina() override = default;
    void run(float) override;

private:
    static constexpr int width = 800, height = 600;
    HardwareDisplayer display;
    HardwareImage frame{width, height, HardwareImage::Format::RGBA8_UNORM};
    HardwareExecutor executor;
    ocarina::Device device;
    ocarina::Stream stream;
    ocarina::Buffer<ocarina::float3> vertices;
    ocarina::Buffer<ocarina::uint3> indices;
    ocarina::Texture2D radiance_atlas, irradiance_atlas;
    ocarina::Buffer<ocarina::float4> materials;
    ocarina::Accel chart;
    ocarina::float3 camera_position, light_direction;
    ocarina::Shader<void(ocarina::float3, ocarina::float3, ocarina::Buffer<uint32_t>)> shader;
    ocarina::Buffer<uint32_t> output_buffer;
    std::vector<uint32_t> pixels;
};
