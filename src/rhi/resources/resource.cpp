//
// Created by Zero on 06/07/2022.
//

#include "resource.h"

namespace ocarina {

void RHIResource::_destroy() {
    if (!valid()) {
        return;
    }
    if (handle_ == 0) {
        return;
    }
    switch (tag_) {
        case Tag::BUFFER: device_->destroy_buffer(handle_); break;
        case Tag::TEXTURE3D: device_->destroy_texture3d(handle_); break;
        case Tag::TEXTURE2D: device_->destroy_texture2d(handle_); break;
        case Tag::STREAM: device_->destroy_stream(handle_); break;
        case Tag::SHADER: device_->destroy_shader(handle_); break;
        case Tag::MESH: device_->destroy_mesh(handle_); break;
        case Tag::ACCEL: device_->destroy_accel(handle_); break;
        case Tag::BINDLESS_ARRAY: device_->destroy_bindless_array(handle_); break;
    }
    handle_ = 0;
}
}// namespace ocarina