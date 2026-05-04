#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/base.h"
#include "rhi/common.h"
#include "rhi/context.h"

using namespace ocarina;

namespace {

using clock_type = std::chrono::high_resolution_clock;

constexpr uint warmup_iterations = 32;
constexpr uint launch_iterations = 2000;
constexpr uint large_launch_iterations = 128;
constexpr uint2 large_dim = make_uint2(1024u, 1024u);
constexpr uint3 tiled_block = make_uint3(8u, 8u, 1u);

template<typename Enqueue>
double measure_submit_us(Stream &stream, uint iterations, Enqueue &&enqueue) {
    for (uint i = 0; i < warmup_iterations; ++i) {
        enqueue();
        stream << synchronize() << commit();
    }

    double total_us = 0.0;
    for (uint i = 0; i < iterations; ++i) {
        const auto begin = clock_type::now();
        enqueue();
        const auto end = clock_type::now();
        total_us += std::chrono::duration<double, std::micro>(end - begin).count();
        stream << synchronize() << commit();
    }
    return total_us / static_cast<double>(iterations);
}

template<typename Enqueue>
double measure_total_us(Stream &stream, uint iterations, Enqueue &&enqueue) {
    for (uint i = 0; i < warmup_iterations; ++i) {
        enqueue();
        stream << synchronize() << commit();
    }

    double total_us = 0.0;
    for (uint i = 0; i < iterations; ++i) {
        const auto begin = clock_type::now();
        enqueue();
        stream << synchronize() << commit();
        const auto end = clock_type::now();
        total_us += std::chrono::duration<double, std::micro>(end - begin).count();
    }
    return total_us / static_cast<double>(iterations);
}

auto make_linear_write_kernel(uint width) {
    Kernel kernel = [width](BufferVar<uint> output) {
        const Uint linear_index = dispatch_idx().x + dispatch_idx().y * width;
        output.write(linear_index, dispatch_id());
    };
    return kernel;
}

}// namespace

int main(int argc, char *argv[]) {
    fs::path path(argv[0]);
    RHIContext &context = RHIContext::instance();
    Device device = context.create_device("cuda");
    device.init_rtx();
    Stream stream = device.create_stream();

    Buffer<uint> output = device.create_buffer<uint>(large_dim.x * large_dim.y, "test_launch_latency_output");

    auto raygen_small_kernel = make_linear_write_kernel(1u);
    raygen_small_kernel.function()->set_raytracing(true);
    auto raygen_small = device.compile(raygen_small_kernel, "launch-latency-raygen-small");

    auto cuda_small_kernel = make_linear_write_kernel(1u);
    cuda_small_kernel.function()->set_raytracing(false);
    auto cuda_small = device.compile(cuda_small_kernel, "launch-latency-cuda-small");

    auto raygen_large_kernel = make_linear_write_kernel(large_dim.x);
    raygen_large_kernel.function()->set_raytracing(true);
    auto raygen_large = device.compile(raygen_large_kernel, "launch-latency-raygen-large");

    auto cuda_default_large_kernel = make_linear_write_kernel(large_dim.x);
    cuda_default_large_kernel.function()->set_raytracing(false);
    auto cuda_default_large = device.compile(cuda_default_large_kernel, "launch-latency-cuda-default-large");

    auto cuda_tiled_large_kernel = make_linear_write_kernel(large_dim.x);
    cuda_tiled_large_kernel.function()->set_raytracing(false);
    cuda_tiled_large_kernel.function()->configure(
        make_uint3((large_dim.x + tiled_block.x - 1u) / tiled_block.x,
                   (large_dim.y + tiled_block.y - 1u) / tiled_block.y,
                   1u),
        tiled_block);
    auto cuda_tiled_large = device.compile(cuda_tiled_large_kernel, "launch-latency-cuda-tiled-large");

    const auto raygen_submit_us = measure_submit_us(stream, launch_iterations, [&] {
        stream << raygen_small(output).dispatch(1u) << commit();
    });
    const auto cuda_submit_us = measure_submit_us(stream, launch_iterations, [&] {
        stream << cuda_small(output).dispatch(1u) << commit();
    });

    const auto raygen_total_us = measure_total_us(stream, large_launch_iterations, [&] {
        stream << raygen_large(output).dispatch(large_dim) << commit();
    });
    const auto cuda_default_total_us = measure_total_us(stream, large_launch_iterations, [&] {
        stream << cuda_default_large(output).dispatch(large_dim) << commit();
    });
    const auto cuda_tiled_total_us = measure_total_us(stream, large_launch_iterations, [&] {
        stream << cuda_tiled_large(output).dispatch(large_dim) << commit();
    });

    cout << "launch-latency benchmark" << endl;
    cout << "small dispatch dim=1" << endl;
    cout << "raygen_submit_us=" << raygen_submit_us << endl;
    cout << "cuda_submit_us=" << cuda_submit_us << endl;
    cout << "large dispatch dim=" << large_dim.x << "x" << large_dim.y << endl;
    cout << "raygen_total_us=" << raygen_total_us << endl;
    cout << "cuda_default_total_us=" << cuda_default_total_us << endl;
    cout << "cuda_tiled_total_us=" << cuda_tiled_total_us << endl;

    stream << synchronize() << commit();
    return 0;
}