#pragma once

#include "ray_view.h"
#include "dsl/dsl.h"
#include "rhi/common.h"

namespace ray_example {
ocarina::Device open_device();

// Construct a perspective ray through the centre of this pixel.
inline ocarina::Var<ocarina::Ray> camera_ray(ocarina::Uint pixel, ocarina::Float3 eye, ocarina::Float3 forward,
                                             ocarina::Float3 right, ocarina::Float3 up) {
    using namespace ocarina;
    Float x = (cast<float>(pixel % ray_view::width) + .5f) / ray_view::width * 2.f - 1.f;
    Float y = 1.f - (cast<float>(pixel / ray_view::width) + .5f) / ray_view::height * 2.f;
    constexpr float half_fov = .41421356f; // tan(45 degrees / 2)
    return make_ray(
        eye, normalize(forward + half_fov * (right * x * (float(ray_view::width) / ray_view::height) + up * y)));
}

inline ocarina::Uint rgba8(ocarina::Float3 rgb) {
    using namespace ocarina;
    Float3 bytes = clamp(rgb, 0.f, 1.f) * 255.f + .5f;
    return cast<uint>(bytes.x) | (cast<uint>(bytes.y) << 8u) | (cast<uint>(bytes.z) << 16u) | 0xff000000u;
}
} // namespace ray_example
