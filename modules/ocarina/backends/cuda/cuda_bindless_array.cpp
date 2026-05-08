//
// Created by Zero on 12/01/2023.
//

#include "cuda_bindless_array.h"
#include "cuda_device.h"
#include "core/util/util.h"

namespace ocarina {

CUDABindlessArray::CUDABindlessArray(CUDADevice *device)
    : device_(device), buffers_(device, c_max_slot_num, "CUDABindlessArray::buffers_"),
      tex3ds_(device, c_max_slot_num, "CUDABindlessArray::tex3ds_"),
      tex2ds_(device, c_max_slot_num, "CUDABindlessArray::tex2ds_") {
    slot_soa_.buffer_slot = buffers_.handle();
    slot_soa_.tex3d_slot = tex3ds_.handle();
    slot_soa_.tex2d_slot = tex2ds_.handle();
}

CommandBatch CUDABindlessArray::update_slotSOA(bool async) noexcept {
    CommandBatch ret;
#define OC_MAKE_REALLOCATE_CMD(arg)                                                            \
    if (buffers_.device_buffer().size() > c_max_slot_num) {                                    \
        append(ret, arg##s_.device_buffer().reallocate(buffers_.host_buffer().size(), async)); \
        ret.push_back(HostFunctionCommand::create(async, [&]() {                               \
            slot_soa_.arg##_slot = arg##s_.handle();                                           \
        }));                                                                                   \
    }

    OC_MAKE_REALLOCATE_CMD(buffer)
    OC_MAKE_REALLOCATE_CMD(tex2d)
    OC_MAKE_REALLOCATE_CMD(tex3d)

#undef OC_MAKE_REALLOCATE_CMD
    return ret;
}

namespace detail {
template<typename T>
void remove_by_index(vector<T> &v, handle_ty index) noexcept {
    auto iter = v.begin();
    for (int i = 0; i < v.size(); ++i, ++iter) {
        if (i == index) {
            v.erase(iter);
            return;
        }
    }
}
}// namespace detail

size_t CUDABindlessArray::emplace_buffer(handle_ty handle, uint offset_in_byte, size_t size_in_byte) noexcept {
    auto ret = buffers_.host_buffer().size();
    buffers_.emplace_back(reinterpret_cast<std::byte *>(handle), offset_in_byte, size_in_byte);
    OC_ERROR_IF(ret >= c_max_slot_num, ocarina::format("slot_size is {}, buffer_num is {}", c_max_slot_num, ret));
    return ret;
}

void CUDABindlessArray::remove_buffer(handle_ty index) noexcept {
    detail::remove_by_index(buffers_.host_buffer(), index);
}

void CUDABindlessArray::set_buffer(ocarina::handle_ty index, ocarina::handle_ty handle,
                                   uint offset_in_byte, size_t size_in_byte) noexcept {
    OC_ASSERT(index < buffers_.host_buffer().size());
    buffers_.host_buffer().at(index) = {reinterpret_cast<std::byte *>(handle),
                                        offset_in_byte, size_in_byte};
}

size_t CUDABindlessArray::buffer_num() const noexcept {
    return buffers_.host_buffer().size();
}

size_t CUDABindlessArray::buffer_slot_size() const noexcept {
    return sizeof(ByteBufferDesc) * c_max_slot_num;
}

BufferUploadCommand *CUDABindlessArray::upload_buffer_handles(bool async) const noexcept {
    static const ByteBufferDesc empty_desc{};
    auto count = buffers_.host_buffer().size();
    if (count == 0u) {
        return buffers_.device_buffer().view(0u, 1u).upload(&empty_desc, async);
    }
    return buffers_.device_buffer().view(0u, count).upload(buffers_.host_buffer().data(), async);
}

size_t CUDABindlessArray::emplace_texture3d(handle_ty handle) noexcept {
    auto ret = tex3ds_.host_buffer().size();
    tex3ds_.push_back(handle);
    OC_ERROR_IF(ret >= c_max_slot_num, ocarina::format("slot_size is {}, tex_num is {}", c_max_slot_num, ret));
    return ret;
}

size_t CUDABindlessArray::emplace_texture3d(ocarina::TextureDesc desc) noexcept {
    auto ret = tex3ds_.host_buffer().size();
//    tex3ds_.push_back(handle);
    OC_ERROR_IF(ret >= c_max_slot_num, ocarina::format("slot_size is {}, tex_num is {}", c_max_slot_num, ret));
    return ret;
}

void CUDABindlessArray::remove_texture3d(handle_ty index) noexcept {
    detail::remove_by_index(tex3ds_.host_buffer(), index);
}

void CUDABindlessArray::set_texture3d(ocarina::handle_ty index, ocarina::handle_ty handle) noexcept {
    OC_ASSERT(index < tex3ds_.host_buffer().size());
    tex3ds_.host_buffer().at(index) = handle;
}

void CUDABindlessArray::set_texture3d(ocarina::handle_ty index, ocarina::TextureDesc desc) noexcept {
    OC_ASSERT(index < tex3ds_.host_buffer().size());
}

size_t CUDABindlessArray::texture3d_num() const noexcept {
    return tex3ds_.host_buffer().size();
}

size_t CUDABindlessArray::tex3d_slot_size() const noexcept {
    return sizeof(CUtexObject) * c_max_slot_num;
}

BufferUploadCommand *CUDABindlessArray::upload_texture3d_handles(bool async) const noexcept {
    static const CUtexObject empty_handle = 0u;
    auto count = tex3ds_.host_buffer().size();
    if (count == 0u) {
        return tex3ds_.device_buffer().view(0u, 1u).upload(&empty_handle, async);
    }
    return tex3ds_.device_buffer().view(0u, count).upload(tex3ds_.host_buffer().data(), async);
}

size_t CUDABindlessArray::emplace_texture2d(handle_ty handle) noexcept {
    auto ret = tex2ds_.host_buffer().size();
    tex2ds_.push_back(handle);
    OC_ERROR_IF(ret >= c_max_slot_num, ocarina::format("slot_size is {}, tex_num is {}", c_max_slot_num, ret));
    return ret;
}

size_t CUDABindlessArray::emplace_texture2d(ocarina::TextureDesc desc) noexcept {
    auto ret = tex2ds_.host_buffer().size();
//    tex2ds_.push_back(handle);
//    OC_ERROR_IF(ret >= c_max_slot_num, ocarina::format("slot_size is {}, tex_num is {}", c_max_slot_num, ret));
    return ret;
}

void CUDABindlessArray::remove_texture2d(handle_ty index) noexcept {
    detail::remove_by_index(tex2ds_.host_buffer(), index);
}

void CUDABindlessArray::set_texture2d(ocarina::handle_ty index, ocarina::handle_ty handle) noexcept {
    OC_ASSERT(index < tex2ds_.host_buffer().size());
    tex2ds_.host_buffer().at(index) = handle;
}

void CUDABindlessArray::set_texture2d(ocarina::handle_ty index, ocarina::TextureDesc desc) noexcept {
    OC_ASSERT(index < tex2ds_.host_buffer().size());
}

size_t CUDABindlessArray::tex2d_slot_size() const noexcept {
    return sizeof(CUtexObject) * c_max_slot_num;
}

size_t CUDABindlessArray::texture2d_num() const noexcept {
    return tex2ds_.host_buffer().size();
}

BufferUploadCommand *CUDABindlessArray::upload_texture2d_handles(bool async) const noexcept {
    static const CUtexObject empty_handle = 0u;
    auto count = tex2ds_.host_buffer().size();
    if (count == 0u) {
        return tex2ds_.device_buffer().view(0u, 1u).upload(&empty_handle, async);
    }
    return tex2ds_.device_buffer().view(0u, count).upload(tex2ds_.host_buffer().data(), async);
}

ByteBufferDesc CUDABindlessArray::buffer_view(ocarina::uint index) const noexcept {
    return buffers_.host_buffer().at(index);
}

}// namespace ocarina