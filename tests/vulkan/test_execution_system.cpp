#include "test_registry.h"

#include "hardware_wrapper_vulkan/hardware/device_manager.h"
#include "hardware_wrapper_vulkan/hardware/execution.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    void expect(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class TestResourceRef final : public Corona::Horizon::IResourceRef
    {
    public:
        explicit TestResourceRef(std::uintptr_t id)
            : id_(id)
        {
        }

        [[nodiscard]] std::uintptr_t id() const noexcept override { return id_; }
        [[nodiscard]] bool valid() const noexcept override { return true; }

    private:
        std::uintptr_t id_ { 0 };
    };

    [[nodiscard]] Corona::Horizon::ResourceHandle test_resource(std::uintptr_t id)
    {
        Corona::Horizon::ResourceHandle handle;
        Corona::Horizon::ResourceBridge::set(handle, std::make_shared<TestResourceRef>(id));
        return handle;
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_keep_alive_retires_after_timeline_completion()
    {
        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);

        std::weak_ptr<int> weak_marker;
        {
            auto marker = std::make_shared<int>(7);
            weak_marker = marker;

            Corona::Horizon::QueueSubmission submission;
            submission.command_buffer = queue.acquire();
            submission.keep_alive.add_object(marker);
            marker.reset();

            const Corona::Horizon::SubmissionToken token = queue.submit(submission, {}, {});
            expect(token.value == 1, "First fake submit should signal timeline value 1.");
            expect(queue.in_flight_count() == 1, "Submitted command buffer should be in flight.");
            expect(!weak_marker.expired(), "Keep-alive object must survive while the submission is in flight.");

            queue.retire_completed();
            expect(!weak_marker.expired(), "Retire before timeline completion must keep resources alive.");

            queue.mark_completed_for_tests(token.value);
            queue.retire_completed();
            expect(queue.in_flight_count() == 0, "Completed submission should leave the in-flight list.");
            expect(queue.pooled_count() == 1, "Completed command buffer should return to the pool.");
        }

        expect(weak_marker.expired(), "Keep-alive object should release when the tracked command buffer retires.");
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_submit_failure_keeps_submission_resources()
    {
        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        std::weak_ptr<int> weak_marker;

        Corona::Horizon::QueueSubmission submission;
        submission.command_buffer = queue.acquire();
        {
            auto marker = std::make_shared<int>(11);
            weak_marker = marker;
            submission.keep_alive.add_object(marker);
        }

        queue.fail_next_submit_for_tests();

        bool threw = false;
        try
        {
            (void)queue.submit(submission, {}, {});
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }

        expect(threw, "Injected submit failure should throw.");
        expect(!weak_marker.expired(), "Failed submit must leave keep-alive ownership with the caller submission.");
        expect(submission.command_buffer != nullptr, "Failed submit must leave the command buffer with the caller submission.");
        expect(queue.in_flight_count() == 0, "Failed submit must not enqueue in-flight work.");

        submission.keep_alive.clear();
        expect(weak_marker.expired(), "Caller should still control failed-submit keep-alive lifetime.");
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_recorder_and_compiler_collect_requirements()
    {
        Corona::Horizon::ResourceHandle buffer_a = test_resource(101);
        Corona::Horizon::ResourceHandle buffer_b = test_resource(102);

        Corona::Horizon::CommandRecorder recorder;
        recorder.copy({ buffer_a }, { buffer_b }, { 0, 16, 64 });
        recorder.copy({ buffer_b }, { buffer_a }, { 0, 0, 64 });
        recorder.require_feature(Corona::Horizon::FeatureRequirement::DeferredHostOperations);

        Corona::Horizon::RecordedTask task = recorder.close();
        expect(task.commands.size() == 2, "Recorder should preserve abstract copy commands.");
        expect(task.requirements.transfer, "Copy commands should require a transfer-capable queue.");
        expect(task.requirements.timeline_semaphore, "Executor requirements should default to timeline semaphore support.");
        expect(task.requirements.deferred_host_operations, "Explicit deferred host operation requirement should be recorded.");

        Corona::Horizon::ExecutionCompiler compiler;
        Corona::Horizon::ExecutionPlan plan = compiler.compile(task);
        expect(plan.submissions.size() == 1, "Two transfer commands should compile into one transfer submission.");
        expect(plan.submissions[0].commands.size() == 2, "Compiled submission should retain command order.");
        expect(plan.submissions[0].keep_alive.resource_count() == 2, "Compiler should keep each referenced resource alive once.");
        expect(!plan.submissions[0].barriers.empty(), "Read/write reuse of the same resources should produce a barrier record.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_hardware_executor_uses_injected_queue()
    {
        Corona::Horizon::Queue transfer_queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        Corona::Horizon::HardwareExecutor executor([&transfer_queue](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
            expect(capability == Corona::Horizon::QueueCapability::Transfer, "Recorded copy should resolve the transfer queue.");
            return transfer_queue;
        });

        Corona::Horizon::ResourceHandle src = test_resource(201);
        Corona::Horizon::ResourceHandle dst = test_resource(202);
        Corona::Horizon::RecordedTask task = executor.record([&](Corona::Horizon::CommandRecorder& recorder) {
            recorder.copy({ src }, { dst }, { 0, 0, 4 });
        });

        Corona::Horizon::ExecutionPlan plan = executor.compile(task);
        std::vector<Corona::Horizon::SubmissionToken> tokens = executor.submit(plan);
        expect(tokens.size() == 1, "Executor should submit one queue batch.");
        expect(tokens[0].value == 1, "Submitted batch should expose the queue timeline token.");
        expect(transfer_queue.in_flight_count() == 1, "Injected queue should own the submitted work.");

        transfer_queue.mark_completed_for_tests(tokens[0].value);
        transfer_queue.retire_completed();
        expect(transfer_queue.in_flight_count() == 0, "Submitted executor work should retire through the queue.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_parallel_recorders_and_serialized_queue_submit()
    {
        constexpr int thread_count = 8;
        constexpr int submits_per_thread = 16;

        std::atomic<int> recorded { 0 };
        std::vector<std::thread> recorder_threads;
        recorder_threads.reserve(thread_count);

        for (int thread = 0; thread < thread_count; ++thread)
        {
            recorder_threads.emplace_back([&recorded] {
                Corona::Horizon::CommandRecorder recorder;
                recorder.dispatch({}, { 1, 1, 1 });
                Corona::Horizon::RecordedTask task = recorder.close();
                if (task.commands.size() == 1 && task.requirements.compute)
                {
                    recorded.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (std::thread& thread : recorder_threads)
        {
            thread.join();
        }

        expect(recorded.load(std::memory_order_relaxed) == thread_count,
               "Independent command recorders should close correctly on multiple threads.");

        Corona::Horizon::Queue queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        std::vector<std::thread> submit_threads;
        submit_threads.reserve(thread_count);

        for (int thread = 0; thread < thread_count; ++thread)
        {
            submit_threads.emplace_back([&queue] {
                for (int index = 0; index < submits_per_thread; ++index)
                {
                    Corona::Horizon::QueueSubmission submission;
                    submission.command_buffer = queue.acquire();
                    (void)queue.submit(submission, {}, {});
                }
            });
        }

        for (std::thread& thread : submit_threads)
        {
            thread.join();
        }

        const uint64_t expected_submissions = static_cast<uint64_t>(thread_count * submits_per_thread);
        expect(queue.last_submitted_value() == expected_submissions, "Queue submit should serialize timeline increments.");
        expect(queue.in_flight_count() == expected_submissions, "Every fake submit should be tracked in flight.");

        queue.mark_completed_for_tests(expected_submissions);
        queue.retire_completed();
        expect(queue.in_flight_count() == 0, "Completed parallel submissions should all retire.");
        expect(queue.pooled_count() == expected_submissions, "Retired command buffers should return to the pool.");

        return Corona::Horizon::Tests::TestResult::pass();
    }
}

namespace Corona::Horizon::Tests
{
    std::vector<TestCase> execution_system_tests()
    {
        return {
            {
                "execution.keep_alive_retirement",
                "Fake queue keeps submission resources alive until its timeline completion value retires the command buffer.",
                test_keep_alive_retires_after_timeline_completion,
            },
            {
                "execution.submit_failure_keeps_resources",
                "A failed submit leaves command buffer and keep-alive ownership with the caller.",
                test_submit_failure_keeps_submission_resources,
            },
            {
                "execution.recorder_compiler_ir",
                "CommandRecorder records abstract IR and ExecutionCompiler collects requirements, keep-alives, and hazards.",
                test_recorder_and_compiler_collect_requirements,
            },
            {
                "execution.hardware_executor_injected_queue",
                "HardwareExecutor compiles recorded work and submits it through an injected queue resolver.",
                test_hardware_executor_uses_injected_queue,
            },
            {
                "execution.parallel_record_and_submit",
                "Independent recorders close concurrently and Queue serializes parallel fake submissions.",
                test_parallel_recorders_and_serialized_queue_submit,
            },
        };
    }
}
