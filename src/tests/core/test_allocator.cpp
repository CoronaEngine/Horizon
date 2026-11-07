#include "ast/function.h"
#include "core/concurrency/pool.h"
#include "core/image/image.h"
#include "core/util/string_util.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct alignas(256) Item {
    static inline std::atomic<int> live{};
    int value;

    explicit Item(int value) : value(value) { ++live; }
    ~Item() { --live; }
};

void test_aligned_allocations() {
    for (size_t alignment : std::array<size_t, 8>{1, 2, 8, 16, 32, 64, 256, 4096}) {
        // The byte count need not be a multiple of the requested alignment.
        const size_t size = alignment * 3 + 1;
        void *pointer = ocarina::detail::allocator_allocate(size, alignment);
        expect(pointer != nullptr, "allocation succeeds");
        if (pointer == nullptr) {
            continue;
        }
        expect(reinterpret_cast<uintptr_t>(pointer) % alignment == 0, "requested alignment is preserved");
        std::memset(pointer, 0x5a, size);
        ocarina::detail::allocator_deallocate(pointer, alignment);

        void *empty = ocarina::detail::allocator_allocate(0, alignment);
        expect(empty != nullptr && reinterpret_cast<uintptr_t>(empty) % alignment == 0,
               "zero-sized allocation remains non-null and aligned");
        ocarina::detail::allocator_deallocate(empty, alignment);
        ocarina::detail::allocator_deallocate(nullptr, alignment);
    }
    ocarina::detail::allocator_deallocate(nullptr, 1);

    auto *item = ocarina::new_with_allocator<Item>(17);
    expect(item->value == 17 && Item::live == 1, "exported allocation supports nontrivial objects");
    ocarina::delete_with_allocator(item);
    expect(Item::live == 0, "typed deletion runs the destructor");

    auto *crt = std::construct_at(ocarina::allocate<Item>(1, false), 23);
    expect(crt->value == 23, "explicit CRT allocation mode remains usable");
    ocarina::delete_with_allocator(crt, false);
    expect(Item::live == 0, "CRT deletion runs the destructor");
}

void test_function_temporary_lifetimes() {
    {
        ocarina::Function function{ocarina::Function::Tag::CALLABLE};
        // Grow the type-erased ownership vector and retain nontrivial, aligned objects.
        for (int i = 0; i < 80; ++i) {
            auto *item = function.create_temp_obj<Item>(i);
            expect(item->value == i && reinterpret_cast<uintptr_t>(item) % alignof(Item) == 0,
                   "Function temporaries preserve values and extended alignment");
        }
        expect(Item::live == 80, "Function owns every temporary until destruction");
    }
    expect(Item::live == 0, "type-erased Function temporaries run their original destructors");
}

void test_constructor_failure() {
    struct ConstructionFailure {};
    struct alignas(256) ThrowingItem {
        ThrowingItem() { throw ConstructionFailure{}; }
    };
    bool caught = false;
    try {
        auto *unexpected = ocarina::new_with_allocator<ThrowingItem>();
        ocarina::delete_with_allocator(unexpected);
    } catch (const ConstructionFailure &) {
        caught = true;
    }
    expect(caught, "object construction failure propagates to the caller");
}

void test_image_data_ownership() {
    const std::array<ocarina::uchar4, 4> source{
        ocarina::make_uchar4(1, 2, 3, 4), ocarina::make_uchar4(5, 6, 7, 8),
        ocarina::make_uchar4(9, 10, 11, 12), ocarina::make_uchar4(13, 14, 15, 16)};
    auto image = ocarina::Image::from_data(source.data(), ocarina::make_uint2(2u));
    expect(image.pixel_ptr<ocarina::uchar4>() != source.data(), "Image::from_data owns an independent copy");
    expect(std::memcmp(image.pixel_ptr(), source.data(), sizeof(source)) == 0,
           "Image::from_data preserves every pixel byte");
    image.pixel_ptr<ocarina::uchar4>()[0].x = 99;
    expect(source[0].x == 1, "mutating an owned image does not change its source");
    auto *pixels = image.pixel_ptr();
    ocarina::Image moved{std::move(image)};
    expect(image.pixel_ptr() == nullptr && moved.pixel_ptr() == pixels,
           "moving an image transfers ownership without reallocating");
    moved.clear();
    expect(moved.pixel_ptr() == nullptr, "clearing an image releases its array storage");

    auto empty = ocarina::Image::from_data(static_cast<const ocarina::uchar4 *>(nullptr),
                                         ocarina::make_uint2(0u));
    empty.clear();
}

