//
// Created by Zero on 06/06/2022.
//

#include "cuda_device.h"
#include "cuda_stream.h"
#include "cuda_texture.h"
#include "cuda_shader.h"
#include "cuda_mesh.h"
#include "cuda_bindless_array.h"
#include "rhi/context.h"
#include <optix_stubs.h>
#include <optix_function_table_definition.h>
#include <nvrtc.h>
#include "cuda_gl_interop.h"
#include "driver_types.h"
#include "cuda_compiler.h"
#include "optix_accel.h"
#include <cudaGL.h>
#include "cuda_command_visitor.h"

namespace ocarina {

CUDADevice::CUDADevice(RHIContext *context)
    : Device::Impl(context) {
    OC_CU_CHECK(cuInit(0));
    OC_CU_CHECK(cuDeviceGet(&cu_device_, 0));
    OC_CU_CHECK(cuDevicePrimaryCtxRetain(&cu_ctx_, cu_device_));
    cmd_visitor_ = std::make_unique<CUDACommandVisitor>(this);
    init_hardware_info();
}

void CUDADevice::init_hardware_info() {
    auto compute_cap_major = 0;
    auto compute_cap_minor = 0;
    OC_CU_CHECK(cuDeviceGetAttribute(&compute_cap_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, cu_device_));
    OC_CU_CHECK(cuDeviceGetAttribute(&compute_cap_minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, cu_device_));
    size_t total_mem = 0;
    OC_CU_CHECK(cuDeviceTotalMem(&total_mem, cu_device_));
    OC_INFO_FORMAT(
        "Created CUDA device : (capability = {}.{}, VRAM = {} MB).",
        compute_cap_major, compute_cap_minor,
        total_mem / (1024u * 1024u));
    compute_capability_ = 10u * compute_cap_major + compute_cap_minor;
}

DevicePrecisionCaps CUDADevice::precision_caps() const noexcept {
    DevicePrecisionCaps caps;
    caps.compute_capability = compute_capability_;
    size_t total_mem = 0;
    cuDeviceTotalMem(&total_mem, cu_device_);
    caps.total_vram_bytes = total_mem;
    caps.has_native_fp16 = (compute_capability_ >= 53);
    caps.has_fast_fp16 = (compute_capability_ >= 60);
    caps.has_tensor_fp16 = (compute_capability_ >= 70);
    return caps;
}

void CUDADevice::memory_allocate(handle_ty *handle, size_t size, bool exported) {
    if (!exported) {
        OC_CU_CHECK(cuMemAlloc(handle, size));
    } else {
        size_t granularity = 0;
        CUmemAllocationProp prop = {};
        prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
        prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
        prop.location.id = cu_device_;
        prop.requestedHandleTypes = CU_MEM_HANDLE_TYPE_WIN32;

        OC_CU_CHECK(cuMemGetAllocationGranularity(&granularity, &prop,
                                                  CU_MEM_ALLOC_GRANULARITY_MINIMUM));

        size_t aligned_size = ((size * sizeof(std::byte) + granularity - 1) / granularity) * granularity;
        prop.requestedHandleTypes = CU_MEM_HANDLE_TYPE_WIN32;

#if _WIN32 || _WIN64
        SECURITY_ATTRIBUTES secAttr = {};
        secAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        secAttr.lpSecurityDescriptor = NULL;
        secAttr.bInheritHandle = FALSE;

        CUmemAllocationHandleType handleType = CU_MEM_HANDLE_TYPE_WIN32;
        prop.win32HandleMetaData = &secAttr;
#else
        prop.win32HandleMetaData = nullptr;
        prop.requestedHandleTypes = CU_MEM_HANDLE_TYPE_NONE;
#endif

        CUdeviceptr ptr;
        OC_CU_CHECK(cuMemAddressReserve(&ptr, aligned_size, 0, 0, 0));
        CUmemGenericAllocationHandle alloc_handle;
        OC_CU_CHECK(cuMemCreate(&alloc_handle, aligned_size, &prop, 0));
        OC_CU_CHECK(cuMemMap(ptr, aligned_size, 0, alloc_handle, 0));

        CUmemAccessDesc access_desc = {};
        access_desc.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
        access_desc.location.id = cu_device_;
        access_desc.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
        OC_CU_CHECK(cuMemSetAccess(ptr, aligned_size, &access_desc, 1));

        memory_guard_.with_lock([&] {
            exported_resources[static_cast<handle_ty>(ptr)] = {alloc_handle, aligned_size};
        });
        *handle = static_cast<handle_ty>(ptr);
    }
}

void CUDADevice::memory_free(handle_ty *handle) {
    CUmemGenericAllocationHandle alloc_handle = 0;
    size_t size = 0;
    bool found = false;

    memory_guard_.with_lock([&] {
        auto iter = exported_resources.find(*handle);
        if (iter != exported_resources.cend()) {
            exported_resources.erase(iter);
            found = true;
        }
    });

    if (found) {
        OC_CU_CHECK(cuMemUnmap(*handle, size));

        OC_CU_CHECK(cuMemAddressFree(*handle, size));

        OC_CU_CHECK(cuMemRelease(alloc_handle));
    } else {
        OC_CU_CHECK(cuMemFree(*handle));
    }
}

uint64_t CUDADevice::get_aligned_memory_size(handle_ty handle) const {
    return memory_guard_.with_lock([&]() -> size_t {
        auto iter = exported_resources.find(handle);
        if (iter != exported_resources.cend()) {
            return iter->second.size;
        }
        return 0ull;
    });
}

handle_ty CUDADevice::create_buffer(size_t size, const string &desc, bool exported) noexcept {
    OC_ASSERT(size > 0);
    if (desc == "") {
        OC_WARNING("buffer is no description");
    }
    return use_context([&] {
        handle_ty handle{};
        memory_allocate(&handle, size, exported);
        MemoryStats::instance().on_buffer_allocate(handle, size, desc);
        return handle;
    });
}

namespace detail {
void context_log_cb(unsigned int level, const char *tag, const char *message, void * /*cbdata */) {
    std::cerr << "[" << std::setw(2) << level << "][" << std::setw(12) << tag << "]: " << message << "\n";
}
}// namespace detail

void CUDADevice::init_optix_context() noexcept {
    if (optix_device_context_) {
        return;
    }
    use_context([&] {
        OC_CU_CHECK(cuMemFree(0));
        OC_OPTIX_CHECK(optixInit());

        OptixDeviceContextOptions ctx_options = {};
#ifndef NDEBUG
        ctx_options.logCallbackLevel = 4;// status/progress
#else
        ctx_options.logCallbackLevel = 2;// error
#endif
        ctx_options.logCallbackFunction = detail::context_log_cb;
#if (OPTIX_VERSION >= 70200)
        ctx_options.validationMode = OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_OFF;
#endif
        CUcontext cu_context = nullptr;
        OC_OPTIX_CHECK(optixDeviceContextCreate(cu_context, &ctx_options, &optix_device_context_));
    });
}

handle_ty CUDADevice::create_stream() noexcept {
    return use_context([&] {
        CUDAStream *stream = ocarina::new_with_allocator<CUDAStream>(this);
        return reinterpret_cast<handle_ty>(stream);
    });
}

handle_ty CUDADevice::create_texture2d_from_external(ocarina::uint handle,
                                                     const string &desc) noexcept {
    return use_context([&] {
        auto texture = ocarina::new_with_allocator<CUDATexture2D>(this, handle);
        MemoryStats::instance().on_tex_allocate(reinterpret_cast<handle_ty>(texture),
                                                texture->resolution(),
                                                texture->pixel_storage(), desc);
        return reinterpret_cast<handle_ty>(texture);
    });
}

handle_ty CUDADevice::create_texture2d(ocarina::uint2 res, ocarina::PixelStorage pixel_storage,
                                       ocarina::uint level_num, const std::string &desc) noexcept {
    return use_context([&] {
        auto texture = ocarina::new_with_allocator<CUDATexture2D>(this, make_uint3(res, 1),
                                                                  pixel_storage, level_num);
        MemoryStats::instance().on_tex_allocate(reinterpret_cast<handle_ty>(texture),
                                                make_uint3(res, 1),
                                                pixel_storage, desc);
        return reinterpret_cast<handle_ty>(texture);
    });
}

handle_ty CUDADevice::create_texture3d(uint3 res, PixelStorage pixel_storage,
                                       uint level_num,
                                       const string &desc) noexcept {
    return use_context([&] {
        auto texture = ocarina::new_with_allocator<CUDATexture3D>(this, res, pixel_storage, level_num);
        MemoryStats::instance().on_tex_allocate(reinterpret_cast<handle_ty>(texture),
                                                res, pixel_storage, desc);
        return reinterpret_cast<handle_ty>(texture);
    });
}

handle_ty CUDADevice::create_texture_from_external(ocarina::uint tex_handle) noexcept {
    CUarray handle;
    CUgraphicsResource shared_handle;
    return use_context([&] {
        OC_CU_CHECK(cuGraphicsGLRegisterImage(addressof(shared_handle), tex_handle, GL_TEXTURE_2D,
                                              CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD));
        OC_CU_CHECK(cuGraphicsMapResources(1, addressof(shared_handle), 0));
        OC_CU_CHECK(cuGraphicsSubResourceGetMappedArray((&handle), shared_handle, 0, 0));
        auto ret = reinterpret_cast<handle_ty>(handle);
        shared_handle_map_.insert(make_pair(ret, shared_handle));
        return ret;
    });
}

handle_ty CUDADevice::create_shader(const Function &function) noexcept {
    CUDACompiler compiler(this);
    ocarina::string ptx = compiler.compile(function, compute_capability_);

    auto ptr = use_context([&] {
        auto shader = CUDAShader::create(this, ptx, function);
        return reinterpret_cast<handle_ty>(shader);
    });

    return ptr;
}

handle_ty CUDADevice::create_mesh(const MeshParams &params) noexcept {
    auto ret = new_with_allocator<CUDAMesh>(this, params);
    return reinterpret_cast<handle_ty>(ret);
}

void CUDADevice::destroy_mesh(handle_ty handle) noexcept {
    ocarina::delete_with_allocator(reinterpret_cast<CUDAMesh *>(handle));
}

handle_ty CUDADevice::create_bindless_array() noexcept {
    auto ret = new_with_allocator<CUDABindlessArray>(this);
    return reinterpret_cast<handle_ty>(ret);
}

void CUDADevice::destroy_bindless_array(handle_ty handle) noexcept {
    ocarina::delete_with_allocator(reinterpret_cast<CUDABindlessArray *>(handle));
}

handle_ty CUDADevice::create_buffer_from_external(ocarina::uint buffer_handle) noexcept {
    CUdeviceptr handle;
    CUgraphicsResource shared_handle;
    size_t size;
    return use_context([&] {
        OC_CU_CHECK(cuGraphicsGLRegisterImage(addressof(shared_handle), buffer_handle, GL_TEXTURE_2D,
                                              CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD));
        OC_CU_CHECK(cuGraphicsMapResources(1, addressof(shared_handle), 0));

        OC_CU_CHECK(cuGraphicsResourceGetMappedPointer(&handle, &size, shared_handle));
        handle_ty ret = handle;
        shared_handle_map_.insert(make_pair(ret, shared_handle));
        return ret;
    });
}

void CUDADevice::register_shared_resource(ocarina::handle_ty handle, CUgraphicsResource resource) {
    if (shared_handle_map_.contains(handle)) {
        return;
    }
    shared_handle_map_.insert(make_pair(handle, resource));
}

void CUDADevice::unregister_shared_resource(ocarina::handle_ty handle) {
    shared_handle_map_.erase(handle);
}

bool CUDADevice::is_external_resource(ocarina::handle_ty handle) const {
    return shared_handle_map_.contains(handle);
}

CUgraphicsResource CUDADevice::get_shared_resource(ocarina::handle_ty handle) const {
    return shared_handle_map_.at(handle);
}

//void CUDADevice::register_external_tex_to_buffer(handle_ty *handle, ocarina::uint tex_handle) noexcept {
//
//    use_context([&] {
//        CUgraphicsResource &shared_handle = shared_handle_map_[*handle];
//        OC_CU_CHECK(cuGraphicsGLRegisterImage(addressof(shared_handle), tex_handle,GL_TEXTURE_2D,
//                                              CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD));
//        OC_CU_CHECK(cuGraphicsMapResources(1, addressof(shared_handle), 0));
//        size_t buffer_size = 0u;
//        CUdeviceptr c_udeviceptr;
//        CUarray cu_array;
//        OC_CU_CHECK(cuGraphicsSubResourceGetMappedArray(&cu_array, shared_handle, 0, 0));
//        CUDA_ARRAY3D_DESCRIPTOR retrievedDesc;
//        OC_CU_CHECK(cuArray3DGetDescriptor(&retrievedDesc, cu_array));
//        int i = 0;
//    });
//}
//
//void CUDADevice::mapping_external_tex_to_buffer(handle_ty *handle, ocarina::uint tex_handle) noexcept {
//
//    //    use_context([&] {
//    //        CUgraphicsResource &shared_handle = shared_handle_map_[*handle];
//    //        OC_CU_CHECK(cuGraphicsMapResources(1, addressof(shared_handle), 0));
//    //        size_t buffer_size = 0u;
//    //        OC_CU_CHECK(cuGraphicsResourceGetMappedPointer(reinterpret_cast<CUdeviceptr *>(handle),
//    //                                                       &buffer_size,shared_handle));
//    //        int i = 0;
//    //    });
//}
//
//void CUDADevice::register_shared_buffer(void *&shared_handle, ocarina::uint &gl_handle) noexcept {
//    if (shared_handle != nullptr) {
//        return;
//    }
//    OC_CUDA_CHECK(cudaGraphicsGLRegisterBuffer(reinterpret_cast<cudaGraphicsResource_t *>(&shared_handle),
//                                               gl_handle, CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD));
//}
//
//void CUDADevice::register_shared_tex(void *&shared_handle, ocarina::uint &gl_handle) noexcept {
//    if (shared_handle != nullptr) {
//        return;
//    }
//    OC_CUDA_CHECK(cudaGraphicsGLRegisterImage(reinterpret_cast<cudaGraphicsResource_t *>(&shared_handle),
//                                              gl_handle, GL_TEXTURE_2D,
//                                              CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD));
//}
//
//void CUDADevice::mapping_shared_buffer(void *&shared_handle, handle_ty &handle) noexcept {
//    OC_CUDA_CHECK(cudaGraphicsMapResources(1, reinterpret_cast<cudaGraphicsResource_t *>(&shared_handle)));
//    size_t buffer_size = 0u;
//    OC_CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(
//        reinterpret_cast<void **>(&handle),
//        &buffer_size,
//        reinterpret_cast<cudaGraphicsResource_t>(shared_handle)));
//}
//
//void CUDADevice::mapping_shared_tex(void *&shared_handle, handle_ty &handle) noexcept {
//    OC_CUDA_CHECK(cudaGraphicsMapResources(1, reinterpret_cast<cudaGraphicsResource_t *>(&shared_handle)));
//    OC_CUDA_CHECK(cudaGraphicsSubResourceGetMappedArray(reinterpret_cast<cudaArray_t *>(handle),
//                                                        reinterpret_cast<cudaGraphicsResource_t>(shared_handle), 0, 0));
//}
//
//void CUDADevice::unmapping_shared(void *&shared_handle) noexcept {
//    OC_CUDA_CHECK(cudaGraphicsUnmapResources(1,
//                                             reinterpret_cast<cudaGraphicsResource_t *>(&shared_handle)));
//}
//
//void CUDADevice::unregister_shared(void *&shared_handle) noexcept {
//    if (shared_handle == nullptr) {
//        return;
//    }
//    OC_CUDA_CHECK(cudaGraphicsUnregisterResource(reinterpret_cast<cudaGraphicsResource_t>(shared_handle)));
//}

void CUDADevice::destroy_buffer(handle_ty handle) noexcept {
    if (handle != 0) {
        use_context([&] {
            MemoryStats::instance().on_buffer_free(handle);
            memory_free(&handle);
        });
    }
}

void CUDADevice::destroy_shader(handle_ty handle) noexcept {
    ocarina::delete_with_allocator(reinterpret_cast<CUDAShader *>(handle));
}

void CUDADevice::destroy_texture3d(handle_ty handle) noexcept {
    use_context([&] {
        MemoryStats::instance().on_tex_free(handle);
        ocarina::delete_with_allocator(reinterpret_cast<CUDATexture3D *>(handle));
    });
}

void CUDADevice::destroy_texture2d(ocarina::handle_ty handle) noexcept {
    use_context([&] {
        MemoryStats::instance().on_tex_free(handle);
        ocarina::delete_with_allocator(reinterpret_cast<CUDATexture2D *>(handle));
    });
}

void CUDADevice::destroy_stream(handle_ty handle) noexcept {
    ocarina::delete_with_allocator(reinterpret_cast<CUDAStream *>(handle));
}
handle_ty CUDADevice::create_accel(AccelUsageTag usage_tag) noexcept {
    return use_context([&] {
        auto accel = new_with_allocator<OptixAccel>(this, usage_tag);
        return reinterpret_cast<handle_ty>(accel);
    });
}
void CUDADevice::destroy_accel(handle_ty handle) noexcept {
    ocarina::delete_with_allocator(reinterpret_cast<OptixAccel *>(handle));
}
CommandVisitor *CUDADevice::command_visitor() noexcept {
    return cmd_visitor_.get();
}

#if _WIN32 || _WIN64
handle_ty CUDADevice::import_handle(handle_ty handle, size_t size) {
    return use_context([&] {
        CUDA_EXTERNAL_MEMORY_HANDLE_DESC externalMemoryHandleDesc = {};
        externalMemoryHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32;
        externalMemoryHandleDesc.handle.win32.handle = reinterpret_cast<void *>(handle);
        externalMemoryHandleDesc.size = size;
        externalMemoryHandleDesc.flags = 0;

        CUexternalMemory externalMemory;
        CUresult res = cuImportExternalMemory(&externalMemory, &externalMemoryHandleDesc);
        if (res != CUDA_SUCCESS) {
            const char *error_str = nullptr;
            cuGetErrorString(res, &error_str);
            OC_ERROR_FORMAT("Failed to import external memory: {} (error code: {})",
                            error_str ? error_str : "Unknown error", static_cast<int>(res));
        }

        CUDA_EXTERNAL_MEMORY_BUFFER_DESC bufferDesc = {};
        bufferDesc.offset = 0;
        bufferDesc.size = size;
        bufferDesc.flags = 0;

        CUdeviceptr devicePtr = 0;
        res = cuExternalMemoryGetMappedBuffer(&devicePtr, externalMemory, &bufferDesc);
        if (res != CUDA_SUCCESS) {
            const char *error_str = nullptr;
            cuGetErrorString(res, &error_str);
            cuDestroyExternalMemory(externalMemory);
            OC_ERROR_FORMAT("Failed to get mapped buffer: {} (error code: {})",
                            error_str ? error_str : "Unknown error", static_cast<int>(res));
        }

        return static_cast<handle_ty>(devicePtr);
    });
}

uint64_t CUDADevice::export_handle(handle_ty handle_) {
    return use_context([&] {
        CUmemGenericAllocationHandle alloc_handle;

        memory_guard_.with_lock([&] {
            auto iter = exported_resources.find(handle_);
            if (iter == exported_resources.cend()) {
                throw std::runtime_error("Invalid handle: allocation handle not found");
            }
            alloc_handle = iter->second.handle;
        });

        void *exported_win32_handle = nullptr;
        CUresult res = cuMemExportToShareableHandle(
            &exported_win32_handle,
            alloc_handle,
            CU_MEM_HANDLE_TYPE_WIN32,
            0);

        if (res != CUDA_SUCCESS) {
            const char *error_str = nullptr;
            cuGetErrorString(res, &error_str);
            OC_ERROR_FORMAT("Failed to export shareable handle: {} (error code: {})",
                            error_str ? error_str : "Unknown error", static_cast<int>(res));
            throw std::runtime_error(std::string("Failed to export shareable handle: ") +
                                     (error_str ? error_str : "Unknown error"));
        }

        return reinterpret_cast<uint64_t>(exported_win32_handle);
    });
}
#endif

}// namespace ocarina

OC_EXPORT_API ocarina::CUDADevice *create(ocarina::RHIContext *context) {
    return ocarina::new_with_allocator<ocarina::CUDADevice>(context);
}

OC_EXPORT_API ocarina::CUDADevice *create_device(ocarina::RHIContext *context) {
    return ocarina::new_with_allocator<ocarina::CUDADevice>(context);
}

OC_EXPORT_API void destroy(ocarina::CUDADevice *device) {
    ocarina::delete_with_allocator(device);
    OC_INFO("cuda device is destroy!");
}