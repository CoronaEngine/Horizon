#include "core/concurrency/pool.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
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

void test_aligned_allocations_and_type_erased_free() {
    for (size_t alignment : std::array<size_t, 7>{1, 2, 8, 16, 32, 64, 256}) {
        const size_t size = alignment * 3;
        void *pointer = ocarina::detail::allocator_allocate(size, alignment);
        expect(pointer != nullptr, "allocation succeeds");
        if (pointer == nullptr) {
            continue;
        }
        expect(reinterpret_cast<uintptr_t>(pointer) % alignment == 0, "requested alignment is preserved");
        std::memset(pointer, 0x5a, size);
        ocarina::detail::allocator_deallocate(pointer, 1);
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

}// namespace

int main() {
    test_aligned_allocations_and_type_erased_free();
    test_pool_growth_reuse_and_destruction();
    test_pool_concurrent_recycling();
    return failures == 0 ? 0 : 1;
}
