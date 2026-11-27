#include "../disney_scene.slangh"

struct SphereGrid { uint slices, stacks; };
[[vk::push_constant]] ConstantBuffer<SphereGrid> sphere;
[[vk::binding(0, 1)]] StructuredBuffer<DisneyDraw> draws;

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(32, 1, 1)]
void main(uint lane : SV_GroupIndex, uint3 group : SV_GroupID, uint drawID : SV_DrawIndex,
          out vertices DisneyVaryings vertices[34], out indices uint3 triangles[32]) {
    uint firstSlice = group.x * 16;
    uint quads = min(16u, sphere.slices - firstSlice);
    SetMeshOutputCounts(2 * (quads + 1), 2 * quads);
    if (lane <= quads) {
        const float pi = 3.141592653589793;
        float theta = 2.0 * pi * float(firstSlice + lane) / float(sphere.slices);
        for (uint row = 0; row < 2; ++row) {
            float phi = pi * float(group.y + row) / float(sphere.stacks);
            float3 normal = float3(sin(phi) * cos(theta), cos(phi), sin(phi) * sin(theta));
            vertices[2 * lane + row] = sphereVertex(normal, draws[drawID].model, drawID);
        }
    }
    if (lane < 2 * quads)
        triangles[lane] = (lane & 1) == 0 ? uint3(lane, lane + 1, lane + 2) : uint3(lane + 1, lane, lane + 2);
}
