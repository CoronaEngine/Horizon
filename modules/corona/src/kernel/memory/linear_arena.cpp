#include "corona/kernel/memory/linear_arena.h"

#include <cstring>
#include <utility>

namespace Corona::Kernal::Memory {

LinearArena::LinearArena(std::size_t capacity) : capacity_(capacity) {
    if (capacity_ > 0) {
        buffer_ = static_cast<std::byte*>(aligned_malloc(capacity_, CacheLineSize));
        if (!buffer_) {
            capacity_ = 0;
        }
    }
}

LinearArena::~LinearArena() {
    if (buffer_) {
        aligned_free(buffer_);
        buffer_ = nullptr;
    }
    capacity_ = 0;
    offset_ = 0;
}

LinearArena::LinearArena(LinearArena&& other) noexcept
    : buffer_(other.buffer_), capacity_(other.capacity_), offset_(other.offset_) {
    other.buffer_ = nullptr;
    other.capacity_ = 0;
    other.offset_ = 0;
}

LinearArena& LinearArena::operator=(LinearArena&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        if (buffer_) {
            aligned_free(buffer_);
        }

        // 移动资源
        buffer_ = other.buffer_;
        capacity_ = other.capacity_;
        offset_ = other.offset_;

        // 清空源对象
        other.buffer_ = nullptr;
        other.capacity_ = 0;
        other.offset_ = 0;
    }
    return *this;
}

void* LinearArena::allocate(std::size_t size, std::size_t alignment) noexcept {
    if (size == 0 || !buffer_) {
        return nullptr;
    }

    // 计算对齐后的偏移
    std::size_t aligned_offset = align_up(offset_, alignment);

    // 检查是否有足够空间
    if (aligned_offset + size > capacity_) {
        return nullptr;  // 空间不足
    }

    void* ptr = buffer_ + aligned_offset;
    offset_ = aligned_offset + size;
    return ptr;
}

void LinearArena::reset() noexcept { offset_ = 0; }

}  // namespace Corona::Kernal::Memory
