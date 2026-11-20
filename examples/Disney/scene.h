#pragma once

#include "Horizon.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

struct GLFWwindow;

namespace disney {
constexpr uint32_t disney_width = 1920;
constexpr uint32_t disney_height = 1080;
constexpr int chart_row_count = 10;
constexpr int chart_col_count = 11;
constexpr int chart_sphere_count = chart_row_count * chart_col_count;

struct DisneyDrawParameters {
    glm::mat4 model;
    glm::vec4 mat0, mat1, mat2;
    float clearcoatGloss;
};
static_assert(offsetof(DisneyDrawParameters, clearcoatGloss) == 28 * sizeof(uint32_t));
static_assert(sizeof(DisneyDrawParameters) == 29 * sizeof(uint32_t));

struct DisneyMaterial
{
    glm::vec3 base_color { 0.82f, 0.67f, 0.16f };
    float metallic = 0.0f;
    float roughness = 0.5f;
    float specular = 0.5f;
    float specular_tint = 0.0f;
    float subsurface = 0.0f;
    float anisotropic = 0.0f;
    float sheen = 0.0f;
    float sheen_tint = 0.5f;
    float clearcoat = 0.0f;
    float clearcoat_gloss = 1.0f;
};

struct Settings
{
    float exposure = 0.0f;
    float env_rot_curr = 0.0f;
    float env_rot_dest = 0.0f;
    bool do_diffuse = true;
    bool do_specular = true;
    bool do_diffuse_ibl = true;
    bool do_specular_ibl = true;
    glm::vec3 light_dir { -0.35f, 0.55f, -0.75f };
    glm::vec3 light_col { 1.0f, 1.0f, 1.0f };
};

struct OrbitCamera
{
    float yaw_curr = glm::pi<float>();
    float yaw_dest = glm::pi<float>();
    float pitch_curr = 0.42f * glm::pi<float>();
    float pitch_dest = 0.42f * glm::pi<float>();
    float dist_curr = 13.5f;
    float dist_dest = 13.5f;
    glm::vec3 target { 0.0f };

    void orbit(float dx, float dy)
    {
        yaw_dest += dx * glm::two_pi<float>();
        pitch_dest = std::clamp(pitch_dest - dy * glm::pi<float>(), 0.15f * glm::pi<float>(),
                                0.85f * glm::pi<float>());
    }

    void dolly(float dz) { dist_dest = std::clamp(dist_dest + dist_dest * dz, 4.0f, 40.0f); }

    void update(float dt)
    {
        const float amount = std::min(dt / 0.12f, 1.0f);
        yaw_curr = glm::mix(yaw_curr, yaw_dest, amount);
        pitch_curr = glm::mix(pitch_curr, pitch_dest, amount);
        dist_curr = glm::mix(dist_curr, dist_dest, amount);
    }

    [[nodiscard]] glm::vec3 position() const
    {
        const glm::vec3 dir(std::sin(pitch_curr) * std::sin(yaw_curr), std::cos(pitch_curr),
                            std::sin(pitch_curr) * std::cos(yaw_curr));
        return target + dir * dist_curr;
    }

};

struct InputContext
{
    Settings settings;
    OrbitCamera camera;
    double prev_x = 0.0;
    double prev_y = 0.0;
    bool has_prev = false;
};

struct MaterialChart {
    MaterialChart();
    DisneyDrawParameters parameters(int row, int col) const;

    RasterGeometry sphere;
    RasterDraw range;
private:
    std::array<DisneyMaterial, chart_sphere_count> materials_;
};

struct FrameUniforms {
    glm::mat4 proj_view, env_rotation;
    glm::vec4 camera, flags, light_direction, light_color, exposure;
};

HardwareImage load_cubemap(const char* filename);
FrameUniforms update_frame(InputContext& input, float dt);
void attach_input(GLFWwindow* window, InputContext& input);
} // namespace disney
