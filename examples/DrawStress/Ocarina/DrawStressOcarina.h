#pragma once

#include "../../ExampleBase.h"
#include "Horizon.h"
#include "rhi/resources/shader.h"

#include <cstdint>
#include <vector>

class DrawStressOcarina final : public ExampleBase {
public:
    explicit DrawStressOcarina(void* native_window);
    ~DrawStressOcarina() override = default;
    void run(float seconds) override;

private:
    static constexpr int width = 800, height = 600;
    static constexpr int dim = 40;
    HardwareDisplayer display;
    ocarina::Device device;
    ocarina::Stream stream;
    ocarina::Buffer<ocarina::float3> vertices;
    ocarina::Buffer<ocarina::uint3> indices;
    ocarina::Accel grid;
    ocarina::Shader<void(ocarina::float3, ocarina::Buffer<uint32_t>)> shader;
    ocarina::Buffer<uint32_t> output_buffer;
    std::vector<uint32_t> pixels;
};
