#include "allocator_crt_fixture.h"
#include "core/stl.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>

namespace horizon::test {

void *allocator_crt_allocate(size_t size, bool use_allocator) noexcept {
    auto *pointer = ocarina::allocate<std::byte>(size, use_allocator);
    std::fill_n(pointer, size == 0 ? 1 : size, std::byte{0x52});
    return pointer;
}

bool allocator_crt_free(void *pointer, size_t size) noexcept {
    auto *bytes = static_cast<std::byte *>(pointer);
    const size_t count = size == 0 ? 1 : size;
    const bool payload_preserved = std::all_of(bytes, bytes + count,
                                             [](std::byte value) { return value == std::byte{0xa5}; });
    std::free(pointer);
    return payload_preserved;
}

FreeFunction allocator_crt_free_function() noexcept {
    return &std::free;
}

}// namespace horizon::test
