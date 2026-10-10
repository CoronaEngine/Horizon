// Ocarina Slang device math and warp intrinsic builtins.
// Replaces cuda_device_std.h / cuda_device_scalar.h / cuda_device_vector.h /
// cuda_device_matrix.h / cuda_device_builtin.h / cuda_vector_func.h.
// Slang already provides sin/cos/sqrt/etc natively — only wrappers and helpers here.

// ── Wave / warp intrinsic wrappers ────────────────────────────────────────────

uint4 oc_warp_active_bit_mask(bool pred)       { return WaveActiveBallot(pred); }
uint  oc_warp_active_count_bits(bool pred)     { return WaveActiveCountBits(pred); }
uint  oc_warp_prefix_count_bits(bool pred)     { return WavePrefixCountBits(pred); }
uint  oc_warp_lane_id()               { return WaveGetLaneIndex(); }
uint  oc_warp_size()                  { return WaveGetLaneCount(); }
uint  oc_warp_first_active_lane()     { return WaveReadLaneFirst(WaveGetLaneIndex()); }
bool  oc_warp_is_first_active_lane()  { return WaveIsFirstLane(); }
void  oc_synchronize_block()          { AllMemoryBarrierWithGroupSync(); }

// ── Half conversion ───────────────────────────────────────────────────────────

float16_t oc_float2half(float x)     { return (float16_t)x; }
float     oc_half2float(float16_t x) { return (float)x; }

// ── Ray helpers ───────────────────────────────────────────────────────────────

