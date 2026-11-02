//
// Created by Zero on 06/06/2022.
//

#include "index_buffer.h"
#include "device.h"

namespace ocarina {
IndexBuffer::~IndexBuffer() {
}

IndexBuffer *IndexBuffer::create_index_buffer(Device::Impl *device, void *initial_data, uint32_t indices_count, bool bit16) {
    IndexBuffer *buffer = device->create_index_buffer(initial_data, indices_count, bit16);
    buffer->device_ = device;
    return buffer;
}


}// namespace ocarina