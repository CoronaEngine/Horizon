// Ocarina Slang device math and warp intrinsic builtins.
// Slang already provides sin/cos/sqrt/etc natively — only wrappers and helpers here.

// ── Wave / warp intrinsic wrappers ────────────────────────────────────────────

// The public warp contract is DSL's 32 lanes, also on native 64/128-lane waves.
uint oc_warp_ballot_word(bool pred) { return WaveActiveBallot(pred)[WaveGetLaneIndex() / 32u]; }
uint4 oc_warp_active_bit_mask(bool pred) { return uint4(oc_warp_ballot_word(pred), 0, 0, 0); }
uint oc_warp_active_count_bits(bool pred) { return countbits(oc_warp_ballot_word(pred)); }
uint oc_warp_lane_id() { return WaveGetLaneIndex() % 32u; }
uint oc_warp_size() { return 32u; }
uint oc_warp_prefix_count_bits(bool pred) {
    uint preceding = (1u << oc_warp_lane_id()) - 1u;
    return countbits(oc_warp_ballot_word(pred) & preceding);
}
uint oc_warp_first_active_lane() { return firstbitlow(oc_warp_ballot_word(true)); }
bool oc_warp_is_first_active_lane() { return oc_warp_lane_id() == oc_warp_first_active_lane(); }
void  oc_synchronize_block()          { AllMemoryBarrierWithGroupSync(); }

// ── Half conversion ───────────────────────────────────────────────────────────

// Enforce binary16 round-to-nearest-even using float32 bits. Native half casts
// and a pack/unpack pair both retained float32 precision on the tested GPU.
// Integer rounding also preserves binary16 subnormals without relying on the
// implementation-dependent denormal handling of UnpackHalf2x16.
float oc_half_value(float value) {
    uint bits = asuint(value);
    uint sign = bits & 0x80000000u;
    uint magnitude = bits & 0x7fffffffu;
    if (magnitude >= 0x7f800000u) return value; // Infinity or NaN.
    if (magnitude >= 0x477ff000u) return asfloat(sign | 0x7f800000u); // >= 65520.
    if (magnitude < 0x38800000u) { // Below the minimum normal half, 2^-14.
        if (magnitude <= 0x33000000u) return asfloat(sign); // <= 2^-25, ties to zero.
        uint exponent = magnitude >> 23;
        uint significand = (magnitude & 0x7fffffu) | 0x800000u;
        uint shift = 126u - exponent; // 14..24 fractional bits in 2^-24 units.
        uint units = significand >> shift;
        uint remainder = significand & ((1u << shift) - 1u);
        uint halfway = 1u << (shift - 1u);
        if (remainder > halfway || (remainder == halfway && (units & 1u) != 0u)) ++units;
        return asfloat(sign | asuint(float(units) * 5.9604644775390625e-8f));
    }
    uint rounded = (magnitude + 0xfffu + ((magnitude >> 13) & 1u)) & 0xffffe000u;
    return asfloat(sign | rounded);
}
vector<float, N> oc_half_value<uint N>(vector<float, N> value) {
    vector<float, N> result;
    for (uint i = 0; i < N; ++i) result[i] = oc_half_value(value[i]);
    return result;
}
matrix<float, R, C> oc_half_value<uint R, uint C>(matrix<float, R, C> value) {
    matrix<float, R, C> result;
    for (uint r = 0; r < R; ++r)
        for (uint c = 0; c < C; ++c) result[r][c] = oc_half_value(value[r][c]);
    return result;
}
// Numeric OpFConvert may flush binary16 subnormals even after oc_half_value
// rounded them correctly. Transfer the half bit pattern instead, and decode
// through integer fields so the public conversion APIs preserve gradual underflow.
float16_t oc_float2half(float value) {
    uint bits = asuint(value);
    uint sign = (bits >> 16u) & 0x8000u;
    uint magnitude = bits & 0x7fffffffu;
    uint exponent = (bits >> 23u) & 0xffu;
    uint mantissa = bits & 0x7fffffu;
    uint encoded;
    // The usual finite normal case rounds the whole magnitude at once; an
    // exponent carry also produces the correct infinity at the upper boundary.
    if (magnitude >= 0x38800000u && magnitude < 0x47800000u) {
        uint rounded = magnitude + 0xfffu + ((magnitude >> 13u) & 1u);
        encoded = sign | ((rounded >> 13u) - 0x1c000u);
    } else if (exponent == 0xffu) {
        encoded = sign | 0x7c00u | (mantissa != 0u ? ((mantissa >> 13u) | 0x200u) : 0u);
    } else if (exponent >= 143u) {
        encoded = sign | 0x7c00u;
    } else {
        if (exponent < 102u) {
            encoded = sign;
        } else {
            uint shift = 126u - exponent;
            uint significand = mantissa | 0x800000u;
            uint truncated = significand >> shift;
            uint remainder = significand & ((1u << shift) - 1u);
            uint halfway = 1u << (shift - 1u);
            uint rounded = truncated + uint(remainder > halfway ||
                (remainder == halfway && (truncated & 1u) != 0u));
            encoded = sign | rounded;
        }
    }
    return bit_cast<float16_t>(uint16_t(encoded));
}
float oc_half2float(float16_t value) {
    uint bits = uint(bit_cast<uint16_t>(value));
    uint sign = (bits & 0x8000u) << 16u;
    uint exponent = (bits >> 10u) & 31u;
    uint mantissa = bits & 1023u;
    // Every binary16 subnormal is an exact, normal float32 value. Scaling its
    // integer mantissa by 2^-24 avoids normalization loops and FP16 denorm rules.
    uint subnormal = asuint(float(mantissa) * 5.9604644775390625e-8f);
    uint normal = ((exponent + 112u) << 23u) | (mantissa << 13u);
    uint special = 0x7f800000u | (mantissa << 13u);
    uint decoded = exponent == 0u ? subnormal : (exponent == 31u ? special : normal);
    return asfloat(sign | decoded);
}
vector<float16_t, N> oc_float2half<uint N>(vector<float, N> value) {
    vector<uint, N> bits = asuint(value);
    vector<uint, N> sign = (bits >> 16u) & 0x8000u;
    vector<uint, N> magnitude = bits & 0x7fffffffu;
    vector<uint, N> exponent = magnitude >> 23u;
    vector<uint, N> mantissa = magnitude & 0x7fffffu;
    vector<uint, N> normal = ((magnitude + 0xfffu + ((magnitude >> 13u) & 1u)) >> 13u) - 0x1c000u;
    // Clamp before shifting: even lanes selected away below must not evaluate
    // an out-of-range shift. The retained lanes use exactly 14..24 bits.
    vector<uint, N> shift = 126u - clamp(exponent, 102u, 112u);
    vector<uint, N> significand = mantissa | 0x800000u;
    vector<uint, N> truncated = significand >> shift;
    vector<uint, N> remainder = significand & ((1u << shift) - 1u);
    vector<uint, N> halfway = 1u << (shift - 1u);
    vector<uint, N> subnormal = truncated + vector<uint, N>(remainder > halfway ||
        (remainder == halfway && (truncated & 1u) != 0u));
    vector<uint, N> finite = select(exponent > 112u, normal, select(exponent < 102u, 0u, subnormal));
    vector<uint, N> special = 0x7c00u | select(mantissa != 0u, (mantissa >> 13u) | 0x200u, 0u);
    vector<uint, N> encoded = select(exponent == 255u, special, select(exponent >= 143u, 0x7c00u, finite));
    return bit_cast<vector<float16_t, N>>(vector<uint16_t, N>(sign | encoded));
}
matrix<float16_t, R, C> oc_float2half<uint R, uint C>(matrix<float, R, C> value) {
    matrix<float16_t, R, C> result;
    [ForceUnroll]
    for (uint r = 0u; r < R; ++r)
        result[r] = oc_float2half(value[r]);
    return result;
}
vector<float, N> oc_half2float<uint N>(vector<float16_t, N> value) {
    vector<uint, N> bits = vector<uint, N>(bit_cast<vector<uint16_t, N>>(value));
    vector<uint, N> sign = (bits & 0x8000u) << 16u;
    vector<uint, N> exponent = (bits >> 10u) & 31u;
    vector<uint, N> mantissa = bits & 1023u;
    vector<uint, N> subnormal = asuint(vector<float, N>(mantissa) * 5.9604644775390625e-8f);
    vector<uint, N> normal = ((exponent + 112u) << 23u) | (mantissa << 13u);
    vector<uint, N> special = 0x7f800000u | (mantissa << 13u);
    vector<uint, N> decoded = select(exponent == 0u, subnormal, select(exponent == 31u, special, normal));
    return asfloat(sign | decoded);
}
matrix<float, R, C> oc_half2float<uint R, uint C>(matrix<float16_t, R, C> value) {
    matrix<float, R, C> result;
    [ForceUnroll]
    for (uint r = 0u; r < R; ++r)
        result[r] = oc_half2float(value[r]);
    return result;
}

