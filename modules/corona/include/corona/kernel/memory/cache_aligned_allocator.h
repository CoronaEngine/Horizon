#pragma once
#include <cstdlib>
#include <limits>
#include <new>

#include "corona/pal/cfw_platform.h"

namespace Corona::Kernal::Memory {

#if defined(__cpp_lib_hardware_interference_size)
constexpr std::size_t CacheLineSize = std::hardware_destructive_interference_size;
#else
constexpr std::size_t CacheLineSize = 64;
#endif

[[nodiscard]] std::size_t align_up(std::size_t size, std::size_t alignment);
[[nodiscard]] std::size_t align_down(std::size_t size, std::size_t alignment);
[[nodiscard]] bool is_aligned(void* ptr, std::size_t alignment);
[[nodiscard]] bool is_power_of_two(std::size_t value);

inline void* aligned_malloc(std::size_t size, std::size_t alignment) {
    if (size % alignment != 0) {
        size = ((size + alignment - 1) / alignment) * alignment;
    }

    void* ptr = nullptr;
#if _WIN32 || _WIN64
    ptr = _aligned_malloc(size, alignment);
#else
    ptr = std::aligned_alloc(alignment, size);
#endif
    return ptr;
}

inline void* aligned_free(void* ptr) {
#if _WIN32 || _WIN64
    _aligned_free(ptr);
#endif
    return nullptr;
}

template <std::size_t Value>
[[nodiscard]] constexpr bool is_power_of_two() {
    return Value != 0 && (Value & (Value - 1)) == 0;
}

template <typename T, std::size_t Alignment = CacheLineSize>
class CacheAlignedAllocator {
    static_assert(is_power_of_two<Alignment>(), "Alignment must be a power of two");

   public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = std::size_t;

    template <typename U>
    struct rebind {
        using other = CacheAlignedAllocator<U, Alignment>;
    };

    CacheAlignedAllocator() noexcept = default;

    template <typename U>
    explicit CacheAlignedAllocator(const CacheAlignedAllocator<U, Alignment>&) noexcept {}

    [[nodiscard]] pointer allocate(size_type n) {
        if (n > std::numeric_limits<size_type>::max() / sizeof(T)) {
            throw std::bad_alloc();
        }

        // Handle zero-size allocation
        if (n == 0) {
            n = 1;
        }

        const size_type allocate_bytes = n * sizeof(T);
        const size_type rounded_size = align_up(allocate_bytes, Alignment);

#if defined(CFW_COMPILER_CLANG_CL) || defined(CFW_COMPILER_MSVC)
        void* ptr = _aligned_malloc(rounded_size, Alignment);
#else
        void* ptr = std::aligned_alloc(Alignment, rounded_size);
#endif

        if (!ptr) {
            throw std::bad_alloc();
        }
        return static_cast<pointer>(ptr);
    }

    void deallocate(pointer p, size_type) noexcept {
#if defined(CFW_COMPILER_CLANG_CL) || defined(CFW_COMPILER_MSVC)
        _aligned_free(p);
#else
        std::free(p);
#endif
    }

    template <typename U, std::size_t OtherAlignment>
    [[nodiscard]] bool operator==(const CacheAlignedAllocator<U, OtherAlignment>&) const noexcept {
        return Alignment == OtherAlignment;
    }

    template <typename U, std::size_t OtherAlignment>
    [[nodiscard]] bool operator!=(const CacheAlignedAllocator<U, OtherAlignment>&) const noexcept {
        return Alignment != OtherAlignment;
    }
};

}  // namespace Corona::Kernal::Memory