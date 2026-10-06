// Ocarina Slang device math and warp intrinsic builtins.
// Replaces cuda_device_std.h / cuda_device_scalar.h / cuda_device_vector.h /
// cuda_device_matrix.h / cuda_device_builtin.h / cuda_vector_func.h.
// Slang already provides sin/cos/sqrt/etc natively — only wrappers and helpers here.

// ── Wave / warp intrinsic wrappers ────────────────────────────────────────────

uint4 oc_warp_active_bit_mask()       { return WaveActiveBallot(true); }
uint  oc_warp_active_count_bits()     { return WaveActiveCountBits(true); }
uint  oc_warp_prefix_count_bits()     { return WavePrefixCountBits(true); }
uint  oc_warp_lane_id()               { return WaveGetLaneIndex(); }
uint  oc_warp_size()                  { return WaveGetLaneCount(); }
uint  oc_warp_first_active_lane()     { return WaveReadLaneFirst(WaveGetLaneIndex()); }
bool  oc_warp_is_first_active_lane()  { return WaveIsFirstLane(); }
void  oc_synchronize_block()          { GroupMemoryBarrierWithGroupSync(); }

// ── Half conversion ───────────────────────────────────────────────────────────

float16_t oc_float2half(float x)     { return (float16_t)x; }
float     oc_half2float(float16_t x) { return (float)x; }

// ── Ray helpers ───────────────────────────────────────────────────────────────

RayDesc oc_make_ray(float3 origin, float tmin, float3 direction, float tmax) {
    RayDesc r;
    r.Origin    = origin;
    r.TMin      = tmin;
    r.Direction = direction;
    r.TMax      = tmax;
    return r;
}

// Offset ray origin to prevent self-intersection (Wächter/Binder 2019).
float3 oc_offset_ray_origin(float3 p, float3 n) {
    const float origin      = 1.0f / 32.0f;
    const float float_scale = 1.0f / 65536.0f;
    const float int_scale   = 256.0f;
    int3 of_i = int3(int_scale * n);
    float3 p_i = float3(
        asfloat(asint(p.x) + (p.x < 0 ? -of_i.x : of_i.x)),
        asfloat(asint(p.y) + (p.y < 0 ? -of_i.y : of_i.y)),
        asfloat(asint(p.z) + (p.z < 0 ? -of_i.z : of_i.z)));
    return float3(
        abs(p.x) < origin ? p.x + float_scale * n.x : p_i.x,
        abs(p.y) < origin ? p.y + float_scale * n.y : p_i.y,
        abs(p.z) < origin ? p.z + float_scale * n.z : p_i.z);
}

// ── Buffer helpers ────────────────────────────────────────────────────────────

bool oc_is_null_buffer<T>(OCBuffer<T> buf)  { return uint64_t(buf.handle) == 0; }
bool oc_is_null_texture(OCTextureDesc desc) { return desc.tex_index == 0xFFFFFFFFu; }

uint64_t oc_buffer_size<T>(OCBuffer<T> buf) { return buf.size / sizeof(T); }

T oc_buffer_read<T>(OCBuffer<T> buf, uint index) {
    T* ptr = (T*)((uint8_t*)buf.handle + buf.offset);
    return ptr[index];
}

void oc_buffer_write<T>(OCBuffer<T> buf, uint index, T val) {
    T* ptr = (T*)((uint8_t*)buf.handle + buf.offset);
    ptr[index] = val;
}

// ── Bindless buffer read / write ──────────────────────────────────────────────

T oc_bindless_array_buffer_read<T>(OCBindlessArrayDesc arr, uint slot, uint index) {
    OCBuffer<T>* slot_ptr = (OCBuffer<T>*)(arr.buffer_slot + slot * sizeof(OCBuffer<T>));
    OCBuffer<T>  buf      = *slot_ptr;
    T*           data     = (T*)((uint8_t*)buf.handle + buf.offset);
    return data[index];
}

void oc_bindless_array_buffer_write<T>(OCBindlessArrayDesc arr, uint slot, uint index, T val) {
    OCBuffer<T>* slot_ptr = (OCBuffer<T>*)(arr.buffer_slot + slot * sizeof(OCBuffer<T>));
    OCBuffer<T>  buf      = *slot_ptr;
    T*           data     = (T*)((uint8_t*)buf.handle + buf.offset);
    data[index] = val;
}

uint64_t oc_bindless_array_buffer_size<T>(OCBindlessArrayDesc arr, uint slot) {
    OCBuffer<T>* slot_ptr = (OCBuffer<T>*)(arr.buffer_slot + slot * sizeof(OCBuffer<T>));
    return (*slot_ptr).size / sizeof(T);
}

