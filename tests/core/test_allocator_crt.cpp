#include "allocator_crt_fixture.h"
#include "core/stl.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

bool expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
    return condition;
}

void test_shared_crt() {
    const horizon::test::FreeFunction local_free = &std::free;
    expect(horizon::test::allocator_crt_free_function() == local_free,
           "the executable and DLL resolve std::free to the same CRT function");
}

void test_cross_dll_allocations() {
    for (bool use_allocator : {false, true}) {
        for (size_t size : std::array<size_t, 4>{0, 1, 37, 1025}) {
            // The allocation contract reserves one usable byte for size zero.
            const size_t bytes = size == 0 ? 1 : size;
            auto *from_dll = static_cast<std::byte *>(
                horizon::test::allocator_crt_allocate(size, use_allocator));
            if (expect(from_dll != nullptr, "DLL allocation returns usable storage")) {
                expect(std::all_of(from_dll, from_dll + bytes,
                                   [](std::byte value) { return value == std::byte{0x52}; }),
                       "the executable reads every payload byte written in the DLL");
                std::free(from_dll);
            }

            auto *from_executable = ocarina::allocate<std::byte>(size, use_allocator);
            if (expect(from_executable != nullptr, "executable allocation returns usable storage")) {
                std::fill_n(from_executable, bytes, std::byte{0xa5});
                expect(horizon::test::allocator_crt_free(from_executable, size),
                       "the DLL reads the executable payload and frees its allocation");
            }
        }
    }
}

}// namespace

int main() {
    test_shared_crt();
    test_cross_dll_allocations();
    return failures == 0 ? 0 : 1;
}
