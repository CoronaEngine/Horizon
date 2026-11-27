struct DisneyVaryings {
    float4 position : SV_Position;
    [[vk::location(0)]] float3 view : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
    [[vk::location(15)]] nointerpolation uint materialIndex : TEXCOORD15;
};

struct DisneyPbrShared {
    column_major float4x4 proj_view;
    float4 camPos;
    float4 lightDir;
    uint texCubeIndex;
    uint texCubeIrrIndex;
};

struct DisneyDraw {
    column_major float4x4 model;
    float4 mat0; // baseColor.rgb, metallic
    float4 mat1; // roughness, specular, specularTint, subsurface
    float4 mat2; // anisotropic, sheen, sheenTint, clearcoat
    float4 mat3; // clearcoat gloss in x; one draw occupies 128 bytes.
};

[[vk::binding(0, 3)]] ConstantBuffer<DisneyPbrShared> vsp;

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
            float4 world = mul(draws[drawID].model, float4(normal, 1.0));
            DisneyVaryings vertex;
            vertex.position = mul(vsp.proj_view, world);
            vertex.view = vsp.camPos.xyz - world.xyz;
            vertex.normal = mul(draws[drawID].model, float4(normal, 0.0)).xyz;
            vertex.materialIndex = drawID;
            vertices[2 * lane + row] = vertex;
        }
    }
    if (lane < 2 * quads)
        triangles[lane] = (lane & 1) == 0 ? uint3(lane, lane + 1, lane + 2) : uint3(lane + 1, lane, lane + 2);
}
