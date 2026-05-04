//
// Created by Zero on 06/06/2022.
//

#include "vertex_buffer.h"
#include "device.h"

namespace ocarina {
VertexBuffer::~VertexBuffer() {
}

VertexBuffer *VertexBuffer::create_vertex_buffer(Device::Impl *device) {
    VertexBuffer *buffer = device->create_vertex_buffer();
    buffer->device_ = device;
    //buffer->buffer_handle_ =  device->create_vertex_buffer(initial_data, bytes);
    return buffer;
}


void VertexBuffer::add_vertex_stream(VertexAttributeType::Enum type, uint32_t count, uint32_t stride, const void *data) {
    if (vertex_streams_[(uint8_t)type].data) {
        delete[] static_cast<uint8_t *>(vertex_streams_[(uint8_t)type].data);
    }

    if (data != nullptr)
    {
        vertex_streams_[(uint8_t)type].data = new uint8_t[count * stride];
        memcpy(vertex_streams_[(uint8_t)type].data, data, count * stride);
        if (type == VertexAttributeType::Enum::Position)
        {
            Vector3 *pos = static_cast<Vector3 *>(vertex_streams_[(uint8_t)type].data);
            int a = 0;
        }
    }
    vertex_streams_[(uint8_t)type].type = type;
    vertex_streams_[(uint8_t)type].count = count;
    vertex_streams_[(uint8_t)type].stride = stride;
    vertex_streams_[(uint8_t)type].offset = 0;

    dirty_ = true;
}

void VertexBuffer::upload_data()
{
    for (auto& stream : vertex_streams_)
    {
        if (stream.data)
        {
            upload_attribute_data(stream.type, stream.data, stream.offset);
        }
    }
    dirty_ = false;
}

}// namespace ocarina