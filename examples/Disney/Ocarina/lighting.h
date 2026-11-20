#pragma once

#include "Ocarina/rays.h"

namespace disney_rt {
// All six faces of each prefiltered DDS mip are stored side by side in a 2D texture.
struct Environment {
    ocarina::Texture2D atlas;
    unsigned face_size, levels;
};
Environment load_environment(ocarina::Device& device, const char* filename);
ocarina::Float3 sample_environment(const Environment& environment, ocarina::Float3 direction, ocarina::Float mip);
ocarina::Float3 direct_brdf(ocarina::Float3 normal, ocarina::Float3 view, ocarina::Float3 light,
                            ocarina::Float3 base, ocarina::Float3 specular_color, ocarina::Float4 mat0,
                            ocarina::Float4 mat1, ocarina::Float4 mat2, ocarina::Float gloss);
} // namespace disney_rt
