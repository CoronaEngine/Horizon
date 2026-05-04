
#pragma once

#include "cuda_matrix_func.h"

template<class To, class From>
[[nodiscard]] OC_DEVICE_FLAG To bit_cast(const From &src) noexcept {
    static_assert(sizeof(To) == sizeof(From));
    return *reinterpret_cast<const To *>(&src);
}

struct alignas(16) TriangleHit {
    oc_uint inst_id{oc_uint(-1)};
    oc_uint prim_id{oc_uint(-1)};
    oc_float2 bary;
    TriangleHit() = default;
    TriangleHit(oc_uint inst_id, oc_uint prim_id, oc_float2 bary)
        : inst_id(inst_id), prim_id(prim_id), bary(bary) {}
};

struct alignas(16) Ray {
public:
    oc_float4 org_min;
    oc_float4 dir_max;
    Ray() = default;
    Ray(oc_float4 org, oc_float4 dir)
        : org_min(org), dir_max(dir) {}
};

template<typename T>
struct OCBuffer {
    T *ptr{};
    oc_uint offset{};
    oc_ulong size{};
    template<typename Index>
    [[nodiscard]] const T &operator[](Index index) const noexcept { return ptr[index]; }
    template<typename Index>
    [[nodiscard]] T &operator[](Index index) noexcept { return ptr[index]; }
};

template<typename T>
oc_ulong oc_buffer_size(OCBuffer<T> buffer) {
    return buffer.size;
}

constexpr float ray_t_max = 1e16f;

OC_DEVICE_FLAG oc_float3 oc_offset_ray_origin(const oc_float3 &p_in, const oc_float3 &n_in) noexcept {
    constexpr auto origin = 1.0f / 32.0f;
    constexpr auto float_scale = 1.0f / 65536.0f;
    constexpr auto int_scale = 256.0f;

    oc_float3 n = n_in;
    auto of_i = oc_make_int3(static_cast<int>(int_scale * n.x),
                             static_cast<int>(int_scale * n.y),
                             static_cast<int>(int_scale * n.z));

    oc_float3 p = p_in;
    oc_float3 p_i = oc_make_float3(
        bit_cast<float>(bit_cast<int>(p.x) + oc_select(p.x < 0, -of_i.x, of_i.x)),
        bit_cast<float>(bit_cast<int>(p.y) + oc_select(p.y < 0, -of_i.y, of_i.y)),
        bit_cast<float>(bit_cast<int>(p.z) + oc_select(p.z < 0, -of_i.z, of_i.z)));

    return oc_select(oc_abs(p) < origin, p + float_scale * n, p_i);
}

OC_DEVICE_FLAG inline Ray oc_make_ray(oc_float3 org, oc_float3 dir, oc_float t_max = ray_t_max) noexcept {
    Ray ret;

    ret.org_min = oc_make_float4(org, 0.f);
    ret.dir_max = oc_make_float4(dir, t_max);

    return ret;
}

enum struct OCPixelStorage : oc_uint {
    BYTE1,
    BYTE2,
    BYTE4,

    UINT1,
    UINT2,
    UINT4,

    FLOAT1,
    FLOAT2,
    FLOAT4,

    UNKNOWN
};

template<typename T, typename U>
inline T oc_atomicExch(T &a, U v) noexcept {
    return atomicExch(&a, v);
}

template<typename T, typename Index, typename U>
inline T oc_atomicExch(OCBuffer<T> buffer, Index index, U val) noexcept {
    return oc_atomicExch(buffer[index], val);
}

template<typename T, typename Offset>
inline T oc_atomicExch(OCBuffer<oc_uchar> buffer, Offset offset, T val) noexcept {
    T *ref = (reinterpret_cast<T *>(&(buffer.ptr[offset])));
    return oc_atomicExch(ref[0], val);
}

template<typename T>
inline T oc_atomicCAS(T &ref, T compare, T val) noexcept {
    return atomicCAS(&ref, compare, val);
}

template<typename T, typename U>
inline T oc_atomicAdd(T &a, U v) noexcept {
    return atomicAdd(&a, v);
}

