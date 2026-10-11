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

void test_many_commits_and_gpu_batch_reuse(Device &device) {
    constexpr uint dispatch_count = 320u;
    constexpr uint callback_count = 3u;
    constexpr array<uint, callback_count> callback_points{95u, 223u, 319u};
    auto output = device.create_buffer<uint>(dispatch_count + callback_count, "stream-order-many-commits");
    auto input = device.create_buffer<uint>(1u, "stream-order-callback-upload");
    Kernel write_kernel = [](BufferVar<uint> output, Uint index, Uint value) {
        output.write(index, value);
    };
    Kernel copy_kernel = [](BufferVar<uint> input, BufferVar<uint> output, Uint index) {
        output.write(index, input.read(0u));
    };
    auto write_shader = device.compile(write_kernel, "stream-order-many-commit-parameters");
    auto copy_shader = device.compile(copy_kernel, "stream-order-many-commit-host-upload");
    auto stream = device.create_stream();
    array<uint, callback_count> snapshots{};
    array<uint, callback_count> upload_sources{};
    array<std::atomic<uint>, callback_count> callback_hits{};
    std::atomic<uint> next_callback = 0u;
    std::atomic<bool> ordered = true;
    uint callback_index = 0u;

    for (uint i = 0u; i < dispatch_count; ++i) {
        // Runs of 96/128/96 separate commits exceed the stream's 64 batch
        // capacity, so pure GPU work must make progress without a host task.
        stream << write_shader(output, i, 0x12000000u + i * 17u).dispatch(1u) << commit();
        if (callback_index < callback_count && i == callback_points[callback_index]) {
            const auto index = callback_index++;
            stream << output.view(i, 1u).download(&snapshots[index])
                   << [&, index, i] {
                          if (next_callback.fetch_add(1u) != index ||
                              snapshots[index] != 0x12000000u + i * 17u)
                              ordered = false;
                          callback_hits[index].fetch_add(1u);
                          upload_sources[index] = 0xa5000000u + index * 23u;
                      }
                   << input.upload(&upload_sources[index])
                   << copy_shader(input, output, dispatch_count + index).dispatch(1u)
                   << commit();
        }
    }
    array<uint, dispatch_count + callback_count> results{};
    stream << output.download(results.data()) << synchronize() << commit();
    require(ordered.load() && next_callback.load() == callback_count,
            "batch retirement lost or reordered host callbacks");
    for (uint i = 0u; i < dispatch_count; ++i)
        require(results[i] == 0x12000000u + i * 17u,
                "many asynchronous commits reused in-flight dispatch parameters");
    for (uint i = 0u; i < callback_count; ++i) {
        require(callback_hits[i].load() == 1u, "a retirement callback did not execute exactly once");
        require(results[dispatch_count + i] == 0xa5000000u + i * 23u,
                "retirement did not order callback output before the following upload");
    }

    auto reused = device.create_buffer<uint>(4u, "stream-order-reused-synchronous-batch");
    array<uint, 4> expected{};
    for (uint i = 0u; i < 96u; ++i) {
        uint index = i % 4u;
        expected[index] = 0xbeef0000u + i;
        stream << write_shader(reused, index, expected[index]).dispatch(1u);
        // Alternate an inline synchronization and a later synchronization of
        // already committed GPU-only work. Both paths must safely reuse fences,
        // command pools and argument storage without entering the host worker.
        if ((i & 1u) != 0u) stream << commit();
        stream << synchronize() << commit();
    }
    array<uint, 4> reused_results{};
    stream << reused.download(reused_results.data()) << synchronize() << commit();
    for (uint i = 0u; i < expected.size(); ++i)
        require(reused_results[i] == expected[i], "synchronous batch reuse corrupted GPU output");
}

} // namespace

int main() {
    auto device = RHIContext::instance().create_device(test_backend_name());
    test_nonblocking_commit_and_interleaved_streams(device);
    test_transfers_callbacks_and_repeated_parameters(device);
    test_synchronize_before_resource_release(device);
    test_keep_alive_releases_shader_on_completion_thread(device);
    test_many_commits_and_gpu_batch_reuse(device);
    std::cout << "stream order regression checks passed" << std::endl;
}