float oc_half_add(float a, float b) { return oc_half_value(a + b); }
float oc_half_sub(float a, float b) { return oc_half_value(a - b); }
float oc_half_mul(float a, float b) { return oc_half_value(a * b); }
// Every input is already a binary16 value, so its product is exact in float32.
// TwoSum retains the low bits discarded by the following addition. Correct an
// exact half midpoint before rounding, avoiding float32 -> binary16 double
// rounding while retaining DSL HFMA's single binary16 rounding.
float oc_half_fma(float a, float b, float c) {
    // Slang 2026.10 ignores local precise on SPIR-V. Explicit decorations
    // preserve TwoSum's operation order without changing other shader math.
    float2 sum_residual = spirv_asm {
        %product : $$float = OpFMul $a $b;
        OpDecorate %product NoContraction;
        %sum : $$float = OpFAdd %product $c;
        OpDecorate %sum NoContraction;
        %carried : $$float = OpFSub %sum %product;
        OpDecorate %carried NoContraction;
        %rounded_product : $$float = OpFSub %sum %carried;
        OpDecorate %rounded_product NoContraction;
        %error_product : $$float = OpFSub %product %rounded_product;
        OpDecorate %error_product NoContraction;
        %error_c : $$float = OpFSub $c %carried;
        OpDecorate %error_c NoContraction;
        %residual : $$float = OpFAdd %error_product %error_c;
        OpDecorate %residual NoContraction;
        result : $$float2 = OpCompositeConstruct %sum %residual
    };
    float sum = sum_residual.x;
    float residual = sum_residual.y;
    uint bits = asuint(sum);
    uint magnitude = bits & 0x7fffffffu;
    if (magnitude >= 0x33000000u && magnitude <= 0x477ff000u) {
        uint shift = magnitude >= 0x38800000u ? 13u : 126u - (magnitude >> 23u);
        uint remainder = (magnitude | 0x800000u) & ((1u << shift) - 1u);
        if (remainder == (1u << (shift - 1u)) && residual != 0.0f) {
            bool increase_magnitude = (sum > 0.0f) == (residual > 0.0f);
            sum = asfloat(bits + (increase_magnitude ? 1u : 0xffffffffu));
        }
    }
    return oc_half_value(sum);
}
vector<float, N> oc_half_fma<uint N>(vector<float, N> a, vector<float, N> b, vector<float, N> c) {
    vector<float, N> result;
    for (uint i = 0u; i < N; ++i) result[i] = oc_half_fma(a[i], b[i], c[i]);
    return result;
}
// Round the second product of a minor, then fuse the first product and the
// subtraction with one final binary16 rounding.
float oc_half_minor(float a, float b, float c, float d) {
    return oc_half_fma(a, b, -oc_half_mul(c, d));
}

