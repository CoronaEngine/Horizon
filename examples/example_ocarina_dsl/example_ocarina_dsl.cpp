// Ocarina DSL example: C++ EDSL -> Slang source -> Vulkan compute -> PNG.
//
// Run through the existing example host:
//
//   HorizonExamples ocarina_dsl [--size 960x540] [--print-slang]
//
// The example deliberately keeps the image simple (a raymarched sphere over a
// checkerboard floor) so the reader can focus on the complete pipeline:
//
//   1. Build a compute kernel with Ocarina's C++ EDSL.
//   2. Emit the Slang source that the Vulkan backend generates for it.
//   3. Compile the same kernel through the RHI (Slang -> SPIR-V) and dispatch
//      it on Vulkan, writing the result into a storage texture.
//   4. Read the texture back and save it as a PNG.
//
// Ocarina's Vulkan backend is compute-only. "Rendering" therefore means a
// compute shader writing an image, not a graphics pipeline.

#include "backends/vulkan_compute/ast_to_slang_source.h"
#include "core/image/image.h"
#include "dsl/dsl.h"
#include "rhi/common.h"
#include "rhi/context.h"

#include "example_ocarina_dsl/example_ocarina_dsl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

using namespace ocarina;
namespace stdfs = std::filesystem;

namespace {

struct Options {
    stdfs::path output_dir{"ocarina_dsl_example_output"};
    stdfs::path runtime_dir;
    uint width{960u};
    uint height{540u};
    bool print_slang{false};
    bool help{false};
};

[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error(message);
}

void print_help(const char *executable) {
    std::cout
        << "Ocarina DSL -> Slang -> Vulkan compute example\n\n"
        << "usage: " << executable << " ocarina_dsl [options]\n\n"
        << "  --output <dir>       directory for the .slang and .png files\n"
        << "                       (default: ocarina_dsl_example_output)\n"
        << "  --runtime-dir <dir>  directory containing the Ocarina backend and\n"
        << "                       its slang/ headers (default: auto-detect)\n"
        << "  --size <WxH>         output resolution (default: 960x540)\n"
        << "  --print-slang        print the complete generated Slang source\n"
        << "  -h, --help           show this help\n";
}

uint parse_uint(const std::string &text, const char *what) {
    try {
        size_t consumed = 0u;
        const unsigned long value = std::stoul(text, &consumed);
        if (consumed != text.size() || value == 0ul || value > 16384ul)
            fail(std::string("invalid ") + what + ": " + text);
        return static_cast<uint>(value);
    } catch (const std::exception &) {
        fail(std::string("invalid ") + what + ": " + text);
    }
}

Options parse_options(int argc, char **argv) {
    Options options;
    // argv[1] is the "ocarina_dsl" mode consumed by HorizonExamples.
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        auto next_value = [&](const char *name) -> std::string {
            if (++i >= argc) fail(std::string("missing value for ") + name);
            return argv[i];
        };

        if (argument == "-h" || argument == "--help") {
            options.help = true;
            return options;
        }
        if (argument == "--output") {
            options.output_dir = next_value("--output");
        } else if (argument == "--runtime-dir") {
            options.runtime_dir = next_value("--runtime-dir");
        } else if (argument == "--size") {
            const std::string size = next_value("--size");
            const auto separator = size.find_first_of("xX");
            if (separator == std::string::npos)
                fail("--size expects <width>x<height>, for example 960x540");
            options.width = parse_uint(size.substr(0u, separator), "width");
            options.height = parse_uint(size.substr(separator + 1u), "height");
        } else if (argument == "--print-slang") {
            options.print_slang = true;
        } else {
            fail("unknown argument: " + argument);
        }
    }
    return options;
}

// The backend needs its module DLL and the sibling slang/ include directory.
// Prefer the current directory (the usual CMake/CTest layout), then the
// executable's directory so the example also works when launched by path.
stdfs::path locate_runtime_dir(const Options &options, const char *argv0) {
    if (!options.runtime_dir.empty()) return stdfs::absolute(options.runtime_dir);

    const auto current = stdfs::current_path();
    if (stdfs::exists(current / "slang")) return current;

    std::error_code error;
    const auto executable_dir = stdfs::absolute(argv0, error).parent_path();
    if (!error && !executable_dir.empty() && stdfs::exists(executable_dir / "slang"))
        return executable_dir;
    return current;
}

