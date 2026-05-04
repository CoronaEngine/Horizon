//
// Created by Zero on 12/01/2023.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/managed.h"
#include "rhi/resources/bindless_array.h"
#include <cuda.h>

namespace ocarina {
class CUDADevice;
class CUDABindlessArray : public BindlessArray::Impl {

private:
    BindlessArrayDesc slot_soa_{};
    CUDADevice *device_{};
    Managed<ByteBufferDesc> buffers_;
    Managed<CUtexObject> tex3ds_;
    Managed<CUtexObject> tex2ds_;

public:
    explicit CUDABindlessArray(CUDADevice *device);

    /// for device side structure
    [[nodiscard]] const void *handle_ptr() const noexcept override {
        return &slot_soa_;
    }
    [[nodiscard]] size_t max_member_size() const noexcept override { return sizeof(CUdeviceptr); }
    [[nodiscard]] size_t data_size() const noexcept override { return sizeof(BindlessArrayDesc); }
    [[nodiscard]] size_t data_alignment() const noexcept override { return alignof(BindlessArrayDesc); }
    [[nodiscard]] CommandBatch update_slotSOA(bool async) noexcept override;

    [[nodiscard]] size_t emplace_buffer(handle_ty handle, uint offset_in_byte,
                                        size_t size_in_byte) noexcept override;
    void remove_buffer(handle_ty index) noexcept override;
    void set_buffer(ocarina::handle_ty index, ocarina::handle_ty handle, uint offset_in_byte,
                    size_t size_in_byte) noexcept override;
    [[nodiscard]] size_t buffer_num() const noexcept override;
    [[nodiscard]] size_t buffer_slot_size() const noexcept override;
    [[nodiscard]] BufferUploadCommand *upload_buffer_handles(bool async) const noexcept override;

    [[nodiscard]] size_t emplace_texture3d(handle_ty handle) noexcept override;
    [[nodiscard]] size_t emplace_texture3d(ocarina::TextureDesc desc) noexcept override;
    void remove_texture3d(handle_ty index) noexcept override;
    void set_texture3d(ocarina::handle_ty index, ocarina::handle_ty handle) noexcept override;
    void set_texture3d(ocarina::handle_ty index, ocarina::TextureDesc desc) noexcept override;
    [[nodiscard]] size_t texture3d_num() const noexcept override;
    [[nodiscard]] size_t tex3d_slot_size() const noexcept override;
    [[nodiscard]] BufferUploadCommand *upload_texture3d_handles(bool async) const noexcept override;

    [[nodiscard]] size_t emplace_texture2d(handle_ty handle) noexcept override;
    [[nodiscard]] size_t emplace_texture2d(ocarina::TextureDesc desc) noexcept override;
    void remove_texture2d(handle_ty index) noexcept override;
    void set_texture2d(ocarina::handle_ty index, ocarina::handle_ty handle) noexcept override;
    void set_texture2d(ocarina::handle_ty index, ocarina::TextureDesc desc) noexcept override;
    [[nodiscard]] size_t texture2d_num() const noexcept override;
    [[nodiscard]] size_t tex2d_slot_size() const noexcept override;
    [[nodiscard]] BufferUploadCommand *upload_texture2d_handles(bool async) const noexcept override;

    [[nodiscard]] ByteBufferDesc buffer_view(ocarina::uint index) const noexcept override;
};

}// namespace ocarina