float oc_fmod(float a, float b) { return fmod(a, b); }
float oc_mod(float a, float b) { return a - b * floor(a / b); }
float16_t oc_fmod(float16_t a, float16_t b) {
    return oc_float2half(fmod(oc_half2float(a), oc_half2float(b)));
}
float16_t oc_mod(float16_t a, float16_t b) {
    float x = oc_half2float(a), y = oc_half2float(b);
    float quotient = oc_half_value(x / y);
    float integral = oc_half_value(floor(quotient));
    return oc_float2half(oc_half_sub(x, oc_half_mul(y, integral)));
}
vector<float, N> oc_fmod<uint N>(vector<float, N> a, vector<float, N> b) { return fmod(a, b); }
vector<float, N> oc_mod<uint N>(vector<float, N> a, vector<float, N> b) { return a - b * floor(a / b); }
vector<float16_t, N> oc_fmod<uint N>(vector<float16_t, N> a, vector<float16_t, N> b) {
    vector<float16_t, N> result;
    for (uint i = 0; i < N; ++i) result[i] = oc_fmod(a[i], b[i]);
    return result;
}
vector<float16_t, N> oc_mod<uint N>(vector<float16_t, N> a, vector<float16_t, N> b) {
    vector<float16_t, N> result;
    for (uint i = 0; i < N; ++i) result[i] = oc_mod(a[i], b[i]);
    return result;
}

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

// Preserve C++ remainder for negative divisors. The tested RTX 2080 path returned
// the dividend for OpSRem with a negative divisor. Signed division retains the
// required truncation toward zero; value arguments evaluate each operand once.
int oc_srem(int a, int b) { return a - (a / b) * b; }
int8_t oc_srem(int8_t a, int8_t b) { return int8_t(oc_srem(int(a), int(b))); }
int16_t oc_srem(int16_t a, int16_t b) { return int16_t(oc_srem(int(a), int(b))); }
int64_t oc_srem(int64_t a, int64_t b) { return a - (a / b) * b; }
vector<int, N> oc_srem<uint N>(vector<int, N> a, vector<int, N> b) { return a - (a / b) * b; }
vector<int8_t, N> oc_srem<uint N>(vector<int8_t, N> a, vector<int8_t, N> b) { return vector<int8_t, N>(oc_srem(vector<int, N>(a), vector<int, N>(b))); }
vector<int16_t, N> oc_srem<uint N>(vector<int16_t, N> a, vector<int16_t, N> b) { return vector<int16_t, N>(oc_srem(vector<int, N>(a), vector<int, N>(b))); }
vector<int64_t, N> oc_srem<uint N>(vector<int64_t, N> a, vector<int64_t, N> b) { return a - (a / b) * b; }

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

// DSL sign returns +1 for both signed zero values. DSL round rounds ties away
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
// (DSL oc_lerp(t, a, b) = a + t * (b - a)), whereas Slang lerp is (a, b, t).
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

// ocarina face_forward (DSL semantics): 3-arg (n, i, n_ref) and 2-arg (v1, v2).
float3 oc_face_forward(float3 n, float3 i, float3 n_ref) { return dot(n_ref, i) < 0.0f ? n : -n; }
float3 oc_face_forward(float3 v1, float3 v2)             { return dot(v1, v2) > 0.0f ? v1 : -v1; }
vector<float16_t, 3> oc_face_forward(vector<float16_t, 3> n, vector<float16_t, 3> i, vector<float16_t, 3> n_ref) { return dot(n_ref, i) < float16_t(0) ? n : -n; }
vector<float16_t, 3> oc_face_forward(vector<float16_t, 3> v1, vector<float16_t, 3> v2) { return dot(v1, v2) > float16_t(0) ? v1 : -v1; }

// Slang has no unreachable intrinsic; reaching this is undefined behaviour anyway.
void oc_unreachable() {}

void oc_coordinate_system(float3 n, out float3 b1, out float3 b2) {
    // Keep this orientation stable across compiler paths: rotating the frame
    // changes every world-space sample for a fixed seed.
    b1 = abs(n.x) > abs(n.y)
        ? float3(-n.z, 0.0f, n.x) / sqrt(n.x * n.x + n.z * n.z)
        : float3(0.0f, n.z, -n.y) / sqrt(n.y * n.y + n.z * n.z);
    b2 = cross(n, b1);
}