// The expression is intentionally a normal C++ lambda: Ocarina records the
// statements and expressions as an AST while this function runs.
auto make_raymarch_kernel() {
    return Kernel{[](Texture2DVar output) {
        // dispatch_idx()/dispatch_dim() expose the 2D pixel coordinate and the
        // logical image size to the kernel.
        Uint2 pixel = dispatch_idx().xy();
        Uint2 size = dispatch_dim().xy();

        Float2 uv = (make_float2(cast<float>(pixel.x), cast<float>(pixel.y)) + 0.5f) /
                    make_float2(cast<float>(size.x), cast<float>(size.y));
        uv = uv * 2.f - 1.f;
        // dispatch_idx() starts at the top-left; NDC y points up.
        const Float2 ndc = make_float2(uv.x, -uv.y);
        const Float aspect = cast<float>(size.x) / cast<float>(size.y);

        const Float3 ray_origin = make_float3(0.f, 0.35f, 3.2f);
        const Float3 ray_direction = normalize(make_float3(ndc.x * aspect, ndc.y, -1.6f));

        // Sphere at the origin, unit radius, over a plane at y = -1.
        Float t = 0.f;
        Bool hit = false;
        Float3 normal = make_float3(0.f, 1.f, 0.f);
        Float3 albedo = make_float3(0.1f);

        $for(step, 96u) {
            const Float3 position = ray_origin + ray_direction * t;
            const Float sphere_distance = length(position) - 1.f;
            const Float plane_distance = position.y + 1.f;
            const Float distance = min(sphere_distance, plane_distance);

            $if(distance < 0.001f) {
                $if(sphere_distance < plane_distance) {
                    normal = normalize(position);
                    albedo = make_float3(0.86f, 0.33f, 0.18f);
                } $else {
                    normal = make_float3(0.f, 1.f, 0.f);
                    const Float checker = abs(fmod(floor(position.x) + floor(position.z), 2.f));
                    albedo = make_float3(0.78f);
                    $if(checker > 0.5f) { albedo = make_float3(0.16f); };
                };
                hit = true;
                $break;
            };

            t += distance;
            $if(t > 20.f) { $break; };
        };

        Float3 color = make_float3(0.03f, 0.05f, 0.10f);
        $if(hit) {
            const Float3 position = ray_origin + ray_direction * t;
            const Float3 light_direction = normalize(make_float3(0.6f, 0.9f, 0.35f));
            const Float diffuse = max(dot(normal, light_direction), 0.f);

            const Float3 view_direction = -ray_direction;
            const Float3 half_direction = normalize(light_direction + view_direction);
            const Float specular = pow(max(dot(normal, half_direction), 0.f), 48.f);

            color = albedo * (0.12f + 0.88f * diffuse) + make_float3(0.55f) * specular;

            // Distance fog blends the surface into the sky.
            const Float fog = saturate(1.f - exp(-0.015f * t * t));
            color = lerp(fog, color, make_float3(0.03f, 0.05f, 0.10f));
        } $else {
            const Float sky_t = clamp(ndc.y * 0.5f + 0.5f, 0.f, 1.f);
            color = lerp(sky_t, make_float3(0.92f, 0.58f, 0.36f), make_float3(0.08f, 0.16f, 0.42f));
        };

        // The shader writes gamma-encoded values, matching the 8-bit PNG.
        color = pow(saturate(color), make_float3(1.f / 2.2f));
        output.write(make_float4(color, 1.f), pixel.x, pixel.y);
    }};
}

// This mirrors the prelude that SlangShaderCompiler::compile_program() puts in
// front of the emitted body. Keeping it here lets the example save a complete,
// readable .slang file.
std::string compose_slang_source(const EmittedSlangSource &emitted, uint3 block_dim,
                                 bool native_float32_fma) {
    std::ostringstream source;
    source << "// Generated for the Ocarina DSL example.\n"
           << "// The Vulkan backend prepends this prelude before compiling the body.\n"
           << "#define OC_RAY_QUERY 0\n"
           << "#define OC_RAY_PIPELINE 0\n"
           << "#define OC_PRIMITIVE_CULLING 0\n"
           << "#define OC_WORKGROUP_X " << block_dim.x << "\n"
           << "#define OC_WORKGROUP_Y " << block_dim.y << "\n"
           << "#define OC_WORKGROUP_Z " << block_dim.z << "\n";
    if (native_float32_fma) source << "#define OC_NATIVE_FLOAT32_FMA 1\n";
    source << "#include \"slang_device_resource.slang\"\n"
           << "#include \"slang_device_math.slang\"\n"
           << emitted.source;
    return source.str();
}

void write_text_file(const stdfs::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary);
    if (!file) fail("cannot open '" + path.string() + "' for writing");
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) fail("failed to write '" + path.string() + "'");
}

std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(std::move(line));
    return lines;
}

