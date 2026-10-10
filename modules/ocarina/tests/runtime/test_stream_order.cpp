#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/context.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

using namespace ocarina;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

// A synchronous commit implementation cannot return before this callback is
// released by the calling thread. The timeout turns that error into a failure
// instead of leaving the regression process deadlocked.
struct CallbackGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    std::atomic<bool> expired = false;
    std::atomic<bool> finished = false;

    void wait() {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(10), [&] { return released; })) {
            expired = true;
        }
        finished = true;
    }

    void release_after_commit() {
        require(!finished.load(), "commit waited for an asynchronous host callback");
        {
            std::lock_guard lock(mutex);
            released = true;
        }
        changed.notify_all();
    }
};

void test_nonblocking_commit_and_interleaved_streams(Device &device) {
    auto a = device.create_buffer<uint>(1u, "stream-order-a");
    auto b = device.create_buffer<uint>(1u, "stream-order-b");
    Kernel kernel = [](BufferVar<uint> output, Uint value) { output.write(0u, value); };
    auto shader = device.compile(kernel, "stream-order-interleaved");
    auto first = device.create_stream();
    auto second = device.create_stream();
    CallbackGate gate;
    std::atomic<uint> first_stage = 0u;
    std::atomic<uint> second_stage = 0u;
    std::atomic<bool> ordered = true;

    first << shader(a, 11u).dispatch(1u)
          << [&] {
                 first_stage = 1u;
                 gate.wait();
                 first_stage = 2u;
             }
          << shader(a, 29u).dispatch(1u)
          << [&] {
                 if (first_stage.load() != 2u) ordered = false;
                 first_stage = 3u;
             }
          << commit();

    // This commit also has to enqueue work while the other stream's callback
    // is gated. Neither stream may hold the device's submission lock while
    // executing a host function.
    second << shader(b, 37u).dispatch(1u)
           << [&] { second_stage = 1u; }
           << shader(b, 53u).dispatch(1u)
           << commit();
    gate.release_after_commit();

    uint first_result = 0u;
    uint second_result = 0u;
    second << b.download(&second_result) << synchronize() << commit();
    first << a.download(&first_result) << synchronize() << commit();
    require(!gate.expired.load(), "asynchronous callback timed out before host release");
    require(ordered.load() && first_stage.load() == 3u && second_stage.load() == 1u,
            "synchronize returned before all preceding callbacks completed");
    require(first_result == 29u && second_result == 53u,
            "interleaved streams corrupted dispatch arguments or command order");
}

void test_transfers_callbacks_and_repeated_parameters(Device &device) {
    constexpr uint count = 19u;
    auto input = device.create_buffer<uint>(1u, "stream-order-input");
    auto output = device.create_buffer<uint>(1u, "stream-order-output");
    Kernel kernel = [](BufferVar<uint> input, BufferVar<uint> output, Uint addend) {
        output.write(0u, input.read(0u) + addend);
    };
    auto shader = device.compile(kernel, "stream-order-transfer-parameters");
    auto stream = device.create_stream();
    array<uint, count> source{};
    array<uint, count> results{};
    std::atomic<uint> next_callback = 0u;
    std::atomic<bool> ordered = true;
    for (uint i = 0u; i < count; ++i) {
        source[i] = 1000u + i * 7u;
        stream << input.upload(&source[i])
               << shader(input, output, 3u * i).dispatch(1u)
               << output.download(&results[i])
               << [&, i] {
                      if (next_callback.load() != i || results[i] != 1000u + i * 10u) ordered = false;
                      next_callback = i + 1u;
                  };
        // Mix commands within a batch and work submitted in earlier commits.
        if (i % 4u == 3u) stream << commit();
    }
    stream << synchronize() << commit();
    require(ordered.load() && next_callback.load() == count,
            "a callback ran before its preceding download or outside stream order");
    for (uint i = 0u; i < count; ++i)
        require(results[i] == 1000u + i * 10u, "dispatch parameters or asynchronous transfer data were reused");

    // Synchronize is a point within the command list, not merely a marker at
    // the end of commit. The immediate host function must see the GPU result.
    bool observed_after_sync = false;
    uint final_result = 0u;
    stream << shader(input, output, 71u).dispatch(1u)
           << output.download(&final_result)
           << synchronize()
           << HostFunctionCommand::create(false, [&] {
                  observed_after_sync = final_result == source.back() + 71u;
              })
           << commit();
    require(observed_after_sync, "synchronize did not finish earlier downloads before the next host command");

    bool observed_by_sync_callback = false;
    uint callback_result = 0u;
    stream << shader(input, output, 97u).dispatch(1u)
           << output.download(&callback_result)
           << HostFunctionCommand::create(false, [&] {
                  observed_by_sync_callback = callback_result == source.back() + 97u;
              })
           << commit();
    require(observed_by_sync_callback,
            "a synchronous host function ran before the preceding asynchronous download completed");
}