void oc_make_normal_tangent(float3 n, float3 tangent, out float3 b1, out float3 b2) {
    b2 = normalize(cross(n, tangent));
    b1 = cross(b2, n);
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

// ── Atomic wrappers (returns old value, matching DSL semantics) ──────────────

// ── Texture helpers (bindless descriptor-array indices) ───────────────────────

// Float and uint sampled views share the existing sampled-image descriptor
// array. Each access uses an index whose VkImageView numeric type matches it.
[allow("parameterBindingsOverlap")]
[[vk::binding(0, 0)]] Texture2D<float4>      g_bindless_tex2d[];
[allow("parameterBindingsOverlap")]
[[vk::binding(0, 0)]] Texture2D<uint4>       g_nrd_uint_tex2d[];
[[vk::binding(1, 0)]] RWTexture2D<float4>    g_bindless_surf2d[];
[[vk::binding(2, 0)]] Texture3D<float4>      g_bindless_tex3d[];
[[vk::binding(3, 0)]] RWTexture3D<float4>    g_bindless_surf3d[];
[[vk::binding(5, 0)]] globallycoherent RWTexture2D<uint4> g_bindless_uint_surf2d[];
[[vk::binding(6, 0)]] globallycoherent RWTexture3D<uint4> g_bindless_uint_surf3d[];
[[vk::binding(4, 0)]] SamplerState           g_samplers[16];
[[vk::binding(7, 0)]] globallycoherent RWStructuredBuffer<uint> g_texture_write_locks;

// The NRD conversion adapter uses float4-aligned addresses and five
// float4-sized SOA planes, matching NativeReblurPack.hlsl's RawBufferLoad(...,16).
// These helpers are private to that adapter; generic buffer reads stay unchanged.
float4 oc_nrd_byte_load_float4_aligned16(OCBuffer<uint8_t> buf, uint64_t byte_offset) {
    float4* ptr = (float4*)(buf.handle + uint64_t(buf.offset) + byte_offset);
    return spirv_asm {
        result : $$float4 = OpLoad $ptr Aligned 16
    };
}
uint4 oc_nrd_byte_load_uint4_aligned16(OCBuffer<uint8_t> buf, uint64_t byte_offset) {
    uint4* ptr = (uint4*)(buf.handle + uint64_t(buf.offset) + byte_offset);
    return spirv_asm {
        result : $$uint4 = OpLoad $ptr Aligned 16
    };
}
float4 oc_nrd_buffer_load_float4_aligned16(OCBuffer<float4> buf, uint64_t index) {
    // Typed BufferDesc offsets are in elements; ByteBuffer offsets are bytes.
    float4* ptr = (float4*)(buf.handle + (uint64_t(buf.offset) + index) * 16ull);
    return spirv_asm {
        result : $$float4 = OpLoad $ptr Aligned 16
    };
}

// NRD TA needs its normalized roughness mean rounded before squaring.
// Keep this one product opaque to Slang and separate from its consumers.
float oc_nrd_round_mul(float a, float b) {
    return spirv_asm {
        %rounded : $$float = OpFMul $a $b;
        OpDecorate %rounded NoContraction;
        result : $$float = OpCopyObject %rounded
    };
}

// Native REBLUR PrePass also rounds its reflect/mix additions separately.
float oc_nrd_round_add(float a, float b) {
    return spirv_asm {
        %rounded : $$float = OpFAdd $a $b;
        OpDecorate %rounded NoContraction;
        result : $$float = OpCopyObject %rounded
    };
}

// NRD uses the image view's native format conversion. Its Image wrapper owns
// bounds checks; samplers 1 and 2 supply linear and point clamp filtering.
// NRD Image descriptors come from fixed Dispatch parameter slots and are
// uniform across all invocations. Generic texture helpers below remain nonuniform.
float4 oc_nrd_tex2d_read(uint64_t image_index, int2 p) {
    return g_bindless_tex2d[uint(image_index)].Load(int3(p, 0));
}
uint4 oc_nrd_tex2d_read_uint(uint64_t image_index, int2 p) {
    return g_nrd_uint_tex2d[uint(image_index)].Load(int3(p, 0));
}
uint4 oc_nrd_tex2d_gather_uint(uint64_t image_index, float2 uv) {
    // Match NRD GatherRed(gNearestClamp): raw HLSL order is BL, BR, TR, TL.
    return g_nrd_uint_tex2d[uint(image_index)].GatherRed(g_samplers[2], uv);
}
void oc_nrd_tex2d_write(uint64_t image_index, int2 p, float4 value) {
    g_bindless_surf2d[uint(image_index)][p] = value;
}
void oc_nrd_tex2d_write_uint(uint64_t image_index, int2 p, uint4 value) {
    g_bindless_uint_surf2d[uint(image_index)][p] = value;
}
float4 oc_nrd_tex2d_sample(uint64_t image_index, float2 uv) {
    return g_bindless_tex2d[uint(image_index)].SampleLevel(g_samplers[1], uv, 0.f);
}
float4 oc_nrd_tex2d_sample_point(uint64_t image_index, float2 uv) {
    return g_bindless_tex2d[uint(image_index)].SampleLevel(g_samplers[2], uv, 0.f);
}
float4 oc_nrd_tex2d_gather_red(uint64_t image_index, float2 uv) {
    return g_bindless_tex2d[uint(image_index)].GatherRed(g_samplers[1], uv);
}
float4 oc_nrd_tex2d_gather_blue(uint64_t image_index, float2 uv) {
    return g_bindless_tex2d[uint(image_index)].GatherBlue(g_samplers[1], uv);
}
float4 oc_nrd_tex2d_gather(uint64_t image_index, float2 uv, uint component) {
    switch (component) {
        case 0u:
            return g_bindless_tex2d[uint(image_index)].GatherRed(g_samplers[1], uv);
        case 1u:
            return g_bindless_tex2d[uint(image_index)].GatherGreen(g_samplers[1], uv);
        case 2u:
            return g_bindless_tex2d[uint(image_index)].GatherBlue(g_samplers[1], uv);
        default:
            return g_bindless_tex2d[uint(image_index)].GatherAlpha(g_samplers[1], uv);
    }
}

// Match DSL's zero-border for surface loads and stores.
bool oc_tex2d_in_bounds(OCTextureDesc desc, uint x, uint y) {
    uint width, height;
    if ((desc.pixel_storage >= 3 && desc.pixel_storage <= 5) || desc.pixel_storage == 14u)
        g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))].GetDimensions(width, height);
    else
        g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))].GetDimensions(width, height);
    return x < width && y < height;
}

