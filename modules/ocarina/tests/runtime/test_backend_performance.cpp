#include "tests/backend_test.h"
#include "core/util/logging.h"
#include "dsl/dsl.h"
#include "math/geometry.h"
#include "rhi/common.h"
#include "rhi/context.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <vector>

using namespace ocarina;

namespace {

using Clock = std::chrono::steady_clock;
constexpr uint block_size = 64u;
constexpr uint batch_size = 64u;
constexpr uint arithmetic_steps = 64u;

struct Options {
    bool quick{false};
    bool raytracing{true};
    uint rounds() const { return quick ? 5u : 7u; }
    uint warmups() const { return quick ? 8u : 32u; }
    uint memory_elements() const { return quick ? 1u << 20u : 1u << 22u; }
};

struct Samples {
    std::vector<double> us_per_dispatch;
    uint iterations;
    uint batch;
    uint elements;
};

struct PhaseSamples {
    Samples total;
    Samples enqueue;
    Samples submit;
    Samples wait;
};

void write_record(const std::string &record) {
    // Format before acquiring stdout's FILE lock. One fwrite keeps a complete
    // CSV record together when backend logging occurs on another thread.
    std::fwrite(record.data(), 1, record.size(), stdout);
    std::fflush(stdout);
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "backend-performance verification failed: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

void finish(Stream &stream) {
    stream << synchronize() << commit();
}

template<typename Signature>
void configure(Kernel<Signature> &kernel, uint elements) {
    kernel.function()->set_raytracing(false);
    kernel.function()->configure(make_uint3((elements + block_size - 1u) / block_size, 1u, 1u),
                                 make_uint3(block_size, 1u, 1u));
}

// Each operation includes host command construction, submission and completion.
// A batch has one completion boundary after all its dispatches. Samples are
// round means, so their p95 is NOT a per-dispatch tail latency measurement.
template<typename Enqueue>
Samples measure(Stream &stream, const Options &options, uint iterations,
                uint batch, uint elements, Enqueue &&enqueue) {
    for (uint i = 0; i < options.warmups(); ++i) {
        enqueue();
        finish(stream);
    }
    Samples samples{{}, iterations, batch, elements};
    samples.us_per_dispatch.reserve(options.rounds());
    for (uint round = 0; round < options.rounds(); ++round) {
        const auto begin = Clock::now();
        for (uint i = 0; i < iterations; ++i) {
            enqueue();
            finish(stream);
        }
        const auto end = Clock::now();
        const auto elapsed = std::chrono::duration<double, std::micro>(end - begin).count();
        samples.us_per_dispatch.push_back(elapsed / (double(iterations) * double(batch)));
    }
    return samples;
}

// Separate host-side intervals while retaining the same completed-work boundary.
// GPU execution can overlap commit(), so wait is neither pure GPU time nor pure
// synchronization overhead. Batching amortizes completion latency without
// requiring backend-specific timing APIs or changing either backend's kernels.
template<typename Enqueue>
PhaseSamples measure_phases(Stream &stream, const Options &options, uint iterations,
                           uint batch, uint elements, Enqueue &&enqueue) {
    for (uint i = 0; i < options.warmups(); ++i) {
        enqueue();
        stream << commit();
        finish(stream);
    }
    const Samples empty{{}, iterations, batch, elements};
    PhaseSamples samples{empty, empty, empty, empty};
    for (uint round = 0; round < options.rounds(); ++round) {
        double enqueue_us = 0.0;
        double submit_us = 0.0;
        double wait_us = 0.0;
        for (uint i = 0; i < iterations; ++i) {
            const auto begin = Clock::now();
            enqueue();
            const auto queued = Clock::now();
            stream << commit();
            const auto submitted = Clock::now();
            finish(stream);
            const auto completed = Clock::now();
            enqueue_us += std::chrono::duration<double, std::micro>(queued - begin).count();
            submit_us += std::chrono::duration<double, std::micro>(submitted - queued).count();
            wait_us += std::chrono::duration<double, std::micro>(completed - submitted).count();
        }
        const double dispatches = double(iterations) * double(batch);
        samples.total.us_per_dispatch.push_back((enqueue_us + submit_us + wait_us) / dispatches);
        samples.enqueue.us_per_dispatch.push_back(enqueue_us / dispatches);
        samples.submit.us_per_dispatch.push_back(submit_us / dispatches);
        samples.wait.us_per_dispatch.push_back(wait_us / dispatches);
    }
    return samples;
}

double quantile(std::vector<double> values, double percentile) {
    std::sort(values.begin(), values.end());
    const double position = percentile * double(values.size() - 1u);
    const auto lower = static_cast<size_t>(std::floor(position));
    const auto upper = static_cast<size_t>(std::ceil(position));
    return values[lower] + (values[upper] - values[lower]) * (position - double(lower));
}

void report(const char *backend, const char *codegen, const char *workload,
            const char *unit, const Samples &samples, double numerator = 0.0) {
    auto values = samples.us_per_dispatch;
    if (numerator != 0.0)
        for (auto &value : values) value = numerator / value;
    std::ostringstream summary;
    summary << std::fixed << std::setprecision(6)
            << "PERF_SUMMARY," << backend << ',' << codegen << ',' << workload << ',' << unit
            << ',' << quantile(values, 0.5) << ',' << quantile(values, 0.95)
            << ',' << values.size() << ',' << samples.iterations << ',' << samples.batch
            << ',' << samples.elements << '\n';
    write_record(summary.str());
    for (size_t round = 0; round < values.size(); ++round) {
        std::ostringstream line;
        line << std::fixed << std::setprecision(6)
             << "PERF_ROUND," << backend << ',' << codegen << ',' << workload << ',' << unit
             << ',' << round << ',' << values[round] << '\n';
        write_record(line.str());
    }
}

void report_phases(const char *backend, const char *codegen, const char *workload,
                   const PhaseSamples &samples) {
    report(backend, codegen, workload, "us_per_dispatch", samples.total);
    report(backend, codegen, workload, "cpu_enqueue_us_per_dispatch", samples.enqueue);
    report(backend, codegen, workload, "cpu_submit_us_per_dispatch", samples.submit);
    report(backend, codegen, workload, "completion_wait_us_per_dispatch", samples.wait);
}

void benchmark_launch(Device &device, Stream &stream, const Options &options,
                      const char *backend, const char *codegen) {
    auto output = device.create_buffer<uint>(batch_size, "performance-launch-output");
    Kernel kernel = [](BufferVar<uint> out, Uint slot, Uint value) { out.write(slot, value); };
    configure(kernel, 1u);
    auto shader = device.compile(kernel, "backend-performance-launch");
    auto single = [&] { stream << shader(output, 0u, 37u).dispatch(1u); };
    single();
    uint first = 0u;
    stream << output.view(0u, 1u).download(&first);
    finish(stream);
    require(first == 37u, "single dispatch output");
    auto single_samples = measure(stream, options, options.quick ? 32u : 128u, 1u, 1u, single);
    stream << output.view(0u, 1u).download(&first);
    finish(stream);
    require(first == 37u, "single dispatch output after measurement");
    report(backend, codegen, "dispatch_single", "us_per_dispatch", single_samples);

    auto batched = [&] {
        for (uint i = 0; i < batch_size; ++i)
            stream << shader(output, i, 101u + i).dispatch(1u);
    };
    auto batch_samples = measure(stream, options, options.quick ? 4u : 16u,
                                 batch_size, 1u, batched);
    std::vector<uint> host(batch_size);
    stream << output.download(host.data());
    finish(stream);
    for (uint i = 0; i < batch_size; ++i)
        require(host[i] == 101u + i, "batched dispatch argument lifetime or ordering");
    report(backend, codegen, "dispatch_batch64", "us_per_dispatch", batch_samples);

    auto single_phases = measure_phases(stream, options, options.quick ? 32u : 128u, 1u, 1u, single);
    stream << output.view(0u, 1u).download(&first);
    finish(stream);
    require(first == 37u, "single phase-measurement dispatch output");
    report_phases(backend, codegen, "dispatch_single_phases", single_phases);
    auto batch_phases = measure_phases(stream, options, options.quick ? 4u : 16u,
                                       batch_size, 1u, batched);
    stream << output.download(host.data());
    finish(stream);
    for (uint i = 0; i < batch_size; ++i)
        require(host[i] == 101u + i, "batched phase-measurement dispatch output");
    report_phases(backend, codegen, "dispatch_batch64_phases", batch_phases);
}

void benchmark_buffers(Device &device, Stream &stream, const Options &options,
                       const char *backend, const char *codegen) {
    const uint count = options.memory_elements();
    std::vector<float> lhs(count), rhs(count), host(count);
    for (uint i = 0; i < count; ++i) {
        lhs[i] = float(i % 1024u) / 1024.f;
        rhs[i] = float(i % 257u) / 256.f;
    }
    auto a = device.create_buffer<float>(count, "performance-a");
    auto b = device.create_buffer<float>(count, "performance-b");
    auto output = device.create_buffer<float>(count, "performance-output");
    stream << a.upload(lhs.data()) << b.upload(rhs.data());
    finish(stream);
    Kernel memory_kernel = [](BufferVar<float> left, BufferVar<float> right, BufferVar<float> out) {
        Uint i = dispatch_id();
        out.write(i, left.read(i) * 1.75f + right.read(i) * 0.5f);
    };
    configure(memory_kernel, count);
    auto memory_shader = device.compile(memory_kernel, "backend-performance-buffer-triad");
    auto memory_samples = measure(stream, options, options.quick ? 4u : 16u, 1u, count, [&] {
        stream << memory_shader(a, b, output).dispatch(count);
    });
    stream << output.download(host.data());
    finish(stream);
    for (uint i = 0; i < count; ++i)
        require(host[i] == lhs[i] * 1.75f + rhs[i] * 0.5f, "buffer triad output");
    report(backend, codegen, "buffer_triad", "us_per_dispatch", memory_samples);
    // Logical traffic: two float reads plus one float write per element.
    // This is completed-work effective throughput, not a DRAM counter.
    report(backend, codegen, "buffer_triad", "GB_per_s", memory_samples, double(count) * 12.0 / 1000.0);

    auto memory_batch = measure_phases(stream, options, options.quick ? 2u : 4u,
                                       batch_size, count, [&] {
        for (uint i = 0; i < batch_size; ++i)
            stream << memory_shader(a, b, output).dispatch(count);
    });
    stream << output.download(host.data());
    finish(stream);
    for (uint i = 0; i < count; ++i)
        require(host[i] == lhs[i] * 1.75f + rhs[i] * 0.5f, "batched buffer triad output");
    report_phases(backend, codegen, "buffer_triad_batch64", memory_batch);
    report(backend, codegen, "buffer_triad_batch64", "GB_per_s", memory_batch.total,
           double(count) * 12.0 / 1000.0);

    const uint arithmetic_count = count / 4u;
    Kernel arithmetic_kernel = [](BufferVar<float> left, BufferVar<float> right, BufferVar<float> out) {
        Uint i = dispatch_id();
        Float value = left.read(i);
        Float increment = right.read(i) * (1.f / 1024.f);
        $for(step, arithmetic_steps) {
            value = value * (1.f + 1.f / 1024.f) + increment;
        };
        out.write(i, value);
    };
    configure(arithmetic_kernel, arithmetic_count);
    auto arithmetic_shader = device.compile(arithmetic_kernel, "backend-performance-arithmetic");
    auto arithmetic_samples = measure(stream, options, options.quick ? 4u : 16u, 1u,
                                      arithmetic_count, [&] {
        stream << arithmetic_shader(a, b, output).dispatch(arithmetic_count);
    });
    stream << output.view(0u, arithmetic_count).download(host.data());
    finish(stream);
    std::vector<float> arithmetic_expected(arithmetic_count);
    for (uint i = 0; i < arithmetic_count; ++i) {
        float expected = lhs[i];
        const float increment = rhs[i] / 1024.f;
        for (uint step = 0; step < arithmetic_steps; ++step)
            expected = expected * (1.f + 1.f / 1024.f) + increment;
        arithmetic_expected[i] = expected;
        require(std::isfinite(host[i]) && std::abs(host[i] - expected) <= 2e-4f,
                "arithmetic output");
    }
    report(backend, codegen, "arithmetic64", "us_per_dispatch", arithmetic_samples);
    // Nominal source-level operations; compiler instruction counts may differ.
    report(backend, codegen, "arithmetic64", "nominal_GFLOP_per_s", arithmetic_samples,
           double(arithmetic_count) * double(arithmetic_steps * 2u) / 1000.0);

    auto arithmetic_batch = measure_phases(stream, options, options.quick ? 2u : 4u,
                                           batch_size, arithmetic_count, [&] {
        for (uint i = 0; i < batch_size; ++i)
            stream << arithmetic_shader(a, b, output).dispatch(arithmetic_count);
    });
    stream << output.view(0u, arithmetic_count).download(host.data());
    finish(stream);
    for (uint i = 0; i < arithmetic_count; ++i)
        require(std::isfinite(host[i]) && std::abs(host[i] - arithmetic_expected[i]) <= 2e-4f,
                "batched arithmetic output");
    report_phases(backend, codegen, "arithmetic64_batch64", arithmetic_batch);
    report(backend, codegen, "arithmetic64_batch64", "nominal_GFLOP_per_s", arithmetic_batch.total,
           double(arithmetic_count) * double(arithmetic_steps * 2u) / 1000.0);
}

void benchmark_rays(Device &device, Stream &stream, const Options &options,
                    const char *backend, const char *codegen) {
    const uint count = options.quick ? 1u << 16u : 1u << 18u;
    array<float3, 3> vertices{make_float3(-1.f, -1.f, 0.f), make_float3(1.f, -1.f, 0.f),
                              make_float3(0.f, 1.f, 0.f)};
    array<Triangle, 1> triangles{Triangle(0u, 1u, 2u)};
    auto vertex_buffer = device.create_buffer<float3>(vertices.size(), "performance-ray-vertices");
    auto index_buffer = device.create_buffer<Triangle>(triangles.size(), "performance-ray-indices");
    stream << vertex_buffer.upload(vertices.data()) << index_buffer.upload(triangles.data());
    finish(stream);
    auto mesh = device.create_mesh(vertex_buffer.view(), index_buffer.view(), FAST_TRACE, DISABLE_ANYHIT);
    stream << mesh.build_bvh();
    finish(stream);
    auto accel = device.create_accel(FAST_TRACE);
    accel.add_instance(ocarina::move(mesh), make_float4x4(1.f));
    stream << accel.build_bvh();
    finish(stream);
    auto output = device.create_buffer<uint>(count, "performance-ray-output");
    Kernel kernel = [&accel](BufferVar<uint> out) {
        Uint i = dispatch_id();
        Float x = cast<float>(i % 128u) / 512.f - 0.125f + cast<float>(i % 2u) * 4.f;
        Float y = cast<float>((i / 128u) % 128u) / 512.f - 0.125f;
        TriangleHitVar hit = accel.trace_closest(make_ray(make_float3(x, y, 1.f), make_float3(0.f, 0.f, -1.f)));
        $if(hit->is_miss()) { out.write(i, 0u); }
        $else { out.write(i, 1u); };
    };
    kernel.function()->configure(make_uint3(count / block_size, 1u, 1u), make_uint3(block_size, 1u, 1u));
    auto shader = device.compile(kernel, "backend-performance-rays");
    auto samples = measure(stream, options, options.quick ? 4u : 16u, 1u, count, [&] {
        stream << shader(output).dispatch(count);
    });
    std::vector<uint> host(count);
    stream << output.download(host.data());
    finish(stream);
    for (uint i = 0; i < count; ++i)
        require(host[i] == (i % 2u == 0u ? 1u : 0u), "ray hit/miss output");
    report(backend, codegen, "trace_closest_1triangle_half_hit", "us_per_dispatch", samples);
    report(backend, codegen, "trace_closest_1triangle_half_hit", "Mray_per_s", samples, double(count));

    auto batch = measure_phases(stream, options, options.quick ? 2u : 4u,
                                batch_size, count, [&] {
        for (uint i = 0; i < batch_size; ++i)
            stream << shader(output).dispatch(count);
    });
    stream << output.download(host.data());
    finish(stream);
    for (uint i = 0; i < count; ++i)
        require(host[i] == (i % 2u == 0u ? 1u : 0u), "batched ray hit/miss output");
    report_phases(backend, codegen, "trace_closest_1triangle_half_hit_batch64", batch);
    report(backend, codegen, "trace_closest_1triangle_half_hit_batch64", "Mray_per_s", batch.total, double(count));
}

}// namespace

int main(int argc, char *argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--quick") options.quick = true;
        else if (arg == "--no-raytracing") options.raytracing = false;
        else {
            std::cerr << "Usage: test-runtime-backend-performance [--quick] [--no-raytracing]\n";
            return EXIT_FAILURE;
        }
    }
    const char *backend = test_backend_name();
    const char *codegen_env = std::getenv("OCARINA_TEST_CODEGEN");
    const char *codegen = codegen_env && std::string_view(codegen_env) == "ir" ? "ir" : "ast";
    auto &context = RHIContext::instance();
    auto device = context.create_device(backend);
    if (options.raytracing) device.init_rtx();
    auto stream = device.create_stream();
    logger().flush();
    std::ostringstream metadata;
    metadata << "PERF_METADATA,timer=steady_clock,boundary=enqueue_submit_completion,compile_and_upload=excluded"
             << ",statistic=round_mean,percentile=linear_interpolation,block_size=" << block_size
             << ",phase_wait=host_completion_wait_not_gpu_timestamp,batch_reuses_resident_buffers=1"
             << ",warmups=" << options.warmups() << ",quick=" << options.quick << '\n';
    write_record(metadata.str());
    write_record("PERF_HEADER,backend,codegen,workload,unit,median,p95,rounds,iterations,batch,elements\n");
    benchmark_launch(device, stream, options, backend, codegen);
    benchmark_buffers(device, stream, options, backend, codegen);
    if (options.raytracing) benchmark_rays(device, stream, options, backend, codegen);
    std::ostringstream verified;
    verified << "PERF_VERIFIED," << backend << ',' << codegen << '\n';
    write_record(verified.str());
    return EXIT_SUCCESS;
}
