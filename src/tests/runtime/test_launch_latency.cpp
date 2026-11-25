#include "tests/backend_test.h"
#include "core/stl.h"
#include "dsl/dsl.h"
#include "math/base.h"
#include "rhi/common.h"
#include "rhi/context.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

using namespace ocarina;

namespace {

using clock_type = std::chrono::high_resolution_clock;

constexpr uint3 tiled_block = make_uint3(8u, 8u, 1u);

struct Options {
    bool ci{false};
    uint warmups() const { return ci ? 1u : 32u; }
    uint launches() const { return ci ? 4u : 2000u; }
    uint large_launches() const { return ci ? 2u : 128u; }
    uint2 large_dim() const { return ci ? make_uint2(64u, 32u) : make_uint2(1024u, 1024u); }
};

template<typename Enqueue>
double measure_submit_us(Stream &stream, const Options &options, uint iterations, Enqueue &&enqueue) {
    for (uint i = 0; i < options.warmups(); ++i) {
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
double measure_total_us(Stream &stream, const Options &options, uint iterations, Enqueue &&enqueue) {
    for (uint i = 0; i < options.warmups(); ++i) {
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

void reset_output(Stream &stream, Buffer<uint> &output, vector<uint> &host) {
    std::fill(host.begin(), host.end(), 0xffffffffu);
    stream << output.upload(host.data()) << synchronize() << commit();
}

void verify_output(Stream &stream, Buffer<uint> &output, vector<uint> &host,
                   uint written, const char *variant) {
    stream << output.download(host.data()) << synchronize() << commit();
    for (size_t i = 0; i < host.size(); ++i) {
        const uint expected = i < written ? uint(i) : 0xffffffffu;
        if (host[i] != expected) {
            std::cerr << "launch-latency verification failed: " << variant
                      << " index=" << i << " actual=" << host[i]
                      << " expected=" << expected << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }
    std::cout << "LAUNCH_LATENCY_CHECK," << variant << ",PASS" << std::endl;
}

}// namespace

int main(int argc, char *argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--ci") options.ci = true;
        else {
            std::cerr << "Usage: test-runtime-launch-latency [--ci]\n";
            return EXIT_FAILURE;
        }
    }
    const uint2 large_dim = options.large_dim();
    Device device = create_test_device();
    device.init_rtx();
    Stream stream = device.create_stream();

    Buffer<uint> output = device.create_buffer<uint>(large_dim.x * large_dim.y, "test_launch_latency_output");
    vector<uint> host(large_dim.x * large_dim.y);

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

    reset_output(stream, output, host);
    const auto raygen_submit_us = measure_submit_us(stream, options, options.launches(), [&] {
        stream << raygen_small(output).dispatch(1u) << commit();
    });
    verify_output(stream, output, host, 1u, "raygen-small");
    reset_output(stream, output, host);
    const auto compute_submit_us = measure_submit_us(stream, options, options.launches(), [&] {
        stream << compute_small(output).dispatch(1u) << commit();
    });
    verify_output(stream, output, host, 1u, "compute-small");

    reset_output(stream, output, host);
    const auto raygen_total_us = measure_total_us(stream, options, options.large_launches(), [&] {
        stream << raygen_large(output).dispatch(large_dim) << commit();
    });
    verify_output(stream, output, host, uint(host.size()), "raygen-large");
    reset_output(stream, output, host);
    const auto compute_default_total_us = measure_total_us(stream, options, options.large_launches(), [&] {
        stream << compute_default_large(output).dispatch(large_dim) << commit();
    });
    verify_output(stream, output, host, uint(host.size()), "compute-default-large");
    reset_output(stream, output, host);
    const auto compute_tiled_total_us = measure_total_us(stream, options, options.large_launches(), [&] {
        stream << compute_tiled_large(output).dispatch(large_dim) << commit();
    });
    verify_output(stream, output, host, uint(host.size()), "compute-tiled-large");

    cout << "launch-latency benchmark mode=" << (options.ci ? "ci" : "full") << endl;
    cout << "small dispatch dim=1" << endl;
    cout << "raygen_submit_us=" << raygen_submit_us << endl;
    cout << "compute_submit_us=" << compute_submit_us << endl;
    cout << "large dispatch dim=" << large_dim.x << "x" << large_dim.y << endl;
    cout << "raygen_total_us=" << raygen_total_us << endl;
    cout << "compute_default_total_us=" << compute_default_total_us << endl;
    cout << "compute_tiled_total_us=" << compute_tiled_total_us << endl;

    stream << synchronize() << commit();
    cout << "LAUNCH_LATENCY_VERIFIED,variants=5" << endl;
    return EXIT_SUCCESS;
}