bool oc_tex3d_in_bounds(OCTextureDesc desc, uint x, uint y, uint z) {
    uint width, height, depth;
    if (desc.pixel_storage >= 3 && desc.pixel_storage <= 5)
        g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))].GetDimensions(width, height, depth);
    else
        g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))].GetDimensions(width, height, depth);
    return x < width && y < height && z < depth;
}

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
    if (!oc_tex2d_in_bounds(desc, x, y)) return float(0);
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    return float(value.x);
}

void oc_tex2d_write(OCTextureDesc desc, float value, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return;
    float4 packed=float4(float(value),0,0,0);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

vector<float, 2> oc_tex2d_read_float2(OCTextureDesc desc, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return vector<float, 2>(0);
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    return vector<float, 2>(value.xy);
}

void oc_tex2d_write(OCTextureDesc desc, vector<float, 2> value, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return;
    float4 packed=float4(float2(value),0,0);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

vector<float, 4> oc_tex2d_read_float4(OCTextureDesc desc, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return vector<float, 4>(0);
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    return vector<float, 4>(value);
}

void oc_tex2d_write(OCTextureDesc desc, vector<float, 4> value, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return;
    float4 packed=float4(value);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

uint8_t oc_tex2d_read_uchar(OCTextureDesc desc, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return uint8_t(0);
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return uint8_t(value.x);
}

void oc_tex2d_write(OCTextureDesc desc, uint8_t value, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return;
    float4 packed=float4(float(value),0,0,0);
    packed /= 255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

vector<uint8_t, 2> oc_tex2d_read_uchar2(OCTextureDesc desc, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return vector<uint8_t, 2>(0);
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return vector<uint8_t, 2>(value.xy);
}

void oc_tex2d_write(OCTextureDesc desc, vector<uint8_t, 2> value, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return;
    float4 packed=float4(float2(value),0,0);
    packed /= 255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}

vector<uint8_t, 4> oc_tex2d_read_uchar4(OCTextureDesc desc, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return vector<uint8_t, 4>(0);
    float4 value=g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return vector<uint8_t, 4>(value);
}

void oc_tex2d_write(OCTextureDesc desc, vector<uint8_t, 4> value, uint x, uint y) {
    if (!oc_tex2d_in_bounds(desc, x, y)) return;
    float4 packed=float4(value);
    packed /= 255.0f;
    g_bindless_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][uint2(x,y)]=packed;
}


float[N] oc_tex3d_sample_float<uint N>(OCTextureDesc desc, float u, float v, float w = 0.0f) {
    float4 value=g_bindless_tex3d[NonUniformResourceIndex(uint(desc.tex_index))].SampleLevel(g_samplers[0], float3(u,v,w), 0);
    float result[N]; for(uint i=0;i<N;++i) result[i]=value[i]; return result;
}

float[N] oc_bindless_array_tex3d_sample<uint N>(OCBindlessArrayDesc arr, uint slot, float u, float v, float w = 0.0f) {
    uint index=*((uint*)(arr.tex3d_slot + slot*4u));
    OCTextureDesc desc={uint64_t(index),uint64_t(index),0,0};
    return oc_tex3d_sample_float<N>(desc, u,v,w);
}

float oc_tex3d_read_float(OCTextureDesc desc, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return float(0);
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    return float(value.x);
}

void oc_tex3d_write(OCTextureDesc desc, float value, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return;
    float4 packed=float4(float(value),0,0,0);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

vector<float, 2> oc_tex3d_read_float2(OCTextureDesc desc, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return vector<float, 2>(0);
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    return vector<float, 2>(value.xy);
}

void oc_tex3d_write(OCTextureDesc desc, vector<float, 2> value, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return;
    float4 packed=float4(float2(value),0,0);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

vector<float, 4> oc_tex3d_read_float4(OCTextureDesc desc, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return vector<float, 4>(0);
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    return vector<float, 4>(value);
}

void oc_tex3d_write(OCTextureDesc desc, vector<float, 4> value, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return;
    float4 packed=float4(value);
    if(desc.pixel_storage<3) packed=trunc(clamp(packed,0.0f,1.0f)*255.0f)/255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

uint8_t oc_tex3d_read_uchar(OCTextureDesc desc, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return uint8_t(0);
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return uint8_t(value.x);
}

void oc_tex3d_write(OCTextureDesc desc, uint8_t value, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return;
    float4 packed=float4(float(value),0,0,0);
    packed /= 255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

vector<uint8_t, 2> oc_tex3d_read_uchar2(OCTextureDesc desc, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return vector<uint8_t, 2>(0);
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return vector<uint8_t, 2>(value.xy);
}

void oc_tex3d_write(OCTextureDesc desc, vector<uint8_t, 2> value, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return;
    float4 packed=float4(float2(value),0,0);
    packed /= 255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}

vector<uint8_t, 4> oc_tex3d_read_uchar4(OCTextureDesc desc, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return vector<uint8_t, 4>(0);
    float4 value=g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)];
    value = desc.pixel_storage < 3 ? round(value * 255.0f) : trunc(value * 255.0f);
    return vector<uint8_t, 4>(value);
}

void oc_tex3d_write(OCTextureDesc desc, vector<uint8_t, 4> value, uint x, uint y, uint z) {
    if (!oc_tex3d_in_bounds(desc, x, y, z)) return;
    float4 packed=float4(value);
    packed /= 255.0f;
    g_bindless_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][uint3(x,y,z)]=packed;
}


// ── Ray tracing helpers ───────────────────────────────────────────────────────

struct OcHitInfo { uint inst_id; uint prim_id; float2 bary; float t; };

bool oc_trace_occlusion(uint64_t handle, Ray ray) {
    if (handle == 0) return false;
    RaytracingAccelerationStructure accel = RaytracingAccelerationStructure(handle);
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    q.TraceRayInline(accel, RAY_FLAG_NONE, 1u, oc_native_ray(ray));
    while(q.Proceed()) { if(q.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE) q.CommitNonOpaqueTriangleHit(); }
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

TriangleHit oc_trace_closest(uint64_t handle, Ray ray) {
    if (handle == 0) {
        TriangleHit miss;
        miss.inst_id = 0xffffffffu;
        miss.prim_id = 0xffffffffu;
        miss.bary = float2(0, 0);
        return miss;
    }
    RaytracingAccelerationStructure accel = RaytracingAccelerationStructure(handle);
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(accel, RAY_FLAG_NONE, 1u, oc_native_ray(ray));
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

// GEMM markers are placeholders; gemm_commands emits the arithmetic.
void oc_tensor_gemm<uint Id>() {}

// DSL surface x is measured in sizeof(the requested type), not storage texels.
// Integer alias views expose each native channel's unconverted bits. BYTE
// channels are packed/unpacked in little-endian groups of four per uint.
uint oc_texture_write_lock(OCTextureDesc desc, uint3 p) {
    return (uint(desc.surf_index) * 0x9e3779b9u ^ p.x * 0x85ebca6bu ^
            p.y * 0xc2b2ae35u ^ p.z * 0x27d4eb2fu) & 65535u;
}

uint4 oc_tex2d_read_words<uint N>(OCTextureDesc desc, uint2 p) {
    uint width, height;
    g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))].GetDimensions(width, height);
    // USHORT1 is a native single-channel uint texel, not a legacy byte alias.
    if (desc.pixel_storage == 14u) {
        if (p.x >= width || p.y >= height) return uint4(0);
        return g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][p];
    }
    uint channels = 1u << (desc.pixel_storage % 3u);
    uint units = desc.pixel_storage < 3u ? 4u : 1u;
    if (p.x >= width * channels / (N * units) || p.y >= height) return uint4(0);
    if (units == 1u && N == channels) return g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][p];
    uint start = p.x * N * units;
    uint4 result = uint4(0);
    for (uint word = 0u; word < N; ++word) {
        for (uint byte = 0u; byte < units; ++byte) {
            uint component = start + word * units + byte;
            uint2 pixel = p;
            pixel.x = component / channels;
            uint4 loaded = g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][pixel];
            result[word] |= loaded[component % channels] << (byte * 8u);
        }
    }
    return result;
}

