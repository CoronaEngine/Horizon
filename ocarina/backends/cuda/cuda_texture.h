//
// Created by Zero on 06/08/2022.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/texture.h"
#include "driver_types.h"
#include <cuda.h>

namespace ocarina {
class CUDADevice;

class CUDATexture : public Texture::Impl {
protected:
    mutable TextureDesc descriptor_;
    CUDADevice *device_{};
    uint level_num_{1u};
    uint3 res_{};
    CUarray array_{};
    CUDATexture(CUDADevice *device, uint3 res,
                PixelStorage pixel_storage, uint level_num);
    CUDATexture(CUDADevice *device) : device_(device) {}

public:
    [[nodiscard]] uint3 resolution() const noexcept override { return res_; }

    [[nodiscard]] const TextureDesc &descriptor() const noexcept override;
    [[nodiscard]] handle_ty tex_handle() const noexcept override {
        return descriptor_.texture;
    }
    [[nodiscard]] const void *handle_ptr() const noexcept override {
        return &descriptor_;
    }
    [[nodiscard]] handle_ty array_handle() const noexcept override {
        return reinterpret_cast<handle_ty>(array_);
    }
    [[nodiscard]] size_t data_size() const noexcept override;
    [[nodiscard]] size_t data_alignment() const noexcept override;
    [[nodiscard]] size_t max_member_size() const noexcept override;
    [[nodiscard]] PixelStorage pixel_storage() const noexcept override { return descriptor_.pixel_storage; }
    void init_by_array(CUarray array);
    void unmapping_external_resource();
    [[nodiscard]] bool is_external() const noexcept override;
    ~CUDATexture() override;
};

class CUDATexture2D : public CUDATexture {
public:
    CUDATexture2D(CUDADevice *device, uint3 res, PixelStorage pixel_storage, uint level_num);
    CUDATexture2D(CUDADevice *device,uint external_handle);
    void init() noexcept;
    void init_with_external_handle(uint external_handle) noexcept;
};

class CUDATexture3D : public CUDATexture {
public:
    CUDATexture3D(CUDADevice *device, uint3 res, PixelStorage pixel_storage, uint level_num);
    void init()noexcept;
};
}// namespace ocarina