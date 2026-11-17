#include "backends/vulkan_compute/slang_compiler.h"
#include "core/util/logging.h"
#include "dsl/dsl.h"
#include "rhi/context.h"

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

bool has_capability(const vector<uint32_t> &spirv, uint32_t capability) {
    require(spirv.size() >= 5u && spirv[0] == 0x07230203u, "missing SPIR-V module");
    for (size_t offset = 5u; offset < spirv.size();) {
        const uint32_t count = spirv[offset] >> 16u;
        require(count != 0u && count <= spirv.size() - offset, "malformed SPIR-V instruction");
        if ((spirv[offset] & 0xffffu) == 17u && count == 2u && spirv[offset + 1u] == capability)
            return true;
        offset += count;
    }
    return false;
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
    // Keep fatal compiler diagnostics visible in CTest as well as the file
    // inspected below, including failures before the final file read.
    logging.console = true;
    logging.file_path = diagnostics_path;
    horizon::core::initialize_logging(logging);
    auto &context = RHIContext::instance().init(argv[0], cache);
    // Validate a declared Vulkan target without creating a device or pipeline.
    // Executable shaders still use capabilities queried from their real device.
    SlangCompileTarget target;
    target.warp32 = true;
    target.ray_query = ray_query;
    SlangShaderCompiler compiler{&context, target};
    auto compile = [&](const auto &kernel, const char *description) {
        kernel.function()->set_description(description);
        auto spirv = compiler.compile(*kernel.function(), make_uint3(64u, 1u, 1u));
        require(!spirv.empty(), "kernel did not produce SPIR-V");
        return spirv;
    };

    // Texture intrinsics carry Slang's standard-library SPIR-V requirements.
    Kernel texture = [](Texture2DVar input, BufferVar<float4> output) {
        output.write(0u, input.sample(4u, make_float2(0.5f)).as_vec4());
    };
    auto texture_spirv = compile(texture, "slang-capabilities-texture");
    require(has_capability(texture_spirv, 1u), "texture module has no Shader capability");

    Kernel ballot = [](BufferVar<uint4> output) {
        output.write(0u, warp_active_bit_mask(Bool(true)));
    };
    auto ballot_spirv = compile(ballot, "slang-capabilities-ballot");
    require(has_capability(ballot_spirv, 64u), "ballot module has no GroupNonUniformBallot capability");

    // Ray-query compilation is independent of the host device's support.
    if (ray_query) {
        Kernel trace = [](Var<Accel> accel, BufferVar<TriangleHit> output) {
            Float3 origin = make_float3(0.f);
            Float3 direction = make_float3(0.f, 0.f, 1.f);
            auto ray = make_ray(origin, direction);
            output.write(0u, accel.trace_closest(ray));
        };
        auto trace_spirv = compile(trace, "slang-capabilities-ray-query");
        require(has_capability(trace_spirv, 4472u), "trace module has no RayQueryKHR capability");
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
    std::cout << "Slang target capability checks passed: " << compiles
              << " kernels compiled without a Vulkan device" << std::endl;
}