template<typename T, typename Index, typename U>
inline T oc_atomicAdd(OCBuffer<T> buffer, Index index, U val) noexcept {
    return oc_atomicAdd(buffer[index], val);
}

template<typename T, typename Offset>
inline T oc_atomicAdd(OCBuffer<oc_uchar> buffer, Offset offset, T val) noexcept {
    T *ref = (reinterpret_cast<T *>(&(buffer.ptr[offset])));
    return oc_atomicAdd(ref[0], val);
}

template<typename T, typename U>
inline T oc_atomicSub(T &a, U v) noexcept {
    return atomicAdd(&a, -v);
}

template<typename T, typename Index, typename U>
inline T oc_atomicSub(OCBuffer<T> buffer, Index index, U val) noexcept {
    return oc_atomicSub(buffer[index], val);
}

template<typename T, typename Offset>
inline T oc_atomicSub(OCBuffer<oc_uchar> buffer, Offset offset, T val) noexcept {
    T *ref = (reinterpret_cast<T *>(&(buffer.ptr[offset])));
    return oc_atomicSub(ref[0], val);
}

#define OC_WARP_FULL_MASK 0xffff'ffffu
#define OC_WARP_ACTIVE_MASK __activemask()

[[nodiscard]] inline oc_uint4 oc_warp_active_bit_mask(bool pred) noexcept {
    return oc_make_uint4(__ballot_sync(OC_WARP_ACTIVE_MASK, pred), 0u, 0u, 0u);
}

[[nodiscard]] inline oc_uint oc_warp_active_count_bits(bool pred) noexcept {
    return oc_popcount(__ballot_sync(OC_WARP_ACTIVE_MASK, pred));
}

[[nodiscard]] inline auto oc_warp_prefix_mask() noexcept {
    oc_uint ret;
    asm("mov.u32 %0, %lanemask_lt;"
        : "=r"(ret));
    return ret;
}

[[nodiscard]] inline auto oc_warp_lane_id() noexcept {
    oc_uint ret;
    asm("mov.u32 %0, %laneid;"
        : "=r"(ret));
    return ret;
}

[[nodiscard]] constexpr auto oc_warp_size() noexcept {
    return static_cast<oc_uint>(warpSize);
}

void oc_synchronize_block() noexcept {
    __syncthreads();
}

[[nodiscard]] inline auto oc_warp_first_active_lane() noexcept {
    return __ffs(OC_WARP_ACTIVE_MASK) - 1u;
}

[[nodiscard]] inline auto oc_warp_is_first_active_lane() noexcept {
    return oc_warp_first_active_lane() == oc_warp_lane_id();
}

[[nodiscard]] inline oc_uint oc_warp_prefix_count_bits(bool pred) noexcept {
    return oc_popcount(__ballot_sync(OC_WARP_ACTIVE_MASK, pred) & oc_warp_prefix_mask());
}

[[nodiscard]] inline auto oc_warp_active_all(bool pred) noexcept {
    return static_cast<oc_bool>(__all_sync(OC_WARP_ACTIVE_MASK, pred));
}

[[nodiscard]] inline auto oc_warp_active_any(bool pred) noexcept {
    return static_cast<oc_bool>(__any_sync(OC_WARP_ACTIVE_MASK, pred));
}

struct OCTextureDesc {
    cudaTextureObject_t texture{};
    cudaSurfaceObject_t surface{};
    OCPixelStorage pixel_storage{};
};

[[nodiscard]] inline bool oc_is_null_buffer(void *buffer) noexcept {
    return buffer == nullptr;
}

[[nodiscard]] inline bool oc_is_null_texture(OCTextureDesc obj) noexcept {
    return obj.texture == 0;
}

struct OCBufferDesc {
    void *handle{};
    oc_uint offset_in_byte{};
    size_t size_in_byte{};

    char *head() {
        return reinterpret_cast<char *>(handle) + offset_in_byte;
    }
};

struct OCBindlessArrayDesc {
    OCBufferDesc *buffer_slot;
    cudaTextureObject_t *tex3d_slot{};
    cudaTextureObject_t *tex2d_slot{};
};

template<typename A, typename B>
struct oc_is_same {
    static constexpr bool value = false;
};