Ray oc_make_ray(float3 origin, float3 direction, float tmax = 1e16f) {
    Ray r; r.org_min = float4(origin, 0); r.dir_max = float4(direction, tmax); return r;
}
RayDesc oc_native_ray(Ray r) {
    RayDesc d; d.Origin=r.org_min.xyz; d.TMin=r.org_min.w;
    d.Direction=r.dir_max.xyz; d.TMax=r.dir_max.w; return d;
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

bool oc_is_null_buffer<T>(OCBuffer<T> buf)  { return buf.handle == 0; }
bool oc_is_null_texture(OCTextureDesc desc) { return desc.tex_index == 0; }

uint64_t oc_buffer_size<T>(OCBuffer<T> buf) { return buf.size; }

T oc_buffer_read<T>(OCBuffer<T> buf, uint64_t index) {
    T* ptr = (T*)(buf.handle + uint64_t(buf.offset) * sizeof(T));
    return ptr[index];
}

void oc_buffer_write<T>(OCBuffer<T> buf, uint64_t index, T val) {
    T* ptr = (T*)(buf.handle + uint64_t(buf.offset) * sizeof(T));
    ptr[index] = val;
}

// ── Bindless buffer read / write ──────────────────────────────────────────────

uint64_t oc_bindless_buffer_address(OCBindlessArrayDesc arr, uint slot, uint64_t index, uint stride) {
    OCBuffer<uint8_t> buf = *((OCBuffer<uint8_t>*)(arr.buffer_slot + slot * 24u));
    return buf.handle + buf.offset + uint64_t(index) * stride;
}

T oc_bindless_array_buffer_read<T, uint Stride>(OCBindlessArrayDesc arr, uint slot, uint64_t index) {
    OCBuffer<T>* slot_ptr = (OCBuffer<T>*)(arr.buffer_slot + slot * sizeof(OCBuffer<T>));
    OCBuffer<T>  buf      = *slot_ptr;
    return *(T*)(buf.handle + buf.offset + uint64_t(index) * Stride);
}

void oc_bindless_array_buffer_write<T, uint Stride>(OCBindlessArrayDesc arr, uint slot, uint64_t index, T val) {
    OCBuffer<T>* slot_ptr = (OCBuffer<T>*)(arr.buffer_slot + slot * sizeof(OCBuffer<T>));
    OCBuffer<T>  buf      = *slot_ptr;
    *(T*)(buf.handle + buf.offset + uint64_t(index) * Stride) = val;
}

uint oc_bindless_array_buffer_size(OCBindlessArrayDesc arr, uint slot) {
    OCBuffer<uint8_t>* slot_ptr = (OCBuffer<uint8_t>*)(arr.buffer_slot + slot * 24u);
    return uint((*slot_ptr).size);
}

T oc_bindless_array_byte_buffer_read<T>(OCBindlessArrayDesc arr, uint slot, uint64_t byte_offset) {
    OCBuffer<uint8_t>* slot_ptr = (OCBuffer<uint8_t>*)(arr.buffer_slot + slot * sizeof(OCBuffer<uint8_t>));
    OCBuffer<uint8_t>  buf      = *slot_ptr;
    T*                 data     = (T*)(buf.handle + uint64_t(buf.offset) + byte_offset);
    return *data;
}

void oc_bindless_array_byte_buffer_write<T>(OCBindlessArrayDesc arr, uint slot, uint64_t byte_offset, T val) {
    OCBuffer<uint8_t>* slot_ptr = (OCBuffer<uint8_t>*)(arr.buffer_slot + slot * sizeof(OCBuffer<uint8_t>));
    OCBuffer<uint8_t>  buf      = *slot_ptr;
    T*                 data     = (T*)(buf.handle + uint64_t(buf.offset) + byte_offset);
    *data = val;
}

void oc_byte_buffer_write<T>(OCBuffer<uint8_t> buf, uint64_t byte_offset, T val) {
    T* ptr = (T*)(buf.handle + uint64_t(buf.offset) + byte_offset);
    *ptr = val;
}

T oc_byte_buffer_read<T>(OCBuffer<uint8_t> buf, uint64_t byte_offset) {
    T* ptr = (T*)(buf.handle + uint64_t(buf.offset) + byte_offset);
    return *ptr;
}

// ── Scalar/vector helpers not in Slang stdlib ─────────────────────────────────

bool  oc_none(bool  v) { return !v; }
bool  oc_none(bool2 v) { return !any(v); }
bool  oc_none(bool3 v) { return !any(v); }
bool  oc_none(bool4 v) { return !any(v); }

float oc_sqr(float x) { return x*x; }
float oc_copysign(float a, float b) { return bit_cast<float>((bit_cast<vector<uint, 1>>(float(a)) & 0x7fffffffu) | (bit_cast<vector<uint, 1>>(float(b)) & 0x80000000u)); }
float oc_exp10(float x) { return pow(float(10),x); }
float oc_length_squared(float x) { return dot(x,x); }
float oc_distance_squared(float a, float b) { return dot(a-b,a-b); }
vector<float, 2> oc_sqr(vector<float, 2> x) { return x*x; }
vector<float, 2> oc_copysign(vector<float, 2> a, vector<float, 2> b) { return bit_cast<vector<float, 2>>((bit_cast<vector<uint, 2>>(float2(a)) & 0x7fffffffu) | (bit_cast<vector<uint, 2>>(float2(b)) & 0x80000000u)); }
vector<float, 2> oc_exp10(vector<float, 2> x) { return pow(vector<float, 2>(10),x); }
float oc_length_squared(vector<float, 2> x) { return dot(x,x); }
float oc_distance_squared(vector<float, 2> a, vector<float, 2> b) { return dot(a-b,a-b); }
vector<float, 3> oc_sqr(vector<float, 3> x) { return x*x; }
vector<float, 3> oc_copysign(vector<float, 3> a, vector<float, 3> b) { return bit_cast<vector<float, 3>>((bit_cast<vector<uint, 3>>(float3(a)) & 0x7fffffffu) | (bit_cast<vector<uint, 3>>(float3(b)) & 0x80000000u)); }
vector<float, 3> oc_exp10(vector<float, 3> x) { return pow(vector<float, 3>(10),x); }
float oc_length_squared(vector<float, 3> x) { return dot(x,x); }
float oc_distance_squared(vector<float, 3> a, vector<float, 3> b) { return dot(a-b,a-b); }
vector<float, 4> oc_sqr(vector<float, 4> x) { return x*x; }
vector<float, 4> oc_copysign(vector<float, 4> a, vector<float, 4> b) { return bit_cast<vector<float, 4>>((bit_cast<vector<uint, 4>>(float4(a)) & 0x7fffffffu) | (bit_cast<vector<uint, 4>>(float4(b)) & 0x80000000u)); }
vector<float, 4> oc_exp10(vector<float, 4> x) { return pow(vector<float, 4>(10),x); }
float oc_length_squared(vector<float, 4> x) { return dot(x,x); }
float oc_distance_squared(vector<float, 4> a, vector<float, 4> b) { return dot(a-b,a-b); }
float16_t oc_sqr(float16_t x) { return x*x; }
float16_t oc_copysign(float16_t a, float16_t b) { return float16_t(oc_copysign(float(a), float(b))); }
float16_t oc_exp10(float16_t x) { return pow(float16_t(10),x); }
float16_t oc_length_squared(float16_t x) { return dot(x,x); }
float16_t oc_distance_squared(float16_t a, float16_t b) { return dot(a-b,a-b); }
vector<float16_t, 2> oc_sqr(vector<float16_t, 2> x) { return x*x; }
vector<float16_t, 2> oc_copysign(vector<float16_t, 2> a, vector<float16_t, 2> b) { return vector<float16_t, 2>(oc_copysign(float2(a), float2(b))); }
vector<float16_t, 2> oc_exp10(vector<float16_t, 2> x) { return pow(vector<float16_t, 2>(10),x); }
float16_t oc_length_squared(vector<float16_t, 2> x) { return dot(x,x); }
float16_t oc_distance_squared(vector<float16_t, 2> a, vector<float16_t, 2> b) { return dot(a-b,a-b); }
vector<float16_t, 3> oc_sqr(vector<float16_t, 3> x) { return x*x; }
vector<float16_t, 3> oc_copysign(vector<float16_t, 3> a, vector<float16_t, 3> b) { return vector<float16_t, 3>(oc_copysign(float3(a), float3(b))); }
vector<float16_t, 3> oc_exp10(vector<float16_t, 3> x) { return pow(vector<float16_t, 3>(10),x); }
float16_t oc_length_squared(vector<float16_t, 3> x) { return dot(x,x); }
float16_t oc_distance_squared(vector<float16_t, 3> a, vector<float16_t, 3> b) { return dot(a-b,a-b); }
vector<float16_t, 4> oc_sqr(vector<float16_t, 4> x) { return x*x; }
vector<float16_t, 4> oc_copysign(vector<float16_t, 4> a, vector<float16_t, 4> b) { return vector<float16_t, 4>(oc_copysign(float4(a), float4(b))); }
vector<float16_t, 4> oc_exp10(vector<float16_t, 4> x) { return pow(vector<float16_t, 4>(10),x); }
float16_t oc_length_squared(vector<float16_t, 4> x) { return dot(x,x); }
float16_t oc_distance_squared(vector<float16_t, 4> a, vector<float16_t, 4> b) { return dot(a-b,a-b); }
// Preserve integer arithmetic for integer square; converting to float loses low bits.
int oc_sqr(int x) { return x*x; }
vector<int, 2> oc_sqr(vector<int, 2> x) { return x*x; }
vector<int, 3> oc_sqr(vector<int, 3> x) { return x*x; }
vector<int, 4> oc_sqr(vector<int, 4> x) { return x*x; }
int[N] oc_sqr<uint N>(int x[N]) { int result[N]; for(uint i=0;i<N;++i) result[i]=oc_sqr(x[i]); return result; }
uint oc_sqr(uint x) { return x*x; }
vector<uint, 2> oc_sqr(vector<uint, 2> x) { return x*x; }
vector<uint, 3> oc_sqr(vector<uint, 3> x) { return x*x; }
vector<uint, 4> oc_sqr(vector<uint, 4> x) { return x*x; }
uint[N] oc_sqr<uint N>(uint x[N]) { uint result[N]; for(uint i=0;i<N;++i) result[i]=oc_sqr(x[i]); return result; }

// CUDA sign returns +1 for both signed zero values. CUDA round rounds ties away
// from zero, whereas the Slang native round operation uses ties-to-even.
float oc_round(float x) {
    float magnitude = abs(x);
    float integral = floor(magnitude);
    return oc_copysign(integral + (magnitude - integral >= 0.5f ? 1.0f : 0.0f), x);
}
float16_t oc_round(float16_t x) { return float16_t(oc_round(float(x))); }
int oc_sign(int x) { return x >= int(0) ? int(1) : int(-1); }
vector<int, 2> oc_sign(vector<int, 2> x) { return vector<int, 2>(oc_sign(x[0]), oc_sign(x[1])); }
vector<int, 3> oc_sign(vector<int, 3> x) { return vector<int, 3>(oc_sign(x[0]), oc_sign(x[1]), oc_sign(x[2])); }
vector<int, 4> oc_sign(vector<int, 4> x) { return vector<int, 4>(oc_sign(x[0]), oc_sign(x[1]), oc_sign(x[2]), oc_sign(x[3])); }
int[N] oc_sign<uint N>(int x[N]) { int result[N]; for(uint i=0;i<N;++i) result[i]=oc_sign(x[i]); return result; }
float oc_sign(float x) { return x >= float(0) ? float(1) : float(-1); }
vector<float, 2> oc_sign(vector<float, 2> x) { return vector<float, 2>(oc_sign(x[0]), oc_sign(x[1])); }
vector<float, 3> oc_sign(vector<float, 3> x) { return vector<float, 3>(oc_sign(x[0]), oc_sign(x[1]), oc_sign(x[2])); }
vector<float, 4> oc_sign(vector<float, 4> x) { return vector<float, 4>(oc_sign(x[0]), oc_sign(x[1]), oc_sign(x[2]), oc_sign(x[3])); }
float[N] oc_sign<uint N>(float x[N]) { float result[N]; for(uint i=0;i<N;++i) result[i]=oc_sign(x[i]); return result; }
float16_t oc_sign(float16_t x) { return x >= float16_t(0) ? float16_t(1) : float16_t(-1); }
vector<float16_t, 2> oc_sign(vector<float16_t, 2> x) { return vector<float16_t, 2>(oc_sign(x[0]), oc_sign(x[1])); }
vector<float16_t, 3> oc_sign(vector<float16_t, 3> x) { return vector<float16_t, 3>(oc_sign(x[0]), oc_sign(x[1]), oc_sign(x[2])); }
vector<float16_t, 4> oc_sign(vector<float16_t, 4> x) { return vector<float16_t, 4>(oc_sign(x[0]), oc_sign(x[1]), oc_sign(x[2]), oc_sign(x[3])); }
float16_t[N] oc_sign<uint N>(float16_t x[N]) { float16_t result[N]; for(uint i=0;i<N;++i) result[i]=oc_sign(x[i]); return result; }
vector<float, 2> oc_round(vector<float, 2> x) { return vector<float, 2>(oc_round(x[0]), oc_round(x[1])); }
vector<float, 3> oc_round(vector<float, 3> x) { return vector<float, 3>(oc_round(x[0]), oc_round(x[1]), oc_round(x[2])); }
vector<float, 4> oc_round(vector<float, 4> x) { return vector<float, 4>(oc_round(x[0]), oc_round(x[1]), oc_round(x[2]), oc_round(x[3])); }
float[N] oc_round<uint N>(float x[N]) { float result[N]; for(uint i=0;i<N;++i) result[i]=oc_round(x[i]); return result; }
vector<float16_t, 2> oc_round(vector<float16_t, 2> x) { return vector<float16_t, 2>(oc_round(x[0]), oc_round(x[1])); }
vector<float16_t, 3> oc_round(vector<float16_t, 3> x) { return vector<float16_t, 3>(oc_round(x[0]), oc_round(x[1]), oc_round(x[2])); }
vector<float16_t, 4> oc_round(vector<float16_t, 4> x) { return vector<float16_t, 4>(oc_round(x[0]), oc_round(x[1]), oc_round(x[2]), oc_round(x[3])); }
float16_t[N] oc_round<uint N>(float16_t x[N]) { float16_t result[N]; for(uint i=0;i<N;++i) result[i]=oc_round(x[i]); return result; }

uint oc_atomicAdd(inout uint dest, uint val) { uint old; InterlockedAdd(dest, val, old); return old; }
uint oc_atomicAdd(OCBuffer<uint> buf, uint64_t index, uint val) { return oc_atomicAdd(*(uint*)(buf.handle + uint64_t(index + buf.offset) * sizeof(uint)), val); }
uint oc_atomicAdd(OCBuffer<uint8_t> buf, uint64_t offset, uint val) { return oc_atomicAdd(*(uint*)(buf.handle + buf.offset + offset), val); }
uint oc_atomicSub(inout uint dest, uint val) { uint old; InterlockedAdd(dest, -val, old); return old; }
uint oc_atomicSub(OCBuffer<uint> buf, uint64_t index, uint val) { return oc_atomicSub(*(uint*)(buf.handle + uint64_t(index + buf.offset) * sizeof(uint)), val); }
uint oc_atomicSub(OCBuffer<uint8_t> buf, uint64_t offset, uint val) { return oc_atomicSub(*(uint*)(buf.handle + buf.offset + offset), val); }
uint oc_atomicExch(inout uint dest, uint val) { uint old; InterlockedExchange(dest, val, old); return old; }
uint oc_atomicExch(OCBuffer<uint> buf, uint64_t index, uint val) { return oc_atomicExch(*(uint*)(buf.handle + uint64_t(index + buf.offset) * sizeof(uint)), val); }
uint oc_atomicExch(OCBuffer<uint8_t> buf, uint64_t offset, uint val) { return oc_atomicExch(*(uint*)(buf.handle + buf.offset + offset), val); }
uint oc_atomicCAS(inout uint dest, uint compare, uint val) { uint old; InterlockedCompareExchange(dest, compare, val, old); return old; }
int oc_atomicAdd(inout int dest, int val) { int old; InterlockedAdd(dest, val, old); return old; }
int oc_atomicAdd(OCBuffer<int> buf, uint64_t index, int val) { return oc_atomicAdd(*(int*)(buf.handle + uint64_t(index + buf.offset) * sizeof(int)), val); }
int oc_atomicAdd(OCBuffer<uint8_t> buf, uint64_t offset, int val) { return oc_atomicAdd(*(int*)(buf.handle + buf.offset + offset), val); }
int oc_atomicSub(inout int dest, int val) { int old; InterlockedAdd(dest, -val, old); return old; }
int oc_atomicSub(OCBuffer<int> buf, uint64_t index, int val) { return oc_atomicSub(*(int*)(buf.handle + uint64_t(index + buf.offset) * sizeof(int)), val); }
int oc_atomicSub(OCBuffer<uint8_t> buf, uint64_t offset, int val) { return oc_atomicSub(*(int*)(buf.handle + buf.offset + offset), val); }
int oc_atomicExch(inout int dest, int val) { int old; InterlockedExchange(dest, val, old); return old; }
int oc_atomicExch(OCBuffer<int> buf, uint64_t index, int val) { return oc_atomicExch(*(int*)(buf.handle + uint64_t(index + buf.offset) * sizeof(int)), val); }
int oc_atomicExch(OCBuffer<uint8_t> buf, uint64_t offset, int val) { return oc_atomicExch(*(int*)(buf.handle + buf.offset + offset), val); }
int oc_atomicCAS(inout int dest, int compare, int val) { int old; InterlockedCompareExchange(dest, compare, val, old); return old; }
uint64_t oc_atomicAdd(inout uint64_t dest, uint64_t val) { uint64_t old; InterlockedAdd(dest, val, old); return old; }
uint64_t oc_atomicAdd(OCBuffer<uint64_t> buf, uint64_t index, uint64_t val) { return oc_atomicAdd(*(uint64_t*)(buf.handle + uint64_t(index + buf.offset) * sizeof(uint64_t)), val); }
uint64_t oc_atomicAdd(OCBuffer<uint8_t> buf, uint64_t offset, uint64_t val) { return oc_atomicAdd(*(uint64_t*)(buf.handle + buf.offset + offset), val); }
uint64_t oc_atomicSub(inout uint64_t dest, uint64_t val) { uint64_t old; InterlockedAdd(dest, -val, old); return old; }
uint64_t oc_atomicSub(OCBuffer<uint64_t> buf, uint64_t index, uint64_t val) { return oc_atomicSub(*(uint64_t*)(buf.handle + uint64_t(index + buf.offset) * sizeof(uint64_t)), val); }
uint64_t oc_atomicSub(OCBuffer<uint8_t> buf, uint64_t offset, uint64_t val) { return oc_atomicSub(*(uint64_t*)(buf.handle + buf.offset + offset), val); }
uint64_t oc_atomicExch(inout uint64_t dest, uint64_t val) { uint64_t old; InterlockedExchange(dest, val, old); return old; }
uint64_t oc_atomicExch(OCBuffer<uint64_t> buf, uint64_t index, uint64_t val) { return oc_atomicExch(*(uint64_t*)(buf.handle + uint64_t(index + buf.offset) * sizeof(uint64_t)), val); }
uint64_t oc_atomicExch(OCBuffer<uint8_t> buf, uint64_t offset, uint64_t val) { return oc_atomicExch(*(uint64_t*)(buf.handle + buf.offset + offset), val); }
uint64_t oc_atomicCAS(inout uint64_t dest, uint64_t compare, uint64_t val) { uint64_t old; InterlockedCompareExchange(dest, compare, val, old); return old; }
int64_t oc_atomicAdd(inout int64_t dest, int64_t val) { int64_t old; InterlockedAdd(dest, val, old); return old; }
int64_t oc_atomicAdd(OCBuffer<int64_t> buf, uint64_t index, int64_t val) { return oc_atomicAdd(*(int64_t*)(buf.handle + uint64_t(index + buf.offset) * sizeof(int64_t)), val); }
int64_t oc_atomicAdd(OCBuffer<uint8_t> buf, uint64_t offset, int64_t val) { return oc_atomicAdd(*(int64_t*)(buf.handle + buf.offset + offset), val); }
int64_t oc_atomicSub(inout int64_t dest, int64_t val) { int64_t old; InterlockedAdd(dest, -val, old); return old; }
int64_t oc_atomicSub(OCBuffer<int64_t> buf, uint64_t index, int64_t val) { return oc_atomicSub(*(int64_t*)(buf.handle + uint64_t(index + buf.offset) * sizeof(int64_t)), val); }
int64_t oc_atomicSub(OCBuffer<uint8_t> buf, uint64_t offset, int64_t val) { return oc_atomicSub(*(int64_t*)(buf.handle + buf.offset + offset), val); }
int64_t oc_atomicExch(inout int64_t dest, int64_t val) { int64_t old; InterlockedExchange(dest, val, old); return old; }
int64_t oc_atomicExch(OCBuffer<int64_t> buf, uint64_t index, int64_t val) { return oc_atomicExch(*(int64_t*)(buf.handle + uint64_t(index + buf.offset) * sizeof(int64_t)), val); }
int64_t oc_atomicExch(OCBuffer<uint8_t> buf, uint64_t offset, int64_t val) { return oc_atomicExch(*(int64_t*)(buf.handle + buf.offset + offset), val); }
int64_t oc_atomicCAS(inout int64_t dest, int64_t compare, int64_t val) { int64_t old; InterlockedCompareExchange(dest, compare, val, old); return old; }
float oc_atomicAdd(inout float dest, float val) {
    uint* bits = (uint*)&dest;
    uint expected;
    InterlockedCompareExchange(*bits, 0u, 0u, expected);
    for (;;) {
        uint old;
        InterlockedCompareExchange(*bits, expected, asuint(asfloat(expected) + val), old);
        if (old == expected) return asfloat(old);
        expected = old;
    }
}
float oc_atomicAdd(OCBuffer<float> buf, uint64_t index, float val) { return oc_atomicAdd(*(float*)(buf.handle + uint64_t(index + buf.offset) * sizeof(float)), val); }
float oc_atomicAdd(OCBuffer<uint8_t> buf, uint64_t offset, float val) { return oc_atomicAdd(*(float*)(buf.handle + buf.offset + offset), val); }
float oc_atomicSub(inout float dest, float val) { return oc_atomicAdd(dest, -val); }
float oc_atomicSub(OCBuffer<float> buf, uint64_t index, float val) { return oc_atomicSub(*(float*)(buf.handle + uint64_t(index + buf.offset) * sizeof(float)), val); }
float oc_atomicSub(OCBuffer<uint8_t> buf, uint64_t offset, float val) { return oc_atomicSub(*(float*)(buf.handle + buf.offset + offset), val); }
float oc_atomicExch(inout float dest, float val) { uint old; InterlockedExchange(*(uint*)&dest, asuint(val), old); return asfloat(old); }
float oc_atomicExch(OCBuffer<float> buf, uint64_t index, float val) { return oc_atomicExch(*(float*)(buf.handle + uint64_t(index + buf.offset) * sizeof(float)), val); }
float oc_atomicExch(OCBuffer<uint8_t> buf, uint64_t offset, float val) { return oc_atomicExch(*(float*)(buf.handle + buf.offset + offset), val); }

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

// ── Texture helpers (bindless descriptor-array indices) ───────────────────────

[[vk::binding(0, 0)]] Texture2D<float4>      g_bindless_tex2d[];
[[vk::binding(1, 0)]] RWTexture2D<float4>    g_bindless_surf2d[];
[[vk::binding(2, 0)]] Texture3D<float4>      g_bindless_tex3d[];
[[vk::binding(3, 0)]] RWTexture3D<float4>    g_bindless_surf3d[];
[[vk::binding(5, 0)]] RWTexture2D<uint4> g_bindless_uint_surf2d[];
[[vk::binding(6, 0)]] RWTexture3D<uint4> g_bindless_uint_surf3d[];
[[vk::binding(4, 0)]] SamplerState           g_samplers[16];

float[N] oc_tex2d_sample_float<uint N>(OCTextureDesc desc, float u, float v) {
    float4 value=g_bindless_tex2d[NonUniformResourceIndex(uint(desc.tex_index))].SampleLevel(g_samplers[0], float2(u,v), 0);
    float result[N]; for(uint i=0;i<N;++i) result[i]=value[i]; return result;
}

float[N] oc_bindless_array_tex2d_sample<uint N>(OCBindlessArrayDesc arr, uint slot, float u, float v) {
    uint index=*((uint*)(arr.tex2d_slot + slot*4u));
    OCTextureDesc desc={uint64_t(index),uint64_t(index),0,0};
    return oc_tex2d_sample_float<N>(desc, u,v);
}

float oc_tex2d_read_float(OCTextureDesc desc, uint x, uint y) {
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    return float(value.x);
}

void oc_tex2d_write(OCTextureDesc desc, float value, uint x, uint y) {
    float4 packed=float4(float(value),0,0,0);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

vector<float, 2> oc_tex2d_read_float2(OCTextureDesc desc, uint x, uint y) {
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    return vector<float, 2>(value.xy);
}

void oc_tex2d_write(OCTextureDesc desc, vector<float, 2> value, uint x, uint y) {
    float4 packed=float4(float2(value),0,0);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

vector<float, 4> oc_tex2d_read_float4(OCTextureDesc desc, uint x, uint y) {
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    return vector<float, 4>(value);
}

void oc_tex2d_write(OCTextureDesc desc, vector<float, 4> value, uint x, uint y) {
    float4 packed=float4(value);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

uint8_t oc_tex2d_read_uchar(OCTextureDesc desc, uint x, uint y) {
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return uint8_t(value.x);
}

void oc_tex2d_write(OCTextureDesc desc, uint8_t value, uint x, uint y) {
    float4 packed=float4(float(value),0,0,0);
    packed /= 255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

vector<uint8_t, 2> oc_tex2d_read_uchar2(OCTextureDesc desc, uint x, uint y) {
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return vector<uint8_t, 2>(value.xy);
}

void oc_tex2d_write(OCTextureDesc desc, vector<uint8_t, 2> value, uint x, uint y) {
    float4 packed=float4(float2(value),0,0);
    packed /= 255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

vector<uint8_t, 4> oc_tex2d_read_uchar4(OCTextureDesc desc, uint x, uint y) {
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return vector<uint8_t, 4>(value);
}

void oc_tex2d_write(OCTextureDesc desc, vector<uint8_t, 4> value, uint x, uint y) {
    float4 packed=float4(value);
    packed /= 255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}


float[N] oc_tex3d_sample_float<uint N>(OCTextureDesc desc, float u, float v, float w) {
    float4 value=g_bindless_tex3d[NonUniformResourceIndex(uint(desc.tex_index))].SampleLevel(g_samplers[0], float3(u,v,w), 0);
    float result[N]; for(uint i=0;i<N;++i) result[i]=value[i]; return result;
}

float[N] oc_bindless_array_tex3d_sample<uint N>(OCBindlessArrayDesc arr, uint slot, float u, float v, float w) {
    uint index=*((uint*)(arr.tex3d_slot + slot*4u));
    OCTextureDesc desc={uint64_t(index),uint64_t(index),0,0};
    return oc_tex3d_sample_float<N>(desc, u,v,w);
}

float oc_tex3d_read_float(OCTextureDesc desc, uint x, uint y, uint z) {
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    return float(value.x);
}

void oc_tex3d_write(OCTextureDesc desc, float value, uint x, uint y, uint z) {
    float4 packed=float4(float(value),0,0,0);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

vector<float, 2> oc_tex3d_read_float2(OCTextureDesc desc, uint x, uint y, uint z) {
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    return vector<float, 2>(value.xy);
}

void oc_tex3d_write(OCTextureDesc desc, vector<float, 2> value, uint x, uint y, uint z) {
    float4 packed=float4(float2(value),0,0);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

vector<float, 4> oc_tex3d_read_float4(OCTextureDesc desc, uint x, uint y, uint z) {
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    return vector<float, 4>(value);
}

void oc_tex3d_write(OCTextureDesc desc, vector<float, 4> value, uint x, uint y, uint z) {
    float4 packed=float4(value);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

uint8_t oc_tex3d_read_uchar(OCTextureDesc desc, uint x, uint y, uint z) {
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return uint8_t(value.x);
}

void oc_tex3d_write(OCTextureDesc desc, uint8_t value, uint x, uint y, uint z) {
    float4 packed=float4(float(value),0,0,0);
    packed /= 255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

vector<uint8_t, 2> oc_tex3d_read_uchar2(OCTextureDesc desc, uint x, uint y, uint z) {
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return vector<uint8_t, 2>(value.xy);
}

void oc_tex3d_write(OCTextureDesc desc, vector<uint8_t, 2> value, uint x, uint y, uint z) {
    float4 packed=float4(float2(value),0,0);
    packed /= 255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

vector<uint8_t, 4> oc_tex3d_read_uchar4(OCTextureDesc desc, uint x, uint y, uint z) {
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return vector<uint8_t, 4>(value);
}

void oc_tex3d_write(OCTextureDesc desc, vector<uint8_t, 4> value, uint x, uint y, uint z) {
    float4 packed=float4(value);
    packed /= 255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}


// ── Ray tracing helpers ───────────────────────────────────────────────────────

struct OcHitInfo { uint inst_id; uint prim_id; float2 bary; float t; };

bool oc_trace_occlusion(RaytracingAccelerationStructure accel, Ray ray) {
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(accel, RAY_FLAG_NONE, 0xff, oc_native_ray(ray));
    while(q.Proceed()) { if(q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) q.CommitNonOpaqueTriangleHit(); }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

TriangleHit oc_trace_closest(RaytracingAccelerationStructure accel, Ray ray) {
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(accel, RAY_FLAG_NONE, 0xFF, oc_native_ray(ray));
    while (q.Proceed()) {
        if (q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
            q.CommitNonOpaqueTriangleHit();
    }
    TriangleHit h;
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        h.inst_id = q.CommittedInstanceID();
        h.prim_id = q.CommittedPrimitiveIndex();
        float2 uv = q.CommittedTriangleBarycentrics();
        h.bary = float2(1.0f - uv.x - uv.y, uv.x);
    } else {
        h.inst_id = 0xFFFFFFFFu;
        h.prim_id = 0xFFFFFFFFu;
        h.bary    = float2(0, 0);
    }
    return h;
}

// GEMM DSL markers match the CUDA placeholder; gemm_commands emits arithmetic.
void oc_tensor_gemm<uint Id>() {}

uint oc_tex2d_read_uint(OCTextureDesc desc, uint x, uint y) { return g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)].x; }
void oc_tex2d_write(OCTextureDesc desc, uint value, uint x, uint y) { g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)] = uint4(value,0,0,0); }

uint2 oc_tex2d_read_uint2(OCTextureDesc desc, uint x, uint y) { return g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)].xy; }
void oc_tex2d_write(OCTextureDesc desc, uint2 value, uint x, uint y) { g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)] = uint4(value,0,0); }

uint4 oc_tex2d_read_uint4(OCTextureDesc desc, uint x, uint y) { return g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]; }
void oc_tex2d_write(OCTextureDesc desc, uint4 value, uint x, uint y) { g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)] = value; }

uint oc_tex3d_read_uint(OCTextureDesc desc, uint x, uint y, uint z) { return g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)].x; }
void oc_tex3d_write(OCTextureDesc desc, uint value, uint x, uint y, uint z) { g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)] = uint4(value,0,0,0); }

