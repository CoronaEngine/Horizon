#include "corona/kernel/memory/frame_arena.h"

namespace Corona::Kernal::Memory {

FrameArena::FrameArena(std::size_t buffer_size) : arenas_{LinearArena(buffer_size), LinearArena(buffer_size)} {}

void FrameArena::swap() noexcept { current_index_ = 1 - current_index_; }

void FrameArena::begin_frame() noexcept {
    // 重置当前缓冲区
    arenas_[current_index_].reset();
}

void FrameArena::end_frame() noexcept {
    // 交换缓冲区
    swap();
}

void* FrameArena::allocate(std::size_t size, std::size_t alignment) noexcept {
    return arenas_[current_index_].allocate(size, alignment);
}

void FrameArena::reset_all() noexcept {
    arenas_[0].reset();
    arenas_[1].reset();
    current_index_ = 0;
}

}  // namespace Corona::Kernal::Memory
