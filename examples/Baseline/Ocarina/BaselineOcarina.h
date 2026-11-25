#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "rhi/resources/shader.h"
#include "rhi/resources/texture.h"

#include <chrono>
#include <cstdint>
#include <vector>

class BaselineOcarina final : public ExampleBase {
public:
    explicit BaselineOcarina(void* native_window);
    ~BaselineOcarina() override = default;
    void run() override;

private:
    static constexpr int width = 800, height = 600;
    HardwareDisplayer display;
    ocarina::Device device;
    ocarina::Stream stream;
    ocarina::Buffer<ocarina::float3> vertices;
    ocarina::Buffer<ocarina::uint3> indices;
    ocarina::Buffer<ocarina::float2> texcoords;
    ocarina::Texture2D texture;
    ocarina::Accel scene;
    ocarina::Shader<void(ocarina::float3, ocarina::Buffer<uint32_t>)> shader;
    ocarina::Buffer<uint32_t> output_buffer;
    std::vector<uint32_t> pixels;
    std::chrono::steady_clock::time_point start;
    std::uint64_t frame = 0;
};