void oc_tex2d_write_words<uint N>(OCTextureDesc desc, uint4 value, uint2 p) {
    uint width, height;
    g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))].GetDimensions(width, height);
    if (desc.pixel_storage == 14u) {
        if (p.x >= width || p.y >= height) return;
        g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][p] = value;
        return;
    }
    uint channels = 1u << (desc.pixel_storage % 3u);
    uint units = desc.pixel_storage < 3u ? 4u : 1u;
    if (p.x >= width * channels / (N * units) || p.y >= height) return;
    uint start = p.x * N * units;
    if (units == 1u && N < channels) {
        uint2 pixel = p;
        pixel.x = start / channels;
        uint lock = oc_texture_write_lock(desc, uint3(pixel, 0u));
        bool stored = false;
        [loop] while (!stored) {
            uint previous;
            InterlockedCompareExchange(g_texture_write_locks[lock], 0u, 1u, previous);
            if (previous == 0u) {
                // Complete and release inside the successful branch, before
                // reconverging with lanes that are still trying to acquire.
                DeviceMemoryBarrier();
                uint4 packed = g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][pixel];
                for (uint c = 0u; c < N; ++c) packed[start % channels + c] = value[c];
                g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][pixel] = packed;
                DeviceMemoryBarrier();
                InterlockedExchange(g_texture_write_locks[lock], 0u);
                stored = true;
            }
        }
        return;
    }
    // This access covers whole texels; no read/modify/write or lock is needed.
    for (uint texel = 0u; texel < N * units / channels; ++texel) {
        uint4 packed = uint4(0);
        for (uint c = 0u; c < channels; ++c) {
            uint component = texel * channels + c;
            packed[c] = value[component / units] >> ((component % units) * 8u);
            if (units == 4u) packed[c] &= 255u;
        }
        uint2 pixel = p;
        pixel.x = start / channels + texel;
        g_bindless_uint_surf2d[NonUniformResourceIndex(uint(desc.surf_index))][pixel] = packed;
    }
}
uint oc_tex2d_read_uint(OCTextureDesc desc, uint x, uint y) { return oc_tex2d_read_words<1>(desc, uint2(x,y)).x; }
void oc_tex2d_write(OCTextureDesc desc, uint value, uint x, uint y) { oc_tex2d_write_words<1>(desc, uint4(value,0,0,0), uint2(x,y)); }
uint2 oc_tex2d_read_uint2(OCTextureDesc desc, uint x, uint y) { return oc_tex2d_read_words<2>(desc, uint2(x,y)).xy; }
void oc_tex2d_write(OCTextureDesc desc, uint2 value, uint x, uint y) { oc_tex2d_write_words<2>(desc, uint4(value,0,0), uint2(x,y)); }
uint4 oc_tex2d_read_uint4(OCTextureDesc desc, uint x, uint y) { return oc_tex2d_read_words<4>(desc, uint2(x,y)); }
void oc_tex2d_write(OCTextureDesc desc, uint4 value, uint x, uint y) { oc_tex2d_write_words<4>(desc, value, uint2(x,y)); }