void print_slang(const std::string &source, bool print_all) {
    const auto lines = split_lines(source);
    constexpr size_t preview_lines = 24u;
    std::cout << "--- generated Slang (" << lines.size() << " lines) ---\n";
    const size_t count = print_all ? lines.size() : std::min(lines.size(), preview_lines);
    for (size_t i = 0; i < count; ++i) std::cout << lines[i] << '\n';
    if (!print_all && lines.size() > preview_lines) {
        std::cout << "... (" << lines.size() - preview_lines
                  << " more lines; use --print-slang to print all)\n";
    }
    std::cout << "--- end generated Slang ---\n";
}

void verify_pixels(const std::vector<float4> &pixels) {
    size_t visible = 0u;
    for (const float4 pixel : pixels) {
        if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(pixel.z) ||
            !std::isfinite(pixel.w))
            fail("the GPU produced a non-finite pixel");
        if (pixel.x + pixel.y + pixel.z > 0.05f) ++visible;
    }
    if (visible == 0u) fail("the rendered image is completely black");
}

int render_example(const Options &options, const char *argv0) {
    const auto output_dir = stdfs::absolute(options.output_dir);
    stdfs::create_directories(output_dir);
    const auto runtime_dir = locate_runtime_dir(options, argv0);

    std::cout << "runtime directory: " << runtime_dir.string() << '\n'
              << "output directory:  " << output_dir.string() << '\n';

    // RHIContext discovers ocarina-backend-vulkan-compute in the runtime
    // directory and loads it as a dynamic module. It records the current
    // directory the first time instance() is touched, so start there and then
    // restore the caller's working directory.
    const auto original_directory = stdfs::current_path();
    stdfs::current_path(runtime_dir);
    RHIContext::instance().init(runtime_dir, ".cache");
    stdfs::current_path(original_directory);
    auto device = RHIContext::instance().create_device();

    // 1. EDSL construction. The kernel body runs once on the host to record an
    //    AST; the texture argument becomes a shader parameter.
    auto kernel = make_raymarch_kernel();
    constexpr uint3 block_dim = make_uint3(8u, 8u, 1u);
    kernel.function()->set_block_dim(block_dim);
    kernel.set_description("ocarina_dsl_raymarch");

    const std::string shader_name = "ocarina-dsl-raymarch";
    const uint2 resolution = make_uint2(options.width, options.height);
    std::cout << "kernel: " << kernel.function()->description()
              << " | arguments: " << kernel.function()->all_arguments().size()
              << " | workgroup: " << block_dim.x << 'x' << block_dim.y << 'x' << block_dim.z
              << " | resolution: " << resolution.x << 'x' << resolution.y << '\n';

    // 2. Slang generation. AstToSlangSource is the backend's AST visitor; the
    //    same call happens again inside device.compile().
    const ShaderCompilationContext context{{}, ShaderExecutionMode::Compute, shader_name};
    const auto emitted = AstToSlangSource::generate(
        *kernel.function(), context, /*obfuscation=*/false, /*supports_warp32=*/false);
    if (emitted.source.empty()) fail("the Slang source emitter produced an empty body");

    const std::string slang_source =
        compose_slang_source(emitted, block_dim, device.supports_native_float32_fma());
    const auto slang_path = output_dir / (shader_name + ".slang");
    write_text_file(slang_path, slang_source);
    std::cout << "slang source:      " << slang_path.string() << '\n';
    print_slang(slang_source, options.print_slang);

    // 3. Vulkan compute. device.compile() maps the AST to Slang, invokes the
    //    Slang compiler, and creates the VkComputePipeline.
    auto shader = device.compile(kernel, shader_name);
    std::cout << "execution mode:    "
              << (shader.execution_mode() == ShaderExecutionMode::Compute ? "Compute" : "RayTracing")
              << '\n';

    auto texture = device.create_texture2d(resolution, PixelStorage::FLOAT4, shader_name);
    std::vector<float4> pixels(static_cast<size_t>(resolution.x) * resolution.y);

    auto stream = device.create_stream();
    stream << shader(texture).dispatch(resolution)
           << texture.download(pixels.data())
           << synchronize()
           << commit();

    // 4. Readback and save.
    verify_pixels(pixels);
    const auto image_path = output_dir / (shader_name + ".png");
    Image::save_image(image_path, PixelStorage::FLOAT4, resolution, pixels.data());
    std::cout << "rendered image:    " << image_path.string() << '\n'
              << "pixels:            " << pixels.size() << '\n';
    return EXIT_SUCCESS;
}

}// namespace

int run_example_ocarina_dsl(int argc, char **argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.help) {
            print_help(argv[0]);
            return EXIT_SUCCESS;
        }
        return render_example(options, argv[0]);
    } catch (const std::exception &error) {
        std::cerr << "ocarina_dsl example: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
