//
// Created by Zero on 06/08/2022.
//

#include "cuda_texture.h"
#include "util.h"
#include "cuda_device.h"
#include "cuda_runtime_api.h"
#include <cudaGL.h>

namespace ocarina {

CUDATexture::CUDATexture(CUDADevice *device, uint3 res, PixelStorage pixel_storage, uint level_num)
    : device_(device), res_(res), level_num_(level_num) {
    descriptor_.pixel_storage = pixel_storage;
}

CUDATexture::~CUDATexture() {
    if (is_external()) {
        unmapping_external_resource();
    } else {
        OC_CU_CHECK(cuArrayDestroy(array_));
    }
    OC_CU_CHECK(cuTexObjectDestroy(descriptor_.texture));
    OC_CU_CHECK(cuSurfObjectDestroy(descriptor_.surface));
}

size_t CUDATexture::data_size() const noexcept { return CUDADevice::size(Type::Tag::TEXTURE3D); }
size_t CUDATexture::data_alignment() const noexcept { return CUDADevice::alignment(Type::Tag::TEXTURE3D); }
size_t CUDATexture::max_member_size() const noexcept { return sizeof(handle_ty); }

bool CUDATexture::is_external() const noexcept {
    return device_->is_external_resource(reinterpret_cast<handle_ty>(this));
}

void CUDATexture::unmapping_external_resource() {
    CUgraphicsResource resource = device_->get_shared_resource(reinterpret_cast<handle_ty>(this));
    device_->unregister_shared_resource(reinterpret_cast<handle_ty>(this));
    OC_CU_CHECK(cuGraphicsUnmapResources(1, &resource, nullptr));
    OC_CU_CHECK(cuGraphicsUnregisterResource(resource));
}

void CUDATexture::init_by_array(CUarray array) {
    CUDA_RESOURCE_DESC res_desc{};
    res_desc.resType = CU_RESOURCE_TYPE_ARRAY;
    res_desc.res.array.hArray = array;
    res_desc.flags = 0;
    CUDA_TEXTURE_DESC tex_desc{};
    tex_desc.addressMode[0] = CU_TR_ADDRESS_MODE_MIRROR;
    tex_desc.addressMode[1] = CU_TR_ADDRESS_MODE_MIRROR;
    tex_desc.addressMode[2] = CU_TR_ADDRESS_MODE_MIRROR;
    tex_desc.maxAnisotropy = 2;
    tex_desc.maxMipmapLevelClamp = 9;
    tex_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
    tex_desc.flags = CU_TRSF_NORMALIZED_COORDINATES;

    OC_CU_CHECK(cuSurfObjectCreate(&descriptor_.surface, &res_desc));
    OC_CU_CHECK(cuTexObjectCreate(&descriptor_.texture, &res_desc, &tex_desc, nullptr));
}

const TextureDesc &CUDATexture::descriptor() const noexcept {
    return descriptor_;
}

void CUDATexture2D::init() noexcept {
    CUDA_ARRAY_DESCRIPTOR array_desc{};
    array_desc.Width = res_.x;
    array_desc.Height = res_.y;
    switch (descriptor_.pixel_storage) {
        case PixelStorage::BYTE1:
            array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
            array_desc.NumChannels = 1;
            break;
        case PixelStorage::BYTE2:
            array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
            array_desc.NumChannels = 2;
            break;
        case PixelStorage::BYTE4:
            array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
            array_desc.NumChannels = 4;
            break;
        case PixelStorage::FLOAT1:
            array_desc.Format = CU_AD_FORMAT_FLOAT;
            array_desc.NumChannels = 1;
            break;
        case PixelStorage::FLOAT2:
            array_desc.Format = CU_AD_FORMAT_FLOAT;
            array_desc.NumChannels = 2;
            break;
        case PixelStorage::FLOAT4:
            array_desc.Format = CU_AD_FORMAT_FLOAT;
            array_desc.NumChannels = 4;
            break;
        default: OC_ASSERT(0); break;
    }

    OC_CU_CHECK(cuArrayCreate(&array_, &array_desc));
    init_by_array(array_);
}

void CUDATexture3D::init() noexcept {
    CUDA_ARRAY3D_DESCRIPTOR array_desc{};
    array_desc.Width = res_.x;
    array_desc.Height = res_.y;
    array_desc.Depth = res_.z;
    switch (descriptor_.pixel_storage) {
        case PixelStorage::BYTE1:
            array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
            array_desc.NumChannels = 1;
            break;
        case PixelStorage::BYTE2:
            array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
            array_desc.NumChannels = 2;
            break;
        case PixelStorage::BYTE4:
            array_desc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
            array_desc.NumChannels = 4;
            break;
        case PixelStorage::FLOAT1:
            array_desc.Format = CU_AD_FORMAT_FLOAT;
            array_desc.NumChannels = 1;
            break;
        case PixelStorage::FLOAT2:
            array_desc.Format = CU_AD_FORMAT_FLOAT;
            array_desc.NumChannels = 2;
            break;
        case PixelStorage::FLOAT4:
            array_desc.Format = CU_AD_FORMAT_FLOAT;
            array_desc.NumChannels = 4;
            break;
        default: OC_ASSERT(0); break;
    }

    OC_CU_CHECK(cuArray3DCreate(&array_, &array_desc));
    init_by_array(array_);
}

CUDATexture3D::CUDATexture3D(CUDADevice *device, uint3 res, PixelStorage pixel_storage, uint level_num)
    : CUDATexture(device, res, pixel_storage, level_num) {
    init();
}

CUDATexture2D::CUDATexture2D(CUDADevice *device, uint3 res, PixelStorage pixel_storage, uint level_num)
    : CUDATexture(device, res, pixel_storage, level_num) {
    init();
}

CUDATexture2D::CUDATexture2D(ocarina::CUDADevice *device, ocarina::uint external_handle)
    : CUDATexture(device) {
    init_with_external_handle(external_handle);
}

void CUDATexture2D::init_with_external_handle(ocarina::uint external_handle) noexcept {
    CUgraphicsResource res;
    OC_CU_CHECK(cuGraphicsGLRegisterImage(addressof(res), external_handle, GL_TEXTURE_2D,
                                          CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD));
    OC_CU_CHECK(cuGraphicsMapResources(1, addressof(res), 0));
    OC_CU_CHECK(cuGraphicsSubResourceGetMappedArray((&array_), res, 0, 0));
    CUDA_ARRAY_DESCRIPTOR array_desc{};
    OC_CU_CHECK(cuArrayGetDescriptor(&array_desc, array_));
    descriptor_.pixel_storage = PixelStorage::FLOAT4;
    res_ = make_uint3(array_desc.Width, array_desc.Height, 1);
    device_->register_shared_resource(reinterpret_cast<handle_ty>(this), res);
    init_by_array(array_);
}

}// namespace ocarina