uint4 oc_tex3d_read_words<uint N>(OCTextureDesc desc, uint3 p) {
    uint width, height, depth;
    g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))].GetDimensions(width, height, depth);
    uint channels = 1u << (desc.pixel_storage % 3u);
    uint units = desc.pixel_storage < 3u ? 4u : 1u;
    if (p.x >= width * channels / (N * units) || p.y >= height || p.z >= depth) return uint4(0);
    if (units == 1u && N == channels) return g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][p];
    uint start = p.x * N * units;
    uint4 result = uint4(0);
    for (uint word = 0u; word < N; ++word) {
        for (uint byte = 0u; byte < units; ++byte) {
            uint component = start + word * units + byte;
            uint3 pixel = p;
            pixel.x = component / channels;
            uint4 loaded = g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][pixel];
            result[word] |= loaded[component % channels] << (byte * 8u);
        }
    }
    return result;
}

void oc_tex3d_write_words<uint N>(OCTextureDesc desc, uint4 value, uint3 p) {
    uint width, height, depth;
    g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))].GetDimensions(width, height, depth);
    uint channels = 1u << (desc.pixel_storage % 3u);
    uint units = desc.pixel_storage < 3u ? 4u : 1u;
    if (p.x >= width * channels / (N * units) || p.y >= height || p.z >= depth) return;
    uint start = p.x * N * units;
    if (units == 1u && N < channels) {
        uint3 pixel = p;
        pixel.x = start / channels;
        uint lock = oc_texture_write_lock(desc, pixel);
        bool stored = false;
        [loop] while (!stored) {
            uint previous;
            InterlockedCompareExchange(g_texture_write_locks[lock], 0u, 1u, previous);
            if (previous == 0u) {
                // Complete and release inside the successful branch, before
                // reconverging with lanes that are still trying to acquire.
                DeviceMemoryBarrier();
                uint4 packed = g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][pixel];
                for (uint c = 0u; c < N; ++c) packed[start % channels + c] = value[c];
                g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][pixel] = packed;
                DeviceMemoryBarrier();
                InterlockedExchange(g_texture_write_locks[lock], 0u);
                stored = true;
            }
        }
        return;
    }
    // This access covers whole texels; no read/modify/write or lock is needed.
    for (uint texel = 0u; texel < N * units / channels; ++texel) {
        uint4 packed = uint4(0);
        for (uint c = 0u; c < channels; ++c) {
            uint component = texel * channels + c;
            packed[c] = value[component / units] >> ((component % units) * 8u);
            if (units == 4u) packed[c] &= 255u;
        }
        uint3 pixel = p;
        pixel.x = start / channels + texel;
        g_bindless_uint_surf3d[NonUniformResourceIndex(uint(desc.surf_index))][pixel] = packed;
    }
}
uint oc_tex3d_read_uint(OCTextureDesc desc, uint x, uint y, uint z) { return oc_tex3d_read_words<1>(desc, uint3(x,y,z)).x; }
void oc_tex3d_write(OCTextureDesc desc, uint value, uint x, uint y, uint z) { oc_tex3d_write_words<1>(desc, uint4(value,0,0,0), uint3(x,y,z)); }
uint2 oc_tex3d_read_uint2(OCTextureDesc desc, uint x, uint y, uint z) { return oc_tex3d_read_words<2>(desc, uint3(x,y,z)).xy; }
void oc_tex3d_write(OCTextureDesc desc, uint2 value, uint x, uint y, uint z) { oc_tex3d_write_words<2>(desc, uint4(value,0,0), uint3(x,y,z)); }
uint4 oc_tex3d_read_uint4(OCTextureDesc desc, uint x, uint y, uint z) { return oc_tex3d_read_words<4>(desc, uint3(x,y,z)); }
void oc_tex3d_write(OCTextureDesc desc, uint4 value, uint x, uint y, uint z) { oc_tex3d_write_words<4>(desc, value, uint3(x,y,z)); }


