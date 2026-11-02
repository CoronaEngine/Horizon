#include "tests/backend_test.h"
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
    Device device = create_test_device();
    device.init_rtx();
    Stream stream = device.create_stream();

    Buffer<uint> output = device.create_buffer<uint>(large_dim.x * large_dim.y, "test_launch_latency_output");

    auto raygen_small_kernel = make_linear_write_kernel(1u);
    raygen_small_kernel.function()->set_raytracing(true);
    auto raygen_small = device.compile(raygen_small_kernel, "launch-latency-raygen-small");

    auto compute_small_kernel = make_linear_write_kernel(1u);
    compute_small_kernel.function()->set_raytracing(false);
    auto compute_small = device.compile(compute_small_kernel, "launch-latency-compute-small");

    auto raygen_large_kernel = make_linear_write_kernel(large_dim.x);
    raygen_large_kernel.function()->set_raytracing(true);
    auto raygen_large = device.compile(raygen_large_kernel, "launch-latency-raygen-large");

    auto compute_default_large_kernel = make_linear_write_kernel(large_dim.x);
    compute_default_large_kernel.function()->set_raytracing(false);
    auto compute_default_large = device.compile(compute_default_large_kernel, "launch-latency-compute-default-large");

    auto compute_tiled_large_kernel = make_linear_write_kernel(large_dim.x);
    compute_tiled_large_kernel.function()->set_raytracing(false);
    compute_tiled_large_kernel.function()->configure(
        make_uint3((large_dim.x + tiled_block.x - 1u) / tiled_block.x,
                   (large_dim.y + tiled_block.y - 1u) / tiled_block.y,
                   1u),
        tiled_block);
    auto compute_tiled_large = device.compile(compute_tiled_large_kernel, "launch-latency-compute-tiled-large");

    const auto raygen_submit_us = measure_submit_us(stream, launch_iterations, [&] {
        stream << raygen_small(output).dispatch(1u) << commit();
    });
    const auto compute_submit_us = measure_submit_us(stream, launch_iterations, [&] {
        stream << compute_small(output).dispatch(1u) << commit();
    });

    const auto raygen_total_us = measure_total_us(stream, large_launch_iterations, [&] {
        stream << raygen_large(output).dispatch(large_dim) << commit();
    });
    const auto compute_default_total_us = measure_total_us(stream, large_launch_iterations, [&] {
        stream << compute_default_large(output).dispatch(large_dim) << commit();
    });
    const auto compute_tiled_total_us = measure_total_us(stream, large_launch_iterations, [&] {
        stream << compute_tiled_large(output).dispatch(large_dim) << commit();
    });

    cout << "launch-latency benchmark" << endl;
    cout << "small dispatch dim=1" << endl;
    cout << "raygen_submit_us=" << raygen_submit_us << endl;
    cout << "compute_submit_us=" << compute_submit_us << endl;
    cout << "large dispatch dim=" << large_dim.x << "x" << large_dim.y << endl;
    cout << "raygen_total_us=" << raygen_total_us << endl;
    cout << "compute_default_total_us=" << compute_default_total_us << endl;
    cout << "compute_tiled_total_us=" << compute_tiled_total_us << endl;

    stream << synchronize() << commit();
    return 0;
}
