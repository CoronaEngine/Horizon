//
// Created by Zero on 2022/8/17.
//

#include "optix.h"

template<typename... Args>
OC_DEVICE_FLAG inline void trace(OptixTraversableHandle handle,
                             Ray ray,
                             oc_uint flags,
                             oc_uint SBToffset,
                             oc_uint SBTstride,
                             oc_uint missSBTIndex,
                             Args &&...payload) {
    auto origin = ::make_float3(ray.org_min.x, ray.org_min.y, ray.org_min.z);
    auto direction = ::make_float3(ray.dir_max.x, ray.dir_max.y, ray.dir_max.z);

    optixTrace(
        handle,
        origin,
        direction,
        ray.org_min.w,
        ray.dir_max.w,
        0.0f,// rayTime
        OptixVisibilityMask(1),
        flags,
        SBToffset,   // SBT offset
        SBTstride,   // SBT stride
        missSBTIndex,// missSBTIndex
        std::forward<Args>(payload)...);
}

template<typename... Args>
OC_DEVICE_FLAG inline void traverse(OptixTraversableHandle handle,
                                Ray ray,
                                oc_uint flags,
                                oc_uint SBToffset,
                                oc_uint SBTstride,
                                oc_uint missSBTIndex,
                                Args &&...payload) {
    auto origin = ::make_float3(ray.org_min.x, ray.org_min.y, ray.org_min.z);
    auto direction = ::make_float3(ray.dir_max.x, ray.dir_max.y, ray.dir_max.z);

    optixTraverse(
        handle,
        origin,
        direction,
        ray.org_min.w,
        ray.dir_max.w,
        0.0f,// rayTime
        OptixVisibilityMask(1),
        flags,
        SBToffset,   // SBT offset
        SBTstride,   // SBT stride
        missSBTIndex,// missSBTIndex
        std::forward<Args>(payload)...);
}

OC_DEVICE_FLAG oc_uint3 getLaunchIndex() {
    auto idx = optixGetLaunchIndex();
    return oc_make_uint3(idx.x, idx.y, idx.z);
}

OC_DEVICE_FLAG oc_uint3 getLaunchDim() {
    auto idx = optixGetLaunchDimensions();
    return oc_make_uint3(idx.x, idx.y, idx.z);
}

OC_DEVICE_FLAG inline void *unpack_pointer(unsigned int i0, unsigned int i1) {
    const unsigned long long uptr = static_cast<unsigned long long>(i0) << 32 | i1;
    void *ptr = reinterpret_cast<void *>(uptr);
    return ptr;
}

OC_DEVICE_FLAG inline void pack_pointer(void *ptr, unsigned int &i0, unsigned int &i1) {
    const auto uptr = reinterpret_cast<unsigned long long>(ptr);
    i0 = uptr >> 32;
    i1 = uptr & 0x00000000ffffffff;
}

OC_DEVICE_FLAG inline void setPayloadOcclusion(bool occluded) {
    optixSetPayload_0(static_cast<unsigned int>(occluded));
}

OC_DEVICE_FLAG inline oc_float2 getTriangleBarycentric() {
    auto barycentric = optixGetTriangleBarycentrics();
    return oc_make_float2(1 - barycentric.y - barycentric.x, barycentric.x);
}

#define TRAVERSE_ONLY 1

OC_DEVICE_FLAG inline TriangleHit trace_closest_impl(OptixTraversableHandle handle,
                                             Ray ray) {
    TriangleHit hit;
    trace(handle, ray, OPTIX_RAY_FLAG_DISABLE_ANYHIT,
          0,// SBT offset
          0,// SBT stride
          0,// missSBTIndex
          hit.inst_id, hit.prim_id,
          reinterpret_cast<oc_uint &>(hit.bary.x),
          reinterpret_cast<oc_uint &>(hit.bary.y));
    return hit;
}

