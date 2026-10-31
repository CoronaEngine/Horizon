//
// Created by Zero on 09/07/2022.
//

#include "shader.h"
#include "texture.h"

namespace ocarina {

void ShaderArgumentPack::_encode_texture3d(const Texture3D &texture) noexcept {
    push_memory_block(texture.memory_block());
}

void ShaderArgumentPack::_encode_texture2d(const Texture2D &texture) noexcept {
    push_memory_block(texture.memory_block());
}

void ShaderArgumentPack::_encode_bindless_array(const BindlessArray &bindless_array) noexcept {
    push_memory_block(bindless_array.memory_block());
}

}// namespace ocarina