template<typename A>
struct oc_is_same<A, A> {
    static constexpr bool value = true;
};

template<typename A, typename B>
static constexpr bool oc_is_same_v = oc_is_same<A, B>::value;

using uchar = unsigned char;


template<typename T>
struct oc_type {
    static constexpr auto dimension = 1;
    using element_type = T;
};

template<typename T, int N>
struct oc_vec {
    using type = T;
};

#define OC_MAKE_VEC_TYPE(T, N) \
    template<>                 \
    struct oc_vec<T, N> {      \
        using type = T##N;     \
    };

#define OC_MAKE_VEC_TYPE_N(T) \
    OC_MAKE_VEC_TYPE(T, 2)    \
    OC_MAKE_VEC_TYPE(T, 3)    \
    OC_MAKE_VEC_TYPE(T, 4)

OC_MAKE_VEC_TYPE_N(oc_float)
OC_MAKE_VEC_TYPE_N(oc_int)
OC_MAKE_VEC_TYPE_N(oc_uchar)
OC_MAKE_VEC_TYPE_N(oc_uint)

template<typename T, int N>
using oc_vec_t = typename oc_vec<T, N>::type;

#undef OC_MAKE_VEC_TYPE
#undef OC_MAKE_VEC_TYPE_N

#define OC_MAKE_TYPE_DIM(type, dim)            \
    template<>                                 \
    struct oc_type<type##dim> {                \
        static constexpr auto dimension = dim; \
        using element_type = type;             \
    };

#define OC_MAKE_TYPE(type)    \
    OC_MAKE_TYPE_DIM(type, 2) \
    OC_MAKE_TYPE_DIM(type, 3) \
    OC_MAKE_TYPE_DIM(type, 4)

OC_MAKE_TYPE(oc_uint)
OC_MAKE_TYPE(oc_int)
OC_MAKE_TYPE(oc_float)
OC_MAKE_TYPE(oc_uchar)
OC_MAKE_TYPE(oc_bool)

template<typename T>
static constexpr auto oc_type_dim = oc_type<T>::dimension;

template<typename T>
using oc_type_element_t = typename oc_type<T>::element_type;

#undef OC_MAKE_TYPE
#undef OC_MAKE_TYPE_DIM


template<typename T>
OC_DEVICE_FLAG T &oc_bindless_array_buffer_read(OCBindlessArrayDesc bindless_array, oc_uint buffer_index, oc_ulong index) noexcept {
    T *buffer = reinterpret_cast<T *>(bindless_array.buffer_slot[buffer_index].head());
    return buffer[index];
}

OC_DEVICE_FLAG oc_uint oc_bindless_array_buffer_size(OCBindlessArrayDesc bindless_array, oc_uint buffer_index) noexcept {
    return bindless_array.buffer_slot[buffer_index].size_in_byte;
}

template<typename T>
OC_DEVICE_FLAG T &oc_bindless_array_byte_buffer_read(OCBindlessArrayDesc bindless_array, oc_uint buffer_index, oc_ulong offset) noexcept {
    char *buffer = reinterpret_cast<char *>(bindless_array.buffer_slot[buffer_index].head());
    return *reinterpret_cast<T *>(&buffer[offset]);
}

template<typename T>
OC_DEVICE_FLAG void oc_bindless_array_buffer_write(OCBindlessArrayDesc bindless_array, oc_uint buffer_index,
                                               oc_ulong index, const T &val) noexcept {
    T *buffer = reinterpret_cast<T *>(bindless_array.buffer_slot[buffer_index].head());
    buffer[index] = val;
}

template<typename T>
OC_DEVICE_FLAG void oc_bindless_array_byte_buffer_write(OCBindlessArrayDesc bindless_array, oc_uint buffer_index,
                                                    oc_ulong offset, const T &val) noexcept {
    char *buffer = reinterpret_cast<char *>(bindless_array.buffer_slot[buffer_index].head());
    T *ref = (reinterpret_cast<T *>(&(buffer[offset])));
    ref[0] = val;
}

template<typename T>
OC_DEVICE_FLAG T &oc_byte_buffer_read(OCBuffer<oc_uchar> buffer, oc_ulong offset) noexcept {
    T *ref = (reinterpret_cast<T *>(&(buffer.ptr[offset])));
    return ref[0];
}