void test_synchronize_before_resource_release(Device &device) {
    std::atomic<bool> callback_finished = false;
    uint result = 0u;
    {
        auto resource = device.create_buffer<uint>(1u, "stream-order-scoped-resource");
        Kernel kernel = [](BufferVar<uint> output) { output.write(0u, 0x1234abcdu); };
        auto shader = device.compile(kernel, "stream-order-resource-release");
        auto stream = device.create_stream();
        stream << shader(resource).dispatch(1u)
               << resource.download(&result)
               << [&] { callback_finished = true; }
               << commit();
        stream << synchronize() << commit();
        require(callback_finished.load(), "synchronize failed to complete a callback from a previous commit");
        require(result == 0x1234abcdu, "synchronize failed to complete a download before resource release");
    }
    require(callback_finished.load() && result == 0x1234abcdu,
            "scoped resources were released while submitted work was still active");
}

void test_keep_alive_releases_shader_on_completion_thread(Device &device) {
    auto output = device.create_buffer<uint>(1u, "stream-order-retained-shader-output");
    auto stream = device.create_stream();
    Kernel kernel = [](BufferVar<uint> output) { output.write(0u, 0x7654abcdu); };
    using ShaderType = decltype(device.compile(kernel, "stream-order-retained-shader"));
    std::weak_ptr<ShaderType> weak_shader;
    const auto submitting_thread = std::this_thread::get_id();
    std::atomic<bool> destroyed = false;
    std::atomic<bool> destroyed_on_completion_thread = false;
    CallbackGate gate;
    uint result = 0u;

    {
        auto shader = std::shared_ptr<ShaderType>(
            new ShaderType(device.compile(kernel, "stream-order-retained-shader")),
            [&](ShaderType *value) {
                destroyed_on_completion_thread = std::this_thread::get_id() != submitting_thread;
                delete value;
                destroyed = true;
            });
        weak_shader = shader;
        stream << (*shader)(output).dispatch(1u)
               << output.download(&result)
               << [&] { gate.wait(); }
               << keep_alive(true, shader)
               << commit();
    }

    require(!weak_shader.expired() && !destroyed.load(),
            "keep_alive did not retain the shader after the last external reference was released");
    gate.release_after_commit();
    stream << synchronize() << commit();
    require(!gate.expired.load(), "retained shader callback timed out before host release");
    require(result == 0x7654abcdu, "shader resource was released before its dispatch completed");
    require(weak_shader.expired() && destroyed.load(),
            "synchronize returned before the retained shader was destroyed");
    require(destroyed_on_completion_thread.load(),
            "retained shader destruction did not exercise the asynchronous completion thread");
}

} // namespace

int main() {
    auto device = RHIContext::instance().create_device(test_backend_name());
    test_nonblocking_commit_and_interleaved_streams(device);
    test_transfers_callbacks_and_repeated_parameters(device);
    test_synchronize_before_resource_release(device);
    test_keep_alive_releases_shader_on_completion_thread(device);
    std::cout << "stream order regression checks passed" << std::endl;
}
