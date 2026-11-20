#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <array>
#include <vector>
#include <cstddef>
#include <cstdint>

struct RasterGeometry;
struct HardwareImage;
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
};

struct InputContext
{
    Settings settings;
    OrbitCamera camera;
    double prev_x = 0.0;
    double prev_y = 0.0;
    bool has_prev = false;
};

struct FrameUniforms {
    glm::mat4 proj_view, env_rotation;
    glm::vec4 camera, flags, light_direction, light_color, exposure;
};

struct CubeMapData
{
    uint32_t size = 0;
    uint32_t mip_count = 0;
    std::vector<std::byte> payload;
};

struct IblMesh
{
    // Each unit-sphere position also supplies its original object-space normal.
    std::vector<std::array<float, 3>> vertices;
    std::vector<uint16_t> indices;
};

CubeMapData read_cubemap(const char* filename);
IblMesh make_uv_sphere(int slices, int stacks);
RasterGeometry make_sphere();
DisneyDrawParameters sphere_parameters(int row, int col);
HardwareImage load_cubemap(const char* filename);
FrameUniforms update_frame(InputContext& input, float dt);
void attach_input(GLFWwindow* window, InputContext& input);
} // namespace disney