template<int N>
OC_DEVICE_FLAG auto oc_byte_buffer_read(OCBuffer<oc_uchar> buffer, oc_ulong offset) noexcept {
    if constexpr (N == 1) {
        oc_uint *ref = (reinterpret_cast<oc_uint *>(&(buffer.ptr[offset])));
        return ref[0];
    } else if constexpr (N == 2) {
        oc_uint2 *ref = (reinterpret_cast<oc_uint2 *>(&(buffer.ptr[offset])));
        return ref[0];
    } else if constexpr (N == 3) {
        oc_uint3 *ref = (reinterpret_cast<oc_uint3 *>(&(buffer.ptr[offset])));
        return ref[0];
    } else if constexpr (N == 4) {
        oc_uint4 *ref = (reinterpret_cast<oc_uint4 *>(&(buffer.ptr[offset])));
        return ref[0];
    }
}

template<typename T>
OC_DEVICE_FLAG void oc_byte_buffer_write(OCBuffer<oc_uchar> buffer, oc_ulong offset, const T &val) noexcept {
    T *ref = (reinterpret_cast<T *>(&(buffer.ptr[offset])));
    ref[0] = val;
}

template<size_t N>
OC_DEVICE_FLAG oc_array<float, N> oc_tex3d_sample_float_impl(cudaTextureObject_t texture, oc_float u, oc_float v, oc_float w = 0.f) noexcept {
    if constexpr (N == 1) {
        auto ret = tex3D<float>(texture, u, v, w);
        return {ret};
    } else if constexpr (N == 2) {
        auto ret = tex3D<float2>(texture, u, v, w);
        return {ret.x, ret.y};
    } else if constexpr (N == 3) {
        auto ret = tex3D<float4>(texture, u, v, w);
        return {ret.x, ret.y, ret.z};
    } else if constexpr (N == 4) {
        auto ret = tex3D<float4>(texture, u, v, w);
        return {ret.x, ret.y, ret.z, ret.w};
    }
    return {};
}

template<size_t N>
OC_DEVICE_FLAG oc_array<float, N> oc_tex3d_sample_float(OCTextureDesc obj, oc_float u, oc_float v, oc_float w = 0.f) noexcept {
    return oc_tex3d_sample_float_impl<N>(obj.texture, u, v, w);
}

template<size_t N>
OC_DEVICE_FLAG oc_array<float, N> oc_bindless_array_tex3d_sample(OCBindlessArrayDesc bindless_array, oc_uint tex_index,
                                                           oc_float u, oc_float v, oc_float w = 0.f) noexcept {
    cudaTextureObject_t texture = bindless_array.tex3d_slot[tex_index];
    return oc_tex3d_sample_float_impl<N>(texture, u, v, w);
}

template<size_t N>
OC_DEVICE_FLAG oc_array<float, N> oc_tex2d_sample_float_impl(cudaTextureObject_t texture, oc_float u, oc_float v) noexcept {
    if constexpr (N == 1) {
        auto ret = tex2D<float>(texture, u, v);
        return {ret};
    } else if constexpr (N == 2) {
        auto ret = tex2D<float2>(texture, u, v);
        return {ret.x, ret.y};
    } else if constexpr (N == 3) {
        auto ret = tex2D<float4>(texture, u, v);
        return {ret.x, ret.y, ret.z};
    } else if constexpr (N == 4) {
        auto ret = tex2D<float4>(texture, u, v);
        return {ret.x, ret.y, ret.z, ret.w};
    }
    return {};
}

template<size_t N>
OC_DEVICE_FLAG oc_array<float, N> oc_tex2d_sample_float(OCTextureDesc obj, oc_float u, oc_float v) noexcept {
    return oc_tex2d_sample_float_impl<N>(obj.texture, u, v);
}

template<size_t N>
OC_DEVICE_FLAG oc_array<float, N> oc_bindless_array_tex2d_sample(OCBindlessArrayDesc bindless_array, oc_uint tex_index,
                                                           oc_float u, oc_float v) noexcept {
    cudaTextureObject_t texture = bindless_array.tex2d_slot[tex_index];
    return oc_tex2d_sample_float_impl<N>(texture, u, v);
}

