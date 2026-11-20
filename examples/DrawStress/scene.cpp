// 移植自 bgfx examples/17-drawstress，用于对照单线程提交（Num threads=1）的绘制开销。
// 保留 8 顶点立方体、0.25 缩放、0.6 间距和逐立方体的旋转，每个立方体使用独立的间接绘制命令和 MVP。
#include "scene.h"
#include "Horizon.h"
#include "horizon_profiling.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <span>
#include <vector>

namespace drawstress {
namespace {
// The buffer supplies the vertex count. Both shaders decode the indexed corner directly.
const std::vector<uint32_t> cube_vertices = { 0, 1, 2, 3, 4, 5, 6, 7 };

const std::vector<uint16_t> cube_indices = {
    0, 1, 2, 1, 3, 2,
    4, 6, 5, 5, 6, 7,
    0, 2, 4, 4, 2, 6,
    1, 5, 3, 5, 7, 3,
    0, 4, 1, 4, 5, 1,
    2, 3, 6, 6, 3, 7,
};

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
        return;

    int& dim = *static_cast<int*>(glfwGetWindowUserPointer(window));
    switch (key)
    {
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:
        dim = std::min(dim + 2, dim_max);
        break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
        dim = std::max(dim - 2, dim_min);
        break;
    case GLFW_KEY_ESCAPE:
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        break;
    default:
        break;
    }
}

} // namespace

void attach_input(GLFWwindow* window, int& dim, const char* mode) {
    glfwSetWindowUserPointer(window, &dim);
    glfwSetKeyCallback(window, key_callback);
    std::cout << "== " << mode << " controls ==\n"
              << "  + / - : dim +2 / -2 (cubes = dim^3)\n"
              << "  Esc   : quit\n";
}

std::span<const uint16_t> cube_triangle_indices() { return cube_indices; }

RasterGeometry make_cube() {
    auto cube_vb = HardwareBuffer::uploadDeviceLocal(cube_vertices, BufferUsage::VertexBuffer);
    auto cube_ib = HardwareBuffer::uploadDeviceLocal(cube_indices, BufferUsage::IndexBuffer);
    return {cube_vb, cube_ib};
}

HardwareBuffer make_draw_packets() {
    constexpr uint32_t maxDraws = dim_max * dim_max * dim_max;
    std::vector<RasterIndirectPacket> commands(maxDraws);
    for (uint32_t i = 0; i < maxDraws; ++i) {
        commands[i].vertexCount = static_cast<uint32_t>(cube_indices.size());
        commands[i].instanceCount = 1;
        commands[i].parameterWord = i * (sizeof(glm::mat4) / sizeof(uint32_t));
    }
    return HardwareBuffer::uploadDeviceLocal(commands, BufferUsage::RasterIndirect);
}

glm::mat4 make_view_projection() {
    constexpr float aspect = static_cast<float>(stress_width) / static_cast<float>(stress_height);
    // bgfx 左手系：mtxLookAt (0,0,-35)->(0,0,0)、mtxProj fovy 60°
    const glm::mat4 view = glm::lookAtLH(glm::vec3(0.0f, 0.0f, -35.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = [] {
        glm::mat4 m = glm::perspectiveLH(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        m[1][1] *= -1.0f; // Vulkan 裁剪空间 Y 翻转
        return m;
    }();
    return proj * view;
}

void write_matrices(std::span<std::byte> parameters, const glm::mat4& view_proj, int dim, float time) {
    HORIZON_PROFILE_SCOPE_N("drawstress::build_records");
    const float step = 0.6f;
    const glm::vec3 base(-step * dim / 2.0f, -step * dim / 2.0f, -15.0f);

    // The fixed view faces +Z. Clip X and Y scale independently, and Z supplies both clip Z and W.
    const glm::vec4 projection(view_proj[0].x, view_proj[1].y, view_proj[2].z, view_proj[2].w);
    const auto projectAxis = [&](float x, float y, float z) {
        return projection * (0.25f * glm::vec4(x, y, z, z)) + view_proj[3] * 0.0f;
    };

    // Each axis has only dim distinct angles. Keep GLM's eulerAngleZYX product order.
    std::array<glm::vec3, dim_max> sine, cosine;
    std::array<glm::vec4, dim_max> translations;
    for (int i = 0; i < dim; ++i) {
        const glm::vec3 angles = time + float(i) * glm::vec3(0.21f, 0.37f, 0.13f);
        sine[i] = glm::sin(angles);
        cosine[i] = glm::cos(angles);
        translations[i] = view_proj * glm::vec4(base + glm::vec3(i * step), 1.0f);
    }
    auto* dst = parameters.data();
    for (int zz = 0; zz < dim; ++zz) {
        const float sz = sine[zz].z, cz = cosine[zz].z;
        for (int yy = 0; yy < dim; ++yy)
        {
            const float sy = sine[yy].y, cy = cosine[yy].y;
            const glm::vec4 axisX = projectAxis(cz * cy, cy * sz, -sy);
            glm::vec4 translation = translations[zz];
            translation.y = translations[yy].y;
            for (int xx = 0; xx < dim; ++xx)
            {
                const float sx = sine[xx].x, cx = cosine[xx].x;
                translation.x = translations[xx].x;
                const glm::mat4 matrix(axisX,
                    projectAxis(cz * sy * sx - cx * sz, cz * cx + sz * sy * sx, cy * sx),
                    projectAxis(sz * sx + cz * cx * sy, cx * sz * sy - cz * sx, cy * cx),
                    translation);
                std::memcpy(dst, &matrix, sizeof(matrix));
                dst += sizeof(matrix);
            }
        }
    }
}

} // namespace drawstress