float2x2 oc_inverse(float2x2 m) { return float2x2(m[1][1], -m[0][1], -m[1][0], m[0][0]) / determinant(m); }
// Keep DSL's half-precision operation order. Widening the entire inverse to
// float loses the rounding of minors, determinant and cofactor products.
matrix<float16_t,2,2> oc_inverse(matrix<float16_t,2,2> input) {
    float2x2 m = oc_half2float(input);
    float d = oc_half_minor(m[0][0], m[1][1], m[1][0], m[0][1]);
    // DSL's 1.0f / half and half * float are float operations, rounded by
    // construction of the final half matrix (not by the reciprocal itself).
    float reciprocal = 1.0f / d;
    float2x2 result = float2x2(m[1][1], -m[0][1], -m[1][0], m[0][0]) * reciprocal;
    return oc_float2half(result);
}
matrix<float16_t,3,3> oc_inverse(matrix<float16_t,3,3> input) {
    float3x3 m = oc_half2float(input);
    float3x3 c;
    c[0][0] = oc_half_minor(m[1][1], m[2][2], m[2][1], m[1][2]);
    c[0][1] = oc_half_minor(m[2][1], m[0][2], m[0][1], m[2][2]);
    c[0][2] = oc_half_minor(m[0][1], m[1][2], m[1][1], m[0][2]);
    c[1][0] = oc_half_minor(m[2][0], m[1][2], m[1][0], m[2][2]);
    c[1][1] = oc_half_minor(m[0][0], m[2][2], m[2][0], m[0][2]);
    c[1][2] = oc_half_minor(m[1][0], m[0][2], m[0][0], m[1][2]);
    c[2][0] = oc_half_minor(m[1][0], m[2][1], m[2][0], m[1][1]);
    c[2][1] = oc_half_minor(m[2][0], m[0][1], m[0][0], m[2][1]);
    c[2][2] = oc_half_minor(m[0][0], m[1][1], m[1][0], m[0][1]);
    float d1 = oc_half_mul(m[1][0], oc_half_minor(m[0][1], m[2][2], m[2][1], m[0][2]));
    // Match DSL's two outer HFMA steps as well as the fused minors.
    float partial = oc_half_fma(m[0][0], c[0][0], -d1);
    float determinant = oc_half_fma(m[2][0], c[0][2], partial);
    float reciprocal = oc_half_value(1.0f / determinant);
    return oc_float2half(c * reciprocal);
}
matrix<float16_t,4,4> oc_inverse(matrix<float16_t,4,4> input) {
    float4x4 m = oc_half2float(input);
    float coef00 = oc_half_minor(m[2].z, m[3].w, m[3].z, m[2].w);
    float coef02 = oc_half_minor(m[1].z, m[3].w, m[3].z, m[1].w);
    float coef03 = oc_half_minor(m[1].z, m[2].w, m[2].z, m[1].w);
    float coef04 = oc_half_minor(m[2].y, m[3].w, m[3].y, m[2].w);
    float coef06 = oc_half_minor(m[1].y, m[3].w, m[3].y, m[1].w);
    float coef07 = oc_half_minor(m[1].y, m[2].w, m[2].y, m[1].w);
    float coef08 = oc_half_minor(m[2].y, m[3].z, m[3].y, m[2].z);
    float coef10 = oc_half_minor(m[1].y, m[3].z, m[3].y, m[1].z);
    float coef11 = oc_half_minor(m[1].y, m[2].z, m[2].y, m[1].z);
    float coef12 = oc_half_minor(m[2].x, m[3].w, m[3].x, m[2].w);
    float coef14 = oc_half_minor(m[1].x, m[3].w, m[3].x, m[1].w);
    float coef15 = oc_half_minor(m[1].x, m[2].w, m[2].x, m[1].w);
    float coef16 = oc_half_minor(m[2].x, m[3].z, m[3].x, m[2].z);
    float coef18 = oc_half_minor(m[1].x, m[3].z, m[3].x, m[1].z);
    float coef19 = oc_half_minor(m[1].x, m[2].z, m[2].x, m[1].z);
    float coef20 = oc_half_minor(m[2].x, m[3].y, m[3].x, m[2].y);
    float coef22 = oc_half_minor(m[1].x, m[3].y, m[3].x, m[1].y);
    float coef23 = oc_half_minor(m[1].x, m[2].y, m[2].x, m[1].y);
    float4 fac0 = float4(coef00, coef00, coef02, coef03);
    float4 fac1 = float4(coef04, coef04, coef06, coef07);
    float4 fac2 = float4(coef08, coef08, coef10, coef11);
    float4 fac3 = float4(coef12, coef12, coef14, coef15);
    float4 fac4 = float4(coef16, coef16, coef18, coef19);
    float4 fac5 = float4(coef20, coef20, coef22, coef23);
    float4 v0 = float4(m[1].x, m[0].x, m[0].x, m[0].x);
    float4 v1 = float4(m[1].y, m[0].y, m[0].y, m[0].y);
    float4 v2 = float4(m[1].z, m[0].z, m[0].z, m[0].z);
    float4 v3 = float4(m[1].w, m[0].w, m[0].w, m[0].w);
    float4 inv0 = oc_half_fma(v3, fac2, oc_half_fma(v1, fac0, -oc_half_value(v2 * fac1)));
    float4 inv1 = oc_half_fma(v3, fac4, oc_half_fma(v0, fac0, -oc_half_value(v2 * fac3)));
    float4 inv2 = oc_half_fma(v3, fac5, oc_half_fma(v0, fac1, -oc_half_value(v1 * fac3)));
    float4 inv3 = oc_half_fma(v2, fac5, oc_half_fma(v0, fac2, -oc_half_value(v1 * fac4)));
    inv0 *= float4(1, -1, 1, -1);
    inv1 *= float4(-1, 1, -1, 1);
    inv2 *= float4(1, -1, 1, -1);
    inv3 *= float4(-1, 1, -1, 1);
    float dot1 = oc_half_fma(m[0].x, inv0.x, oc_half_mul(m[0].y, inv1.x));
    dot1 = oc_half_fma(m[0].z, inv2.x, dot1);
    dot1 = oc_half_fma(m[0].w, inv3.x, dot1);
    float reciprocal = 1.0f / dot1;
    return oc_float2half(float4x4(inv0, inv1, inv2, inv3) * reciprocal);
}
float16_t oc_inverse_lerp(float16_t x, float16_t a, float16_t b) { return (x-a)/(b-a); }
vector<float16_t,2> oc_inverse_lerp(vector<float16_t,2> x, vector<float16_t,2> a, vector<float16_t,2> b) { return (x-a)/(b-a); }
vector<float16_t,3> oc_inverse_lerp(vector<float16_t,3> x, vector<float16_t,3> a, vector<float16_t,3> b) { return (x-a)/(b-a); }
vector<float16_t,4> oc_inverse_lerp(vector<float16_t,4> x, vector<float16_t,4> a, vector<float16_t,4> b) { return (x-a)/(b-a); }
