#include "corona/kernel/memory/cache_aligned_allocator.h"

[[nodiscard]] std::size_t Corona::Kernal::Memory::align_up(std::size_t size, std::size_t alignment) {
    return (size + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] std::size_t Corona::Kernal::Memory::align_down(std::size_t size, std::size_t alignment) {
    return size & ~(alignment - 1);
}

[[nodiscard]] bool Corona::Kernal::Memory::is_aligned(void* ptr, std::size_t alignment) {
    return (reinterpret_cast<std::uintptr_t>(ptr) & (alignment - 1)) == 0;
}

[[nodiscard]] bool Corona::Kernal::Memory::is_power_of_two(std::size_t value) {
    return (value != 0) && ((value & (value - 1)) == 0);
}