void test_cross_thread_allocation_lifetimes() {
    // Both threads enter the allocator through the exported core DLL API.
    // Exercise a live allocating thread and an allocating thread that has exited.
    for (bool allocate_on_worker : {false, true}) {
        constexpr size_t block_size = 1025;
        std::array<unsigned char *, 64> blocks{};
        bool contents_preserved = true;
        auto allocate_blocks = [&] {
            for (size_t i = 0; i < blocks.size(); ++i) {
                blocks[i] = static_cast<unsigned char *>(
                    ocarina::detail::allocator_allocate(block_size, 64));
                if (blocks[i] != nullptr) {
                    std::memset(blocks[i], static_cast<int>(i), block_size);
                }
            }
        };
        auto free_blocks = [&] {
            for (size_t i = 0; i < blocks.size(); ++i) {
                auto *pointer = blocks[i];
                if (pointer == nullptr) {
                    contents_preserved = false;
                    continue;
                }
                contents_preserved &= std::all_of(pointer, pointer + block_size,
                                                  [i](unsigned char byte) { return byte == i; });
                ocarina::detail::allocator_deallocate(pointer, 64);
            }
        };
        if (allocate_on_worker) {
            std::thread worker(allocate_blocks);
            worker.join();
            free_blocks();
        } else {
            allocate_blocks();
            std::thread worker(free_blocks);
            worker.join();
        }
        expect(contents_preserved, allocate_on_worker
                                       ? "allocations survive their owning thread and can be freed elsewhere"
                                       : "another thread can read and free allocations from a live thread");
    }
}

void test_format_buffer_growth() {
    // Short strings stay in fmt's inline buffer and do not exercise our allocator.
    const std::string payload(fmt::inline_buffer_size * 8, 'x');
    const auto formatted = ocarina::format("{}:{}:{}", "prefix", payload, 17);
    expect(formatted == "prefix:" + payload + ":17",
           "formatting through the custom allocator preserves data after buffer growth");
}

void test_pool_growth_reuse_and_destruction() {
    ocarina::Pool<Item> pool;
    std::vector<Item *> items;
    for (int i = 0; i < 80; ++i) {
        auto *item = pool.create(i);
        expect(item->value == i && reinterpret_cast<uintptr_t>(item) % alignof(Item) == 0,
               "pool growth preserves object values and alignment");
        items.push_back(item);
    }
    expect(Item::live == 80, "pool creates objects across the 64-element block boundary");
    for (auto *item : items) {
        pool.recycle(item);
    }
    expect(Item::live == 0, "recycling destroys every live object");
    auto *reused = pool.create(101);
    expect(std::find(items.begin(), items.end(), reused) != items.end() && reused->value == 101,
           "recycled storage is reused and reconstructed");
    pool.recycle(reused);
}

void test_pool_concurrent_recycling() {
    ocarina::Pool<Item> pool;
    std::atomic<int> mismatches{};
    std::array<std::thread, 4> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers[worker] = std::thread([&, worker] {
            for (int i = 0; i < 512; ++i) {
                auto *item = pool.create(worker * 512 + i);
                if (item->value != worker * 512 + i) {
                    ++mismatches;
                }
                pool.recycle(item);
            }
        });
    }
    for (auto &worker : workers) {
        worker.join();
    }
    expect(mismatches == 0 && Item::live == 0, "concurrent create/recycle preserves values and lifetimes");
}

// The CTest driver runs these in separate processes. Only an intentional abort
// becomes exit code 86; access violations and unexpected returns remain failures.
int probe_allocation_failure(const char *probe) {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::signal(SIGABRT, [](int) { std::_Exit(86); });
    std::cout << "Probing " << probe << std::endl;
    const size_t excessive_count = (std::numeric_limits<size_t>::max)() / sizeof(Item) + 1;
    if (std::strcmp(probe, "--probe-allocation-failure") == 0) {
        // An impossible request exercises failure without exhausting system RAM.
        const auto size = static_cast<size_t>((std::numeric_limits<ptrdiff_t>::max)());
        void *pointer = ocarina::detail::allocator_allocate(size, 16);
        ocarina::detail::allocator_deallocate(pointer, 16);
    } else if (std::strcmp(probe, "--probe-size-overflow") == 0) {
        ocarina::deallocate(ocarina::allocate<Item>(excessive_count));
    } else if (std::strcmp(probe, "--probe-crt-size-overflow") == 0) {
        _aligned_free(ocarina::allocate<Item>(excessive_count, false));
    } else if (std::strcmp(probe, "--probe-invalid-alignment") == 0) {
        (void)ocarina::detail::allocator_allocate(32, 3);
    } else if (std::strcmp(probe, "--probe-zero-alignment") == 0) {
        (void)ocarina::detail::allocator_allocate(32, 0);
    } else if (std::strcmp(probe, "--probe-image-size-overflow") == 0) {
        [[maybe_unused]] auto image = ocarina::Image::from_data(
            static_cast<const ocarina::float4 *>(nullptr),
            ocarina::make_uint2((std::numeric_limits<ocarina::uint>::max)()));
    } else {
        std::cerr << "Unknown failure probe: " << probe << '\n';
        return 2;
    }
    std::cerr << "Expected allocation failure to abort" << std::endl;
    return 1;
}

}// namespace

int main(int argc, char **argv) {
    if (argc == 2) {
        return probe_allocation_failure(argv[1]);
    }
    if (argc != 1) {
        std::cerr << "Usage: test-core-allocator [--probe-<failure-case>]\n";
        return 2;
    }
    test_aligned_allocations();
    test_function_temporary_lifetimes();
    test_constructor_failure();
    test_image_data_ownership();
    test_cross_thread_allocation_lifetimes();
    test_format_buffer_growth();
    test_pool_growth_reuse_and_destruction();
    test_pool_concurrent_recycling();
    return failures == 0 ? 0 : 1;
}
