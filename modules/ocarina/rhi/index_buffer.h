//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "dsl/api/func.h"
#include "dsl/types/rtx.h"
#include "core/image/image_base.h"
#include "core/concepts.h"
#include "core/concurrency/thread_pool.h"
#include "params.h"
#include "graphics_descriptions.h"
#include "device.h"

namespace ocarina {

class OC_RHI_API IndexBuffer {
public:
    IndexBuffer() = default;
    virtual ~IndexBuffer();

    static IndexBuffer *create_index_buffer(Device::Impl *device, void *initial_data, uint32_t indices_count, bool bit16 = true);

    //OC_MAKE_MEMBER_GETTER(buffer_handle, );

    void set_indices(std::vector<uint16_t>&& indices) {
        indices_ = std::move(indices);
    }

    uint32_t get_index_count() const {
        return static_cast<uint32_t>(indices_.size());
    }

    bool is_16_bit() const {
        return bit16_;
    }
protected:
    //std::vector<uint32_t> indices32_;
    std::vector<uint16_t> indices_;
    //handle_ty buffer_handle_ = InvalidUI32;
    Device::Impl *device_ = nullptr;
    bool bit16_ = true; // if true, use 16-bit indices, otherwise use 32-bit indices
};

}// namespace ocarina