template<typename Dst, typename Src>
OC_DEVICE_FLAG auto oc_convert_scalar(const Src &src) noexcept {
    static_assert(oc_type_dim<Src> == 1 && oc_type_dim<Dst> == 1);
    if constexpr (oc_is_same_v<Dst, Src>) {
        return src;
    } else if constexpr (oc_is_same_v<Dst, float>) {
        // Src is uchar
        return static_cast<float>(src / 255.f);
    } else if constexpr (oc_is_same_v<Dst, uchar>) {
        // Src is float
        return static_cast<uchar>(src * 255);
    }
    __builtin_unreachable();
}

template<typename Dst, typename Src>
OC_DEVICE_FLAG auto oc_convert_vector(const Src &val) noexcept {
    static_assert(oc_type_dim<Dst> == oc_type_dim<Src>);
    using element_type = oc_type_element_t<Dst>;
    if constexpr (oc_type_dim<Dst> == 1) {
        return oc_convert_scalar<Dst>(val);
    } else if constexpr (oc_type_dim<Dst> == 2) {
        return Dst(oc_convert_scalar<element_type>(val.x),
                   oc_convert_scalar<element_type>(val.y));
    } else if constexpr (oc_type_dim<Dst> == 4) {
        return Dst(oc_convert_scalar<element_type>(val.x),
                   oc_convert_scalar<element_type>(val.y),
                   oc_convert_scalar<element_type>(val.z),
                   oc_convert_scalar<element_type>(val.w));
    }
}

template<int Dim, typename Src>
OC_DEVICE_FLAG auto oc_fit(const Src &src) noexcept {
    using element_type = oc_type_element_t<Src>;
    static constexpr auto dim = oc_type_dim<Src>;
    using ret_type = typename oc_vec_t<element_type, Dim>;
    if constexpr (dim == 2) {
    }
}

template<typename T>
OC_DEVICE_FLAG T oc_tex3d_read(OCTextureDesc obj, oc_uint x, oc_uint y, oc_uint z = 0) noexcept {
    if constexpr (oc_is_same_v<T, uchar> || oc_is_same_v<T, float>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE1: {
                auto v = surf3Dread<uchar>(obj.surface, x * sizeof(oc_uchar), y, z, cudaBoundaryModeZero);
                return oc_convert_scalar<T>(v);
            }
            case OCPixelStorage::FLOAT1: {
                auto v = surf3Dread<float>(obj.surface, x * sizeof(float), y, z, cudaBoundaryModeZero);
                return oc_convert_scalar<T>(v);
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uchar2> || oc_is_same_v<T, oc_float2>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE2: {
                auto v = surf3Dread<uchar2>(obj.surface, x * sizeof(uchar2), y, z, cudaBoundaryModeZero);
                return oc_convert_vector<T>(oc_make_uchar2(v.x, v.y));
            }
            case OCPixelStorage::FLOAT2: {
                auto v = surf3Dread<float2>(obj.surface, x * sizeof(float2), y, z, cudaBoundaryModeZero);
                return oc_convert_vector<T>(oc_make_float2(v.x, v.y));
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uchar4> || oc_is_same_v<T, oc_float4>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE4: {
                auto v = surf3Dread<uchar4>(obj.surface, x * sizeof(uchar4), y, z, cudaBoundaryModeZero);
                return oc_convert_vector<T>(oc_make_uchar4(v.x, v.y, v.z, v.w));
            }
            case OCPixelStorage::FLOAT4: {
                auto v = surf3Dread<float4>(obj.surface, x * sizeof(float4), y, z, cudaBoundaryModeZero);
                return oc_convert_vector<T>(oc_make_float4(v.x, v.y, v.z, v.w));
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uint>) {
        auto v = surf3Dread<unsigned int>(obj.surface, x * sizeof(unsigned int), y, z, cudaBoundaryModeZero);
        return v;
    } else if constexpr (oc_is_same_v<T, oc_uint2>) {
        auto v = surf3Dread<uint2>(obj.surface, x * sizeof(uint2), y, z, cudaBoundaryModeZero);
        return oc_make_uint2(v.x, v.y);
    } else if constexpr (oc_is_same_v<T, oc_uint4>) {
        auto v = surf3Dread<uint4>(obj.surface, x * sizeof(uint4), y, z, cudaBoundaryModeZero);
        return oc_make_uint4(v.x, v.y, v.z, v.w);
    }
    assert(0);
    __builtin_unreachable();
}

template<typename T>
OC_DEVICE_FLAG T oc_tex2d_read(OCTextureDesc obj, oc_uint x, oc_uint y) noexcept {
    if constexpr (oc_is_same_v<T, uchar> || oc_is_same_v<T, float>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE1: {
                auto v = surf2Dread<uchar>(obj.surface, x * sizeof(oc_uchar), y, cudaBoundaryModeZero);
                return oc_convert_scalar<T>(v);
            }
            case OCPixelStorage::FLOAT1: {
                auto v = surf2Dread<float>(obj.surface, x * sizeof(float), y, cudaBoundaryModeZero);
                return oc_convert_scalar<T>(v);
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uchar2> || oc_is_same_v<T, oc_float2>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE2: {
                auto v = surf2Dread<uchar2>(obj.surface, x * sizeof(uchar2), y, cudaBoundaryModeZero);
                return oc_convert_vector<T>(oc_make_uchar2(v.x, v.y));
            }
            case OCPixelStorage::FLOAT2: {
                auto v = surf2Dread<float2>(obj.surface, x * sizeof(float2), y, cudaBoundaryModeZero);
                return oc_convert_vector<T>(oc_make_float2(v.x, v.y));
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uchar4> || oc_is_same_v<T, oc_float4>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE4: {
                auto v = surf2Dread<uchar4>(obj.surface, x * sizeof(uchar4), y, cudaBoundaryModeZero);
                return oc_convert_vector<T>(oc_make_uchar4(v.x, v.y, v.z, v.w));
            }
            case OCPixelStorage::FLOAT4: {
                auto v = surf2Dread<float4>(obj.surface, x * sizeof(float4), y, cudaBoundaryModeZero);
                return oc_convert_vector<T>(oc_make_float4(v.x, v.y, v.z, v.w));
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uint>) {
        auto v = surf2Dread<unsigned int>(obj.surface, x * sizeof(unsigned int), y, cudaBoundaryModeZero);
        return v;
    } else if constexpr (oc_is_same_v<T, oc_uint2>) {
        auto v = surf2Dread<uint2>(obj.surface, x * sizeof(uint2), y, cudaBoundaryModeZero);
        return oc_make_uint2(v.x, v.y);
    } else if constexpr (oc_is_same_v<T, oc_uint4>) {
        auto v = surf2Dread<uint4>(obj.surface, x * sizeof(uint4), y, cudaBoundaryModeZero);
        return oc_make_uint4(v.x, v.y, v.z, v.w);
    }
    assert(0);
    __builtin_unreachable();
}

template<typename T>
OC_DEVICE_FLAG void oc_tex3d_write(OCTextureDesc obj, T val, oc_uint x, oc_uint y, oc_uint z = 0) noexcept {
    if constexpr (oc_is_same_v<T, uchar> || oc_is_same_v<T, float>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE1: {
                uchar v = oc_convert_scalar<uchar>(val);
                surf3Dwrite(v, obj.surface, x * sizeof(uchar), y, z, cudaBoundaryModeZero);
                return;
            }
            case OCPixelStorage::FLOAT1: {
                oc_float v = oc_convert_vector<float>(val);
                surf3Dwrite(v, obj.surface, x * sizeof(float), y, z, cudaBoundaryModeZero);
                return;
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uchar2> || oc_is_same_v<T, oc_float2>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE2: {
                oc_uchar2 v = oc_convert_vector<oc_uchar2>(val);
                surf3Dwrite(make_uchar2(v.x, v.y), obj.surface, x * sizeof(uchar2), y, z, cudaBoundaryModeZero);
                return;
            }
            case OCPixelStorage::FLOAT2: {
                oc_float2 v = oc_convert_vector<oc_float2>(val);
                surf3Dwrite(make_float2(v.x, v.y), obj.surface, x * sizeof(float2), y, z, cudaBoundaryModeZero);
                return;
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uchar4> || oc_is_same_v<T, oc_float4>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE4: {
                oc_uchar4 v = oc_convert_vector<oc_uchar4>(val);
                surf3Dwrite(make_uchar4(v.x, v.y, v.z, v.w), obj.surface, x * sizeof(uchar4), y, z, cudaBoundaryModeZero);
                return;
            }
            case OCPixelStorage::FLOAT4: {
                oc_float4 v = oc_convert_vector<oc_float4>(val);
                surf3Dwrite(make_float4(v.x, v.y, v.z, v.w), obj.surface, x * sizeof(float4), y, z, cudaBoundaryModeZero);
                return;
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uint>) {
        surf3Dwrite(val, obj.surface, x * sizeof(oc_uint), y, z, cudaBoundaryModeZero);
        return;
    } else if constexpr (oc_is_same_v<T, oc_uint2>) {
        surf3Dwrite(make_uint2(val.x, val.y), obj.surface, x * sizeof(uint2), y, z, cudaBoundaryModeZero);
        return;
    } else if constexpr (oc_is_same_v<T, oc_uint4>) {
        surf3Dwrite(make_uint4(val.x, val.y, val.z, val.w), obj.surface, x * sizeof(uint4), y, z, cudaBoundaryModeZero);
        return;
    }
    assert(0);
    __builtin_unreachable();
}


template<typename T>
OC_DEVICE_FLAG void oc_tex2d_write(OCTextureDesc obj, T val, oc_uint x, oc_uint y) noexcept {
    if constexpr (oc_is_same_v<T, uchar> || oc_is_same_v<T, float>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE1: {
                uchar v = oc_convert_scalar<uchar>(val);
                surf2Dwrite(v, obj.surface, x * sizeof(uchar), y, cudaBoundaryModeZero);
                return;
            }
            case OCPixelStorage::FLOAT1: {
                oc_float v = oc_convert_vector<float>(val);
                surf2Dwrite(v, obj.surface, x * sizeof(float), y, cudaBoundaryModeZero);
                return;
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uchar2> || oc_is_same_v<T, oc_float2>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE2: {
                oc_uchar2 v = oc_convert_vector<oc_uchar2>(val);
                surf2Dwrite(make_uchar2(v.x, v.y), obj.surface, x * sizeof(uchar2), y, cudaBoundaryModeZero);
                return;
            }
            case OCPixelStorage::FLOAT2: {
                oc_float2 v = oc_convert_vector<oc_float2>(val);
                surf2Dwrite(make_float2(v.x, v.y), obj.surface, x * sizeof(float2), y, cudaBoundaryModeZero);
                return;
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uchar4> || oc_is_same_v<T, oc_float4>) {
        switch (obj.pixel_storage) {
            case OCPixelStorage::BYTE4: {
                oc_uchar4 v = oc_convert_vector<oc_uchar4>(val);
                surf2Dwrite(make_uchar4(v.x, v.y, v.z, v.w), obj.surface, x * sizeof(uchar4), y, cudaBoundaryModeZero);
                return;
            }
            case OCPixelStorage::FLOAT4: {
                oc_float4 v = oc_convert_vector<oc_float4>(val);
                surf2Dwrite(make_float4(v.x, v.y, v.z, v.w), obj.surface, x * sizeof(float4), y, cudaBoundaryModeZero);
                return;
            }
        }
    } else if constexpr (oc_is_same_v<T, oc_uint>) {
        surf2Dwrite(val, obj.surface, x * sizeof(oc_uint), y, cudaBoundaryModeZero);
        return;
    } else if constexpr (oc_is_same_v<T, oc_uint2>) {
        surf2Dwrite(make_uint2(val.x, val.y), obj.surface, x * sizeof(uint2), y, cudaBoundaryModeZero);
        return;
    } else if constexpr (oc_is_same_v<T, oc_uint4>) {
        surf2Dwrite(make_uint4(val.x, val.y, val.z, val.w), obj.surface, x * sizeof(uint4), y, cudaBoundaryModeZero);
        return;
    }
    assert(0);
    __builtin_unreachable();
}