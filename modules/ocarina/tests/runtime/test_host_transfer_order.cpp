#include "tests/backend_test.h"
#include "rhi/common.h"
#include "rhi/context.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

using namespace ocarina;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "host-transfer-order: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

// Hold the callback while the submitting thread can enqueue later transfers.
// CUDA may stage pageable uploads synchronously, so an independent thread also
// releases this gate. This tests data ordering, not upload submission latency.
struct CallbackGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered{};
    bool released{};
    std::atomic<bool> timed_out{};

    void wait() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(10), [&] { return released; }))
            timed_out = true;
    }

    void release() {
        {
            std::lock_guard lock(mutex);
            released = true;
        }
        changed.notify_all();
    }

    std::thread independent_release() {
        return std::thread([this] {
            std::unique_lock lock(mutex);
            if (!changed.wait_for(lock, std::chrono::seconds(10), [&] { return entered || released; })) {
                timed_out = true;
            } else if (!released) {
                changed.wait_for(lock, std::chrono::milliseconds(50), [&] { return released; });
            }
            released = true;
            lock.unlock();
            changed.notify_all();
        });
    }
};

template<typename T>
bool identical(const std::vector<T> &actual, const std::vector<T> &expected) {
    return actual.size() == expected.size() &&
           std::memcmp(actual.data(), expected.data(), actual.size() * sizeof(T)) == 0;
}

template<typename Resource, typename T>
void test_callback_upload(Device &device, Resource &destination,
                          const std::vector<T> &expected, const char *label) {
    auto stream = device.create_stream();
    std::vector<T> source(expected.size());
    std::vector<T> result(expected.size());
    std::memset(source.data(), 0, source.size() * sizeof(T));
    CallbackGate gate;
    auto releaser = gate.independent_release();
    stream << [&] {
                  gate.wait();
                  std::copy(expected.begin(), expected.end(), source.begin());
              }
           << destination.upload(source.data())
           << destination.download(result.data())
           << commit();
    gate.release();
    releaser.join();
    stream << synchronize() << commit();
    require(!gate.timed_out.load(), "callback upload gate timed out");
    require(identical(result, expected), label);
}

template<typename Resource, typename T>
void test_download_upload(Device &device, Resource &source, Resource &destination,
                          const std::vector<T> &expected, const char *label) {
    auto stream = device.create_stream();
    stream << source.upload(expected.data()) << synchronize() << commit();
    std::vector<T> intermediate(expected.size());
    std::vector<T> result(expected.size());
    std::memset(intermediate.data(), 0, intermediate.size() * sizeof(T));
    CallbackGate gate;
    auto releaser = gate.independent_release();
    stream << [&] { gate.wait(); }
           << source.download(intermediate.data())
           << destination.upload(intermediate.data())
           << destination.download(result.data())
           << commit();
    gate.release();
    releaser.join();
    stream << synchronize() << commit();
    require(!gate.timed_out.load(), "download upload gate timed out");
    require(identical(intermediate, expected), "first download did not produce the expected host data");
    require(identical(result, expected), label);
}

std::vector<uint4> texture_pixels(uint count) {
    std::vector<uint4> pixels(count);
    for (uint i = 0; i < count; ++i)
        pixels[i] = make_uint4(0xf0000000u + i, 17u + i * 3u, 101u + i * 7u, 0xffffffffu - i);
    return pixels;
}

}// namespace

int main() {
    auto device = RHIContext::instance().create_device(test_backend_name());
    {
        constexpr uint count = 17u;
        std::vector<uint> values(count);
        for (uint i = 0; i < count; ++i) values[i] = 0x12340000u + i * 13u;
        auto a = device.create_buffer<uint>(count, "host-order-buffer-a");
        auto b = device.create_buffer<uint>(count, "host-order-buffer-b");
        test_callback_upload(device, a, values, "buffer upload read its source before the host callback");
        test_download_upload(device, a, b, values, "buffer upload read its source before the earlier download");
    }
    {
        auto values = texture_pixels(6u);
        auto a = device.create_texture2d(make_uint2(3u, 2u), PixelStorage::UINT4);
        auto b = device.create_texture2d(make_uint2(3u, 2u), PixelStorage::UINT4);
        test_callback_upload(device, a, values, "2D texture upload read its source before the host callback");
        test_download_upload(device, a, b, values, "2D texture upload read its source before the earlier download");
    }
    {
        auto values = texture_pixels(12u);
        auto a = device.create_texture3d(make_uint3(2u, 3u, 2u), PixelStorage::UINT4);
        auto b = device.create_texture3d(make_uint3(2u, 3u, 2u), PixelStorage::UINT4);
        test_callback_upload(device, a, values, "3D texture upload read its source before the host callback");
        test_download_upload(device, a, b, values, "3D texture upload read its source before the earlier download");
    }
    std::cout << "host transfer order regression checks passed" << std::endl;
    return EXIT_SUCCESS;
}