// OC_DEVICE_FLAG inline TriangleHit trace_closest_impl(OptixTraversableHandle handle,
//                                              Ray ray) {
//     TriangleHit hit;
//     oc_uint u0, u1;
//     pack_pointer(&hit, u0, u1);
//     trace(handle, ray, OPTIX_RAY_FLAG_DISABLE_ANYHIT,
//           0,// SBT offset
//           0,// SBT stride
//           0,// missSBTIndex
//           u0, u1);
//     return hit;
// }

TriangleHit getHitObjectInfo() {
    TriangleHit hit;
    hit.inst_id = optixHitObjectIsHit() ? optixHitObjectGetInstanceId() : ~0u;
    hit.prim_id = optixHitObjectGetPrimitiveIndex();
    unsigned int attr0 = optixHitObjectGetAttribute_0();
    unsigned int attr1 = optixHitObjectGetAttribute_1();
    float x = __uint_as_float(attr0);
    float y = __uint_as_float(attr1);
    hit.bary = oc_make_float2(1 - x - y, x);
    return hit;
}

OC_DEVICE_FLAG inline TriangleHit traverse_closest_impl(OptixTraversableHandle handle,
                                                Ray ray) {
    traverse(handle, ray, OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_DISABLE_CLOSESTHIT,
             0,// SBT offset
             0,// SBT stride
             0 // missSBTIndex
    );
    return getHitObjectInfo();
}

OC_DEVICE_FLAG inline TriangleHit oc_trace_closest(OptixTraversableHandle handle,
                                               Ray ray) {
#if TRAVERSE_ONLY
    return traverse_closest_impl(handle, ray);
#else
    return trace_closest_impl(handle, ray);
#endif
}

OC_DEVICE_FLAG inline bool traverse_occlusion_impl(OptixTraversableHandle handle, Ray ray) {
    traverse(handle, ray, OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
             1,// SBT offset
             0,// SBT stride
             0 // missSBTIndex
    );
    return optixHitObjectIsHit();
}

OC_DEVICE_FLAG inline bool trace_occlusion_impl(OptixTraversableHandle handle, Ray ray) {
    unsigned int occlude = 0u;
    trace(handle, ray, OPTIX_RAY_FLAG_DISABLE_ANYHIT | OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
          1,// SBT offset
          0,// SBT stride
          0,// missSBTIndex
          occlude);
    return bool(occlude);
}

OC_DEVICE_FLAG inline bool oc_trace_occlusion(OptixTraversableHandle handle, Ray ray) {
#if TRAVERSE_ONLY
    return traverse_occlusion_impl(handle, ray);
#else
    return trace_occlusion_impl(handle, ray);
#endif
}

OC_DEVICE_FLAG inline TriangleHit getClosestHit() {
    TriangleHit ret;
    ret.inst_id = optixGetInstanceId();
    ret.prim_id = optixGetPrimitiveIndex();
    ret.bary = getTriangleBarycentric();
    return ret;
}

template<typename T = TriangleHit>
OC_DEVICE_FLAG inline T *getPayloadPtr() {
    const unsigned int u0 = optixGetPayload_0();
    const unsigned int u1 = optixGetPayload_1();
    return reinterpret_cast<T *>(unpack_pointer(u0, u1));
}

template<typename T = TriangleHit>
OC_DEVICE_FLAG inline T &getPayload() {
    return *getPayloadPtr();
}

extern "C" __global__ void __closesthit__closest() {
    // auto &hit = getPayload();
    // hit = getClosestHit();
    TriangleHit hit = getClosestHit();
    optixSetPayload_0(hit.inst_id);
    optixSetPayload_1(hit.prim_id);
    optixSetPayload_2(__float_as_uint(hit.bary.x));
    optixSetPayload_3(__float_as_uint(hit.bary.y));
}

extern "C" __global__ void __closesthit__occlusion() {
    setPayloadOcclusion(true);
}