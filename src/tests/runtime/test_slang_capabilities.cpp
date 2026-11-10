#include "tests/backend_test.h"
#include "core/util/logging.h"
#include "dsl/dsl.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iterator>
#include <string_view>

using namespace ocarina;

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "Slang capabilities: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}
}

int main(int argc, char **argv) {
    const bool ray_query = argc == 3 && std::string_view(argv[1]) == "--case" &&
                           std::string_view(argv[2]) == "ray-query";
    require(argc == 1 || ray_query, "usage: test-slang-capabilities [--case ray-query]");

    // A fresh cache forces Slang to diagnose every kernel on every test run.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto cache = ocarina::format(".cache-slang-capabilities-{}", stamp);
    const auto diagnostics_path = std::filesystem::path{cache} / "diagnostics.log";
    horizon::core::LoggingOptions logging;
    logging.console = false;
    logging.file_path = diagnostics_path;
    horizon::core::initialize_logging(logging);
    RHIContext::instance().init(argv[0], cache);
    auto device = create_test_device();
    if (ray_query && skip_without_feature(device.supports_raytracing(), "ray queries")) return 0;

    // Texture intrinsics carry Slang's standard-library SPIR-V requirements.
    Kernel texture = [](Texture2DVar input, BufferVar<float4> output) {
        output.write(0u, input.sample(4u, make_float2(0.5f)).as_vec4());
    };
    auto texture_shader = device.compile(texture, "slang-capabilities-texture");

    Kernel ballot = [](BufferVar<uint4> output) {
        output.write(0u, warp_active_bit_mask(Bool(true)));
    };
    auto ballot_shader = device.compile(ballot, "slang-capabilities-ballot");

    // Run this case on devices with Vulkan ray-query support.
    if (ray_query) {
        Kernel trace = [](Var<Accel> accel, BufferVar<TriangleHit> output) {
            Float3 origin = make_float3(0.f);
            Float3 direction = make_float3(0.f, 0.f, 1.f);
            auto ray = make_ray(origin, direction);
            output.write(0u, accel.trace_closest(ray));
        };
        auto trace_shader = device.compile(trace, "slang-capabilities-ray-query");
    }

    log_flush();
    std::ifstream diagnostics{diagnostics_path};
    const std::string output{std::istreambuf_iterator<char>{diagnostics}, {}};
    size_t compiles = 0;
    for (size_t offset = 0; (offset = output.find("shader SPIR-V cache miss:", offset)) != std::string::npos;
         ++offset) ++compiles;
    require(compiles == (ray_query ? 3u : 2u), "kernels did not compile through a fresh Slang session");
    if (output.find("41012") != std::string::npos ||
        output.find("profile implicitly upgraded") != std::string::npos) {
        std::cerr << output;
        require(false, "target profile was implicitly upgraded");
    }
    std::cout << "Slang target capability checks passed" << std::endl;
}