uint2 oc_tex3d_read_uint2(OCTextureDesc desc, uint x, uint y, uint z) { return g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)].xy; }
void oc_tex3d_write(OCTextureDesc desc, uint2 value, uint x, uint y, uint z) { g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)] = uint4(value,0,0); }

uint4 oc_tex3d_read_uint4(OCTextureDesc desc, uint x, uint y, uint z) { return g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]; }
void oc_tex3d_write(OCTextureDesc desc, uint4 value, uint x, uint y, uint z) { g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)] = value; }

float2x2 oc_inverse(float2x2 m) { return float2x2(m[1][1], -m[0][1], -m[1][0], m[0][0]) / determinant(m); }
matrix<float16_t,2,2> oc_inverse(matrix<float16_t,2,2> m) { return matrix<float16_t,2,2>(oc_inverse(float2x2(m))); }
matrix<float16_t,3,3> oc_inverse(matrix<float16_t,3,3> m) { return matrix<float16_t,3,3>(oc_inverse(float3x3(m))); }
matrix<float16_t,4,4> oc_inverse(matrix<float16_t,4,4> m) { return matrix<float16_t,4,4>(oc_inverse(float4x4(m))); }
float16_t oc_inverse_lerp(float16_t x, float16_t a, float16_t b) { return (x-a)/(b-a); }
vector<float16_t,2> oc_inverse_lerp(vector<float16_t,2> x, vector<float16_t,2> a, vector<float16_t,2> b) { return (x-a)/(b-a); }
vector<float16_t,3> oc_inverse_lerp(vector<float16_t,3> x, vector<float16_t,3> a, vector<float16_t,3> b) { return (x-a)/(b-a); }
vector<float16_t,4> oc_inverse_lerp(vector<float16_t,4> x, vector<float16_t,4> a, vector<float16_t,4> b) { return (x-a)/(b-a); }
