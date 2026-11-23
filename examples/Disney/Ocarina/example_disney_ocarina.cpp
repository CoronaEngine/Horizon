#include "dsl/dsl.h"
#include "rhi/common.h"
#include "Horizon.h"
#include "rhi/context.h"
#include "math/transform.h"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#undef STB_IMAGE_WRITE_IMPLEMENTATION
#undef STB_IMAGE_WRITE_STATIC

void run_example_disney_ocarina() {
    using ocarina::Float;
    using ocarina::Float2;
    using ocarina::Float3;
    using ocarina::Float4;
    using ocarina::make_float2;
    using ocarina::make_float3;
    using ocarina::make_float4;
    constexpr auto mode = "disney_ocarina";

    // Slang builtins and the dynamically loaded backend live next to the executable.
#ifdef _WIN32
    std::wstring filename(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, filename.data(), static_cast<DWORD>(filename.size()));
    if (!length || length >= filename.size())
        throw std::runtime_error("Cannot locate HorizonExamples");
    filename.resize(length);
    const auto executable = std::filesystem::path(filename);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string filename(size, '\0');
    if (_NSGetExecutablePath(filename.data(), &size))
        throw std::runtime_error("Cannot locate HorizonExamples");
    const auto executable = std::filesystem::canonical(filename.c_str());
#else
    const auto executable = std::filesystem::read_symlink("/proc/self/exe");
#endif
    auto& context = ocarina::RHIContext::instance(executable.parent_path());
    auto device = context.create_device();
    if (!device.supports_ray_queries())
        throw std::runtime_error("Ocarina ray queries unsupported on this device");
    device.init_rtx();
    auto stream = device.create_stream();

    auto env_int = [](const char* name) {
        const char* value = std::getenv(name);
        if (!value || !*value) return 0;
        char* end{};
        const long parsed = std::strtol(value, &end, 10);
        if (end == value || *end || parsed < 0 || parsed > 10000000)
            throw std::invalid_argument(std::string("Invalid ") + name);
        return static_cast<int>(parsed);
    };
    const bool hidden = env_int("HORIZON_BENCH_HIDDEN") != 0;
    int remaining_frames = env_int("HORIZON_PRESENT_FRAMES");

    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    if (hidden) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto close_window = [](GLFWwindow* handle) {
        glfwDestroyWindow(handle);
        glfwTerminate();
    };
    std::unique_ptr<GLFWwindow, decltype(close_window)> window(
        glfwCreateWindow(1920, 1080, mode, nullptr, nullptr), close_window);
    if (!window) {
        glfwTerminate();
        throw std::runtime_error("glfwCreateWindow failed");
    }
    void* native_window;
#if defined(_WIN32)
    native_window = glfwGetWin32Window(window.get());
#elif defined(__APPLE__)
    native_window = glfwGetCocoaView(window.get());
#else
    throw std::runtime_error("Native Horizon window handles are unavailable on this platform");
#endif

    // Pack the six faces of each DDS mip into a row of a 2D texture.
    struct Environment {
        ocarina::Texture2D atlas;
        unsigned face_size, levels;
    };
    Environment radiance{}, irradiance{};
    const auto assets = std::filesystem::path(__FILE__).parent_path().parent_path() / "assets" / "env";
    for (auto [filename, env] : {std::pair{"bolonga_lod.dds", &radiance}, std::pair{"bolonga_irr.dds", &irradiance}}) {
        const auto path = assets / filename;
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error("Failed to open DDS file: " + path.string());
        constexpr size_t header_size = 148;
        const std::streamsize size = file.tellg();
        if (size < static_cast<std::streamsize>(header_size)) throw std::runtime_error("Truncated DDS header: " + path.string());
        std::vector<std::byte> bytes(static_cast<size_t>(size));
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), size)) throw std::runtime_error("Failed to read DDS: " + path.string());
        auto field = [&](size_t offset) {
            uint32_t value;
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            return value;
        };
        constexpr uint32_t dds_magic = 0x20534444, fourcc_dx10 = 0x30315844, dxgi_rgba16_float = 10;
        if (field(0) != dds_magic || field(84) != fourcc_dx10 || field(128) != dxgi_rgba16_float)
            throw std::runtime_error("Expected DX10 RGBA16F DDS: " + path.string());
        const unsigned side = field(16), levels = std::max(1u, field(28));
        if ((field(112) & 0x200u) == 0 || field(12) != side || !std::has_single_bit(side) || levels > std::bit_width(side))
            throw std::runtime_error("Expected a power-of-two environment cubemap: " + path.string());

        const unsigned width = 6 * side, height = 2 * side;
        ocarina::vector<uint64_t> texels(size_t(width) * height);
        size_t offset = header_size;
        for (unsigned face = 0; face < 6; ++face) {
            for (unsigned mip = 0; mip < levels; ++mip) {
                const unsigned mip_side = side >> mip;
                const unsigned top = 2 * (side - mip_side);
                const size_t row_bytes = size_t(mip_side) * sizeof(uint64_t);
                for (unsigned y = 0; y < mip_side; ++y) {
                    if (row_bytes > bytes.size() - offset) throw std::runtime_error("Truncated environment cubemap");
                    std::memcpy(&texels[size_t(top + y) * width + face * mip_side], bytes.data() + offset, row_bytes);
                    offset += row_bytes;
                }
            }
        }
        env->face_size = side;
        env->levels = levels;
        env->atlas = device.create_texture2d(ocarina::make_uint2(width, height), ocarina::PixelStorage::HALF4);
        env->atlas.upload_immediately(texels.data());
    }

    // Trace the same indexed sphere at each of the 110 chart positions.
    ocarina::vector<ocarina::float3> positions;
    ocarina::vector<ocarina::uint3> triangles;
    constexpr int slices = 48, stacks = 24;
    positions.reserve((slices + 1) * (stacks + 1));
    triangles.reserve(2 * slices * stacks);
    for (int y = 0; y <= stacks; ++y)
    {
        const float v = static_cast<float>(y) / static_cast<float>(stacks);
        const float phi = v * glm::pi<float>();
        const float sin_phi = std::sin(phi);
        const float cos_phi = std::cos(phi);

        for (int x = 0; x <= slices; ++x)
        {
            const float u = static_cast<float>(x) / static_cast<float>(slices);
            const float theta = u * glm::two_pi<float>();
            const float sin_theta = std::sin(theta);
            const float cos_theta = std::cos(theta);

            positions.push_back(ocarina::make_float3(sin_phi * cos_theta, cos_phi, sin_phi * sin_theta));
        }
    }

    // Match the winding and primitive order of the chart's sphere mesh.
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            const unsigned top = y * (slices + 1) + x;
            const unsigned bottom = top + slices + 1;
            triangles.push_back(ocarina::make_uint3(top, bottom, top + 1));
            triangles.push_back(ocarina::make_uint3(top + 1, bottom, bottom + 1));
        }
    }
    auto vertices = device.create_buffer<ocarina::float3>(positions.size());
    auto indices = device.create_buffer<ocarina::uint3>(triangles.size());
    stream << vertices.upload(positions.data()) << indices.upload(triangles.data()) << ocarina::synchronize() << ocarina::commit();
    auto mesh = device.create_mesh(vertices.view(), indices.view(), ocarina::FAST_TRACE, ocarina::DISABLE_ANYHIT);
    stream << mesh.build_bvh() << ocarina::synchronize() << ocarina::commit();
    auto chart = device.create_accel(ocarina::FAST_TRACE);

    constexpr int chart_row_count = 10;
    constexpr int chart_col_count = 11;
    constexpr glm::vec3 chart_row_colors[chart_row_count] = {
        glm::vec3(1.0f, 1.0f, 1.0f),       // subsurface
        glm::vec3(1.0f, 0.86f, 0.12f),     // metallic
        glm::vec3(0.92f, 0.12f, 0.12f),    // specular
        glm::vec3(0.92f, 0.12f, 0.12f),    // specularTint
        glm::vec3(0.42f, 0.32f, 0.92f),    // roughness
        glm::vec3(0.92f, 0.18f, 0.78f),    // anisotropic
        glm::vec3(0.62f, 0.18f, 0.14f),    // sheen
        glm::vec3(0.62f, 0.18f, 0.14f),    // sheenTint
        glm::vec3(0.10f, 0.34f, 0.40f),    // clearcoat
        glm::vec3(0.10f, 0.34f, 0.40f),    // clearcoatGloss
    };
    ocarina::vector<ocarina::float4> material_data;
    for (int row = 0; row < chart_row_count; ++row) {
        for (int col = 0; col < chart_col_count; ++col) {
            constexpr float radius = 0.42f, spacing = 1.12f;
            const float x = (col - (chart_col_count - 1) * 0.5f) * spacing;
            const float y = ((chart_row_count - 1) * 0.5f - row) * spacing;
            const auto transform = ocarina::translation<ocarina::H>(x, y, 0.f) * ocarina::scale<ocarina::H>(radius);
            if (row == 0 && col == 0)
                chart.add_instance(ocarina::move(mesh), transform);
            else
                chart.add_instance(0u, transform);
            const float t = static_cast<float>(col) / (chart_col_count - 1);
            const auto color = chart_row_colors[row];
            auto mat0 = make_float4(color.x, color.y, color.z, 0.f); // base color, metallic
            auto mat1 = make_float4(.5f, .5f, 0.f, 0.f);            // roughness, specular, tint, subsurface
            auto mat2 = make_float4(0.f, 0.f, .5f, 0.f);            // anisotropy, sheen, sheen tint, clearcoat
            float gloss = 1.f;
            switch (row) {
            case 0: mat1.w = t; break;  // subsurface
            case 1: mat0.w = t; break;  // metallic
            case 2: mat1.y = t; break;  // specular
            case 3: mat1.z = t; break;  // specular tint
            case 4: mat1.x = t; break;  // roughness
            case 5: mat2.x = t; break;  // anisotropy
            case 6: mat2.y = t; break;  // sheen
            case 7: mat2.z = t; break;  // sheen tint
            case 8: mat2.w = t; break;  // clearcoat
            case 9: mat2.w = 1.f; gloss = t; break;  // clearcoat gloss
            }
            material_data.push_back(mat0);
            material_data.push_back(mat1);
            material_data.push_back(mat2);
            material_data.push_back(make_float4(x, y, radius, gloss));
        }
    }
    auto materials = device.create_buffer<ocarina::float4>(material_data.size());
    stream << materials.upload(material_data.data()) << chart.build_bvh() << ocarina::synchronize() << ocarina::commit();

    // Each invocation traces one camera ray and shades the surface it hits.
    ocarina::Kernel trace = [&](Float3 eye, Float3 light, ocarina::BufferVar<unsigned> output) {
        ocarina::Uint2 xy = ocarina::dispatch_idx().xy();
        ocarina::Uint2 extent = ocarina::dispatch_dim().xy();
        $if (xy.x >= extent.x || xy.y >= extent.y) { $return(); };
        auto pixel = xy.y * extent.x + xy.x;
        Float3 forward = normalize(-eye);
        Float3 right = normalize(cross(make_float3(0.f, 1.f, 0.f), forward));
        Float3 up = cross(forward, right);
        Float screen_x = (ocarina::cast<float>(xy.x) + .5f) / ocarina::cast<float>(extent.x) * 2.f - 1.f;
        Float screen_y = 1.f - (ocarina::cast<float>(xy.y) + .5f) / ocarina::cast<float>(extent.y) * 2.f;
        ocarina::Float aspect = ocarina::cast<float>(extent.x) / ocarina::cast<float>(extent.y);
        constexpr float half_fov = .41421356f; // tan(45 degrees / 2)
        auto ray = make_ray(eye, normalize(forward + half_fov * (right * screen_x * aspect + up * screen_y)));
        auto hit = chart.trace_closest(ray);
        Float3 color = make_float3(.5f);
        $if (hit->is_hit()) {
            ocarina::Uint3 tri = indices.read(hit->prim_id);
            Float3 local = hit->triangle_lerp(vertices.read(tri.x), vertices.read(tri.y), vertices.read(tri.z));
            ocarina::Uint start = 4u * hit->inst_id;
            Float4 m0 = materials.read(start), m1 = materials.read(start + 1u), m2 = materials.read(start + 2u);
            Float4 placement = materials.read(start + 3u);
            Float3 n = normalize(local), v = -ray->direction();
            Float3 base = pow(m0.xyz(), make_float3(2.2f));
            Float3 tint = base / max(dot(base, make_float3(.3f, .6f, .1f)), .0001f);
            Float3 spec = lerp(m0.w, m1.y * .08f * lerp(m1.z, make_float3(1.f), tint), base);
            Float3 direct = make_float3(0.f);
            $if ((dot(n, light) > 0.f) && (dot(n, v) > 0.f)) {
                // Disney diffuse, anisotropic reflection and clearcoat under the directional light.
                Float metallic = m0.w, roughness = max(.001f, m1.x), subsurface = m1.w;
                Float anisotropy = m2.x, sheen_strength = m2.y, sheen_tint = m2.z, clearcoat = m2.w;
                constexpr float pi = 3.14159265f;
                Float3 h = normalize(v + light);
                Float nl = max(dot(n, light), 0.f), nv = max(dot(n, v), 0.f), nh = max(dot(n, h), 0.f);
                Float lh = clamp(dot(light, h), 0.f, 1.f);
                Float fl = pow(1.f - nl, 5.f), fv = pow(1.f - nv, 5.f), fh = pow(1.f - lh, 5.f);
                Float fd90 = .5f + 2.f * lh * lh * roughness;
                Float fd = lerp(fl, 1.f, fd90) * lerp(fv, 1.f, fd90);
                Float fss90 = lh * lh * roughness;
                Float fss = lerp(fl, 1.f, fss90) * lerp(fv, 1.f, fss90);
                Float ss = 1.25f * (fss * (1.f / max(nl + nv, .0001f) - .5f) + .5f);
                Float3 sheen = fh * sheen_strength * lerp(sheen_tint, make_float3(1.f), tint);
                Float3 diffuse = (1.f - metallic) * (base / pi * lerp(subsurface, fd, ss) + sheen);

                Float3 tangent = cross(n, make_float3(0.f, 1.f, 0.f));
                $if (dot(tangent, tangent) < .0001f) {
                    tangent = cross(n, make_float3(1.f, 0.f, 0.f));
                };
                tangent = normalize(tangent);
                Float3 bitangent = cross(n, tangent);
                Float aspect = sqrt(1.f - .9f * anisotropy);
                Float ax = max(.001f, roughness * roughness / aspect), ay = max(.001f, roughness * roughness * aspect);
                Float q = sqr(dot(h, tangent) / ax) + sqr(dot(h, bitangent) / ay) + nh * nh;
                Float distribution = 1.f / (pi * ax * ay * q * q);
                auto smith = [&](Float3 w, Float nw) {
                    return 1.f / max(.0001f, nw + sqrt(sqr(dot(w, tangent) * ax) + sqr(dot(w, bitangent) * ay) + nw * nw));
                };
                Float3 reflection = lerp(fh, spec, make_float3(1.f)) * distribution * smith(v, nv) * smith(light, nl);

                Float a2 = sqr(lerp(placement.w, .1f, .001f));
                Float coat_d = (a2 - 1.f) / (pi * log(a2) * (1.f + (a2 - 1.f) * nh * nh));
                auto coat_g = [](Float nw) { return 1.f / (nw + sqrt(.0625f + .9375f * nw * nw)); };
                Float coat = .25f * clearcoat * coat_d * lerp(fh, .04f, 1.f) * coat_g(nl) * coat_g(nv);
                direct = 2.f * (diffuse + reflection + coat) * dot(n, light);
            };
            // Select a cube face and blend the two neighboring levels of its atlas.
            auto sample_environment = [](const Environment& env, Float3 d, Float mip) -> Float3 {
                Float3 axis = abs(d);
                Float major = max(axis.x, max(axis.y, axis.z));
                Float face;
                Float2 uv;
                $if (axis.x == major) {
                    face = select(d.x > 0.f, 0.f, 1.f);
                    uv = make_float2(select(d.x > 0.f, -d.z, d.z), -d.y);
                }
                $else {
                    $if (axis.y == major) {
                        face = select(d.y > 0.f, 2.f, 3.f);
                        uv = make_float2(d.x, select(d.y > 0.f, d.z, -d.z));
                    }
                    $else {
                        face = select(d.z > 0.f, 4.f, 5.f);
                        uv = make_float2(select(d.z > 0.f, d.x, -d.x), -d.y);
                    };
                };
                uv = .5f * (uv / major + 1.f);
                auto sample_level = [&](Float level) -> Float3 {
                    Float side = float(env.face_size) * exp2(-level);
                    Float2 xy = clamp(uv * side, make_float2(.5f), make_float2(side - .5f));
                    xy += make_float2(face * side, 2.f * (float(env.face_size) - side));
                    return env.atlas.sample(4, xy / make_float2(6.f * env.face_size, 2.f * env.face_size)).as_vec4().xyz();
                };
                Float level = clamp(mip, 0.f, float(env.levels - 1));
                Float lo = floor(level);
                Float3 first = sample_level(lo);
                Float3 second = sample_level(min(lo + 1.f, float(env.levels - 1)));
                return pow(abs(lerp(level - lo, first, second)), make_float3(2.2f));
            };
            Float mip = m1.x * 5.f;
            Float3 reflected = normalize(2.f * dot(n, v) * n - v);
            Float3 axis = abs(reflected);
            Float major = max(axis.x, max(axis.y, axis.z));
            Float scale = 1.f - exp2(mip) / 256.f;
            Float3 lookup = reflected * make_float3(select(axis.x == major, 1.f, scale),
                select(axis.y == major, 1.f, scale), select(axis.z == major, 1.f, scale));
            Float fresnel = pow(1.f - clamp(dot(n, v), 0.f, 1.f), 5.f);
            Float3 indirect = base * (1.f - m0.w) * sample_environment(irradiance, n, 0.f) +
                              spec * lerp(fresnel, spec.x, 1.f) * sample_environment(radiance, lookup, mip);
            Float3 lit = direct + indirect;
            Float3 x = max(lit - .004f, make_float3(0.f));
            color = x * (6.2f * x + .5f) / (x * (6.2f * x + 1.7f) + .06f);
        };
        Float3 bytes = clamp(color, 0.f, 1.f) * 255.f + .5f;
        output.write(pixel, ocarina::cast<unsigned>(bytes.x) | (ocarina::cast<unsigned>(bytes.y) << 8u) |
                            (ocarina::cast<unsigned>(bytes.z) << 16u) | 0xff000000u);
    };
    auto shader = device.compile(trace, mode);
    int width = 0, height = 0;
    ocarina::vector<unsigned> pixels;
    ocarina::Buffer<unsigned> output;
    HardwareDisplayer display(native_window, PresentDevicePreference::RenderDevice);

    const auto direction = glm::normalize(glm::vec3(-0.35f, 0.55f, -0.75f));
    const auto light = make_float3(direction.x, direction.y, direction.z);
    constexpr float yaw = glm::pi<float>(), pitch = 0.42f * glm::pi<float>();
    const auto eye = 13.5f * make_float3(std::sin(pitch) * std::sin(yaw), std::cos(pitch),
                                         std::sin(pitch) * std::cos(yaw));
    while (!glfwWindowShouldClose(window.get())) {
        glfwPollEvents();
        int w, h;
        glfwGetFramebufferSize(window.get(), &w, &h);
        if (w == 0 || h == 0) {
            if (!glfwWindowShouldClose(window.get())) glfwWaitEvents();
            continue;
        }
        if (w != width || h != height) {
            width = w;
            height = h;
            pixels.resize(static_cast<size_t>(width) * height);
            output = device.create_buffer<uint32_t>(pixels.size());
        }
        stream << shader(eye, light, output).dispatch(width, height)
               << output.download(pixels.data()) << ocarina::synchronize() << ocarina::commit();

        const bool shown = display.present(pixels, width, height, ImageFormat::RGBA8_UNORM);
        const bool frame_limit_reached = shown && remaining_frames > 0 && --remaining_frames == 0;
        if (frame_limit_reached || glfwWindowShouldClose(window.get())) {
            if (const char* path = std::getenv("HORIZON_CAPTURE_PATH"); path && *path) {
                const auto parent = std::filesystem::path(path).parent_path();
                if (!parent.empty()) std::filesystem::create_directories(parent);
                if (!stbi_write_png(path, width, height, 4, pixels.data(), width * 4))
                    throw std::runtime_error("Could not write ray-traced PNG");
            }
            break;
        }
    }
}