T oc_bindless_array_byte_buffer_read<T>(OCBindlessArrayDesc arr, uint slot, uint byte_offset) {
    OCBuffer<uint8_t>* slot_ptr = (OCBuffer<uint8_t>*)(arr.buffer_slot + slot * sizeof(OCBuffer<uint8_t>));
    OCBuffer<uint8_t>  buf      = *slot_ptr;
    T*                 data     = (T*)((uint8_t*)buf.handle + buf.offset + byte_offset);
    return *data;
}

void oc_bindless_array_byte_buffer_write<T>(OCBindlessArrayDesc arr, uint slot, uint byte_offset, T val) {
    OCBuffer<uint8_t>* slot_ptr = (OCBuffer<uint8_t>*)(arr.buffer_slot + slot * sizeof(OCBuffer<uint8_t>));
    OCBuffer<uint8_t>  buf      = *slot_ptr;
    T*                 data     = (T*)((uint8_t*)buf.handle + buf.offset + byte_offset);
    *data = val;
}

void oc_byte_buffer_write<T>(OCBuffer<uint8_t> buf, uint byte_offset, T val) {
    T* ptr = (T*)((uint8_t*)buf.handle + buf.offset + byte_offset);
    *ptr = val;
}

T oc_byte_buffer_read<T>(OCBuffer<uint8_t> buf, uint byte_offset) {
    T* ptr = (T*)((uint8_t*)buf.handle + buf.offset + byte_offset);
    return *ptr;
}

// ── Scalar/vector helpers not in Slang stdlib ─────────────────────────────────

bool  oc_none(bool  v) { return !v; }
bool  oc_none(bool2 v) { return !any(v); }
bool  oc_none(bool3 v) { return !any(v); }
bool  oc_none(bool4 v) { return !any(v); }

T  oc_sqr<T>(T x)           { return x * x; }
T  oc_copysign<T>(T mag, T sgn)      { return abs(mag) * sign(sgn); }
T  oc_exp10<T>(T x)          { return pow((T)10, x); }
T  oc_log10<T>(T x)          { return log2(x) * (T)0.30102999566; }
T  oc_length_squared<T>(T v) { return dot(v, v); }
T  oc_distance_squared<T>(T a, T b) { T d = a - b; return dot(d, d); }

// ocarina lerp / inverse_lerp are pbrt-style: the parameter comes first
// (CUDA oc_lerp(t, a, b) = a + t * (b - a)), whereas Slang lerp is (a, b, t).
float  oc_lerp(float t, float a, float b)    { return lerp(a, b, t); }
float2 oc_lerp(float2 t, float2 a, float2 b) { return lerp(a, b, t); }
float3 oc_lerp(float3 t, float3 a, float3 b) { return lerp(a, b, t); }
float4 oc_lerp(float4 t, float4 a, float4 b) { return lerp(a, b, t); }
float16_t            oc_lerp(float16_t t, float16_t a, float16_t b)                                  { return lerp(a, b, t); }
vector<float16_t, 2> oc_lerp(vector<float16_t, 2> t, vector<float16_t, 2> a, vector<float16_t, 2> b) { return lerp(a, b, t); }
vector<float16_t, 3> oc_lerp(vector<float16_t, 3> t, vector<float16_t, 3> a, vector<float16_t, 3> b) { return lerp(a, b, t); }
vector<float16_t, 4> oc_lerp(vector<float16_t, 4> t, vector<float16_t, 4> a, vector<float16_t, 4> b) { return lerp(a, b, t); }

float  oc_inverse_lerp(float x, float a, float b)    { return (x - a) / (b - a); }
float2 oc_inverse_lerp(float2 x, float2 a, float2 b) { return (x - a) / (b - a); }
float3 oc_inverse_lerp(float3 x, float3 a, float3 b) { return (x - a) / (b - a); }
float4 oc_inverse_lerp(float4 x, float4 a, float4 b) { return (x - a) / (b - a); }

// ocarina face_forward (CUDA semantics): 3-arg (n, i, n_ref) and 2-arg (v1, v2).
float3 oc_face_forward(float3 n, float3 i, float3 n_ref) { return dot(n_ref, i) < 0.0f ? n : -n; }
float3 oc_face_forward(float3 v1, float3 v2)             { return dot(v1, v2) > 0.0f ? v1 : -v1; }
vector<float16_t, 3> oc_face_forward(vector<float16_t, 3> n, vector<float16_t, 3> i, vector<float16_t, 3> n_ref) { return dot(n_ref, i) < float16_t(0) ? n : -n; }
vector<float16_t, 3> oc_face_forward(vector<float16_t, 3> v1, vector<float16_t, 3> v2) { return dot(v1, v2) > float16_t(0) ? v1 : -v1; }

// Slang has no unreachable intrinsic; reaching this is undefined behaviour anyway.
void oc_unreachable() {}

void oc_coordinate_system(float3 n, out float3 b1, out float3 b2) {
    float sign_n = n.z >= 0.0f ? 1.0f : -1.0f;
    float a = -1.0f / (sign_n + n.z);
    float b = n.x * n.y * a;
    b1 = float3(1.0f + sign_n * n.x * n.x * a, sign_n * b, -sign_n * n.x);
    b2 = float3(b, sign_n + n.y * n.y * a, -n.y);
}

void oc_make_normal_tangent(float3 n, out float3 b1, out float3 b2) {
    oc_coordinate_system(n, b1, b2);
}

// 3×3 matrix inverse via cofactors
float3x3 oc_inverse(float3x3 m) {
    float3x3 inv;
    float d = determinant(m);
    float inv_d = 1.0f / d;
    inv[0][0] =  (m[1][1]*m[2][2] - m[2][1]*m[1][2]) * inv_d;
    inv[0][1] = -(m[0][1]*m[2][2] - m[2][1]*m[0][2]) * inv_d;
    inv[0][2] =  (m[0][1]*m[1][2] - m[1][1]*m[0][2]) * inv_d;
    inv[1][0] = -(m[1][0]*m[2][2] - m[2][0]*m[1][2]) * inv_d;
    inv[1][1] =  (m[0][0]*m[2][2] - m[2][0]*m[0][2]) * inv_d;
    inv[1][2] = -(m[0][0]*m[1][2] - m[1][0]*m[0][2]) * inv_d;
    inv[2][0] =  (m[1][0]*m[2][1] - m[2][0]*m[1][1]) * inv_d;
    inv[2][1] = -(m[0][0]*m[2][1] - m[2][0]*m[0][1]) * inv_d;
    inv[2][2] =  (m[0][0]*m[1][1] - m[1][0]*m[0][1]) * inv_d;
    return inv;
}

float4x4 oc_inverse(float4x4 m) {
    // standard 4×4 cofactor expansion
    float s0 = m[0][0]*m[1][1] - m[1][0]*m[0][1];
    float s1 = m[0][0]*m[1][2] - m[1][0]*m[0][2];
    float s2 = m[0][0]*m[1][3] - m[1][0]*m[0][3];
    float s3 = m[0][1]*m[1][2] - m[1][1]*m[0][2];
    float s4 = m[0][1]*m[1][3] - m[1][1]*m[0][3];
    float s5 = m[0][2]*m[1][3] - m[1][2]*m[0][3];
    float c5 = m[2][2]*m[3][3] - m[3][2]*m[2][3];
    float c4 = m[2][1]*m[3][3] - m[3][1]*m[2][3];
    float c3 = m[2][1]*m[3][2] - m[3][1]*m[2][2];
    float c2 = m[2][0]*m[3][3] - m[3][0]*m[2][3];
    float c1 = m[2][0]*m[3][2] - m[3][0]*m[2][2];
    float c0 = m[2][0]*m[3][1] - m[3][0]*m[2][1];
    float inv_d = 1.0f / (s0*c5 - s1*c4 + s2*c3 + s3*c2 - s4*c1 + s5*c0);
    float4x4 inv;
    inv[0][0] = ( m[1][1]*c5 - m[1][2]*c4 + m[1][3]*c3) * inv_d;
    inv[0][1] = (-m[0][1]*c5 + m[0][2]*c4 - m[0][3]*c3) * inv_d;
    inv[0][2] = ( m[3][1]*s5 - m[3][2]*s4 + m[3][3]*s3) * inv_d;
    inv[0][3] = (-m[2][1]*s5 + m[2][2]*s4 - m[2][3]*s3) * inv_d;
    inv[1][0] = (-m[1][0]*c5 + m[1][2]*c2 - m[1][3]*c1) * inv_d;
    inv[1][1] = ( m[0][0]*c5 - m[0][2]*c2 + m[0][3]*c1) * inv_d;
    inv[1][2] = (-m[3][0]*s5 + m[3][2]*s2 - m[3][3]*s1) * inv_d;
    inv[1][3] = ( m[2][0]*s5 - m[2][2]*s2 + m[2][3]*s1) * inv_d;
    inv[2][0] = ( m[1][0]*c4 - m[1][1]*c2 + m[1][3]*c0) * inv_d;
    inv[2][1] = (-m[0][0]*c4 + m[0][1]*c2 - m[0][3]*c0) * inv_d;
    inv[2][2] = ( m[3][0]*s4 - m[3][1]*s2 + m[3][3]*s0) * inv_d;
    inv[2][3] = (-m[2][0]*s4 + m[2][1]*s2 - m[2][3]*s0) * inv_d;
    inv[3][0] = (-m[1][0]*c3 + m[1][1]*c1 - m[1][2]*c0) * inv_d;
    inv[3][1] = ( m[0][0]*c3 - m[0][1]*c1 + m[0][2]*c0) * inv_d;
    inv[3][2] = (-m[3][0]*s3 + m[3][1]*s1 - m[3][2]*s0) * inv_d;
    inv[3][3] = ( m[2][0]*s3 - m[2][1]*s1 + m[2][2]*s0) * inv_d;
    return inv;
}

// ── Atomic wrappers (returns old value, matching CUDA semantics) ──────────────

T oc_atomicAdd<T>(T* dest, T val) {
    T old; InterlockedAdd(*dest, val, old); return old;
}
T oc_atomicSub<T>(T* dest, T val) {
    T old; InterlockedAdd(*dest, (T)(-(int)val), old); return old;
}
T oc_atomicExch<T>(T* dest, T val) {
    T old; InterlockedExchange(*dest, val, old); return old;
}
T oc_atomicCAS<T>(T* dest, T compare, T val) {
    T old; InterlockedCompareExchange(*dest, compare, val, old); return old;
}

// ── Texture helpers (bindless descriptor-array indices) ───────────────────────

[[vk::binding(0, 0)]] Texture2D<float4>      g_bindless_tex2d[];
[[vk::binding(1, 0)]] RWTexture2D<float4>    g_bindless_surf2d[];
[[vk::binding(2, 0)]] Texture3D<float4>      g_bindless_tex3d[];
[[vk::binding(3, 0)]] RWTexture3D<float4>    g_bindless_surf3d[];
[[vk::binding(0, 1)]] SamplerState           g_samplers[16];

float4 oc_tex2d_sample<uint N>(OCTextureDesc desc, float2 uv) {
    return g_bindless_tex2d[desc.tex_index].Sample(g_samplers[N], uv);
}
float4 oc_tex2d_read(OCTextureDesc desc, uint2 coord) {
    return g_bindless_surf2d[desc.surf_index].Load(int3(coord, 0));
}
void oc_tex2d_write(OCTextureDesc desc, uint2 coord, float4 val) {
    g_bindless_surf2d[desc.surf_index][coord] = val;
}

float4 oc_tex3d_sample<uint N>(OCTextureDesc desc, float3 uvw) {
    return g_bindless_tex3d[desc.tex_index].Sample(g_samplers[N], uvw);
}
float4 oc_tex3d_read(OCTextureDesc desc, uint3 coord) {
    return g_bindless_surf3d[desc.surf_index].Load(int4(coord, 0));
}
void oc_tex3d_write(OCTextureDesc desc, uint3 coord, float4 val) {
    g_bindless_surf3d[desc.surf_index][coord] = val;
}

float4 oc_bindless_array_tex2d_sample<uint N>(OCBindlessArrayDesc arr, uint slot, float2 uv) {
    OCTextureDesc* slot_ptr = (OCTextureDesc*)(arr.tex2d_slot + slot * sizeof(OCTextureDesc));
    return oc_tex2d_sample<N>(*slot_ptr, uv);
}
float4 oc_bindless_array_tex3d_sample<uint N>(OCBindlessArrayDesc arr, uint slot, float3 uvw) {
    OCTextureDesc* slot_ptr = (OCTextureDesc*)(arr.tex3d_slot + slot * sizeof(OCTextureDesc));
    return oc_tex3d_sample<N>(*slot_ptr, uvw);
}

// ── Ray tracing helpers ───────────────────────────────────────────────────────

struct OcHitInfo { uint inst_id; uint prim_id; float2 bary; float t; };

bool oc_trace_occlusion(RaytracingAccelerationStructure accel, RayDesc ray) {
    return TraceRayInline(accel,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
        0xFF, ray).Proceed();
}

OcHitInfo oc_trace_closest(RaytracingAccelerationStructure accel, RayDesc ray) {
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(accel, RAY_FLAG_NONE, 0xFF, ray);
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
            q.CommitNonOpaqueTriangleHit();
    }
    OcHitInfo h;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        h.inst_id = q.CommittedInstanceID();
        h.prim_id = q.CommittedPrimitiveIndex();
        h.bary    = q.CommittedTriangleBarycentrics();
        h.t       = q.CommittedRayT();
    } else {
        h.inst_id = 0xFFFFFFFFu;
        h.prim_id = 0xFFFFFFFFu;
        h.bary    = float2(0, 0);
        h.t       = -1.0f;
    }
    return h;
}
