// Layout matches host BufferDesc (24 bytes), TextureDesc (24 bytes), and BindlessArrayDesc.
struct OCBuffer<T> {
    uint64_t handle;
    uint offset;
    uint padding;
    uint64_t size;
};
struct OCTextureDesc {
    uint64_t tex_index;
    uint64_t surf_index;
    uint pixel_storage;
    uint padding;
};
struct OCBindlessArrayDesc {
    uint64_t buffer_slot;
    uint64_t tex3d_slot;
    uint64_t tex2d_slot;
};
struct Ray { float4 org_min; float4 dir_max; };
struct TriangleHit { uint inst_id; uint prim_id; float2 bary; };
