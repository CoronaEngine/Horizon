#include "test_registry.h"

#include "hardware_wrapper_vulkan/hardware/device_manager.h"
#include "hardware_wrapper_vulkan/hardware/execution.h"

#include <algorithm>
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

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_stream_facade_records_and_commits()
    {
        Corona::Horizon::Queue transfer_queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        Corona::Horizon::Queue compute_queue({ 0 }, Corona::Horizon::QueueCapability::Compute);

        Corona::Horizon::HardwareExecutor executor(
            [&](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
                if (capability == Corona::Horizon::QueueCapability::Compute)
                {
                    return compute_queue;
                }
                return transfer_queue;
            });

        Corona::Horizon::ResourceHandle src = test_resource(301);
        Corona::Horizon::ResourceHandle dst = test_resource(302);
        Corona::Horizon::ResourceHandle shader = test_resource(303);

        Corona::Horizon::SubmitReceipt receipt =
            executor.stream()
            << Corona::Horizon::copy({ src }, { dst }, { 0, 0, 16 })
            << Corona::Horizon::dispatch({ shader }, { 2, 1, 1 })
            << Corona::Horizon::commit();

        expect(receipt.serial == 1, "Stream commit should assign a submit serial.");
        expect(receipt.tokens.size() == 2, "Copy and dispatch should submit to two queue batches.");
        expect(transfer_queue.in_flight_count() == 1, "Transfer queue should receive the copy batch.");
        expect(compute_queue.in_flight_count() == 1, "Compute queue should receive the dispatch batch.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_stream_batch_and_close_for_tests_preserve_order()
    {
        Corona::Horizon::HardwareExecutor executor;
        Corona::Horizon::ResourceHandle src = test_resource(311);
        Corona::Horizon::ResourceHandle dst = test_resource(312);
        Corona::Horizon::ResourceHandle shader = test_resource(313);

        Corona::Horizon::CommandBatch batch;
        batch << Corona::Horizon::copy({ src }, { dst }, { 0, 4, 8 })
              << Corona::Horizon::dispatch({ shader }, { 1, 2, 3 });

        auto stream = executor.stream();
        stream << batch;
        Corona::Horizon::RecordedTask task = stream.close_for_tests();

        expect(task.commands.size() == 2, "CommandBatch should append both commands.");
        expect(task.commands[0].op == Corona::Horizon::CommandOp::CopyBuffer, "First stream command should be copy.");
        expect(task.commands[1].op == Corona::Horizon::CommandOp::Dispatch, "Second stream command should be dispatch.");
        expect(task.commands[0].sequence == 0 && task.commands[1].sequence == 1, "Recorder should preserve command sequence numbers.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_compiler_keeps_non_contiguous_queue_batches_ordered()
    {
        Corona::Horizon::ResourceHandle src = test_resource(321);
        Corona::Horizon::ResourceHandle dst = test_resource(322);
        Corona::Horizon::ResourceHandle shader = test_resource(323);

        Corona::Horizon::CommandRecorder recorder;
        recorder.copy({ src }, { dst }, { 0, 0, 16 });
        recorder.dispatch({ shader }, { 1, 1, 1 });
        recorder.copy({ dst }, { src }, { 0, 0, 16 });

        Corona::Horizon::ExecutionCompiler compiler;
        Corona::Horizon::ExecutionPlan plan = compiler.compile(recorder.close());

        expect(plan.submissions.size() == 3, "Non-contiguous transfer work should not be merged across compute work.");
        expect(plan.submissions[0].queue == Corona::Horizon::QueueCapability::Transfer, "First batch should be transfer.");
        expect(plan.submissions[1].queue == Corona::Horizon::QueueCapability::Compute, "Second batch should be compute.");
        expect(plan.submissions[2].queue == Corona::Horizon::QueueCapability::Transfer, "Third batch should return to transfer.");
        expect(!plan.dependencies.empty(), "Reusing written resources across non-contiguous batches should create a DAG dependency.");

        const auto waits_for_first_transfer = std::ranges::find_if(plan.dependencies, [](const Corona::Horizon::SubmissionDependency& dependency) {
            return dependency.producer == 0 && dependency.consumer == 2;
        });
        expect(waits_for_first_transfer != plan.dependencies.end(),
               "The second transfer batch should wait for the first transfer batch.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_host_callback_runs_when_submission_retires()
    {
        Corona::Horizon::Queue transfer_queue({ 0 }, Corona::Horizon::QueueCapability::Transfer);
        Corona::Horizon::HardwareExecutor executor([&](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability) -> Corona::Horizon::Queue& {
            return transfer_queue;
        });

        bool callback_ran = false;
        Corona::Horizon::SubmitReceipt receipt =
            executor.stream()
            << Corona::Horizon::host_callback([&callback_ran] {
                   callback_ran = true;
               })
            << Corona::Horizon::commit();

        expect(receipt.tokens.size() == 1, "Host callback should submit as a tracked timeline batch.");
        expect(!callback_ran, "Host callback should wait for queue retirement.");

        transfer_queue.mark_completed_for_tests(receipt.tokens[0].value);
        transfer_queue.retire_completed();

        expect(callback_ran, "Host callback should run when tracked keep-alives retire.");
        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_present_node_reports_receipt_status()
    {
        Corona::Horizon::Queue present_queue({ 0 }, Corona::Horizon::QueueCapability::Present);
        Corona::Horizon::HardwareExecutor executor([&](Corona::Horizon::DeviceId, Corona::Horizon::QueueCapability capability) -> Corona::Horizon::Queue& {
            expect(capability == Corona::Horizon::QueueCapability::Present, "Present command should resolve the present queue.");
            return present_queue;
        });

        Corona::Horizon::ResourceHandle image = test_resource(401);
        Corona::Horizon::SubmitReceipt receipt =
            executor.stream()
            << Corona::Horizon::present({ 77 }, { image }, { 0 })
            << Corona::Horizon::commit();

        expect(receipt.tokens.size() == 1, "Present node should still produce a queue submission token.");
        expect(receipt.presents.size() == 1, "SubmitReceipt should include present status.");
        expect(receipt.presents[0].status == Corona::Horizon::PresentStatus::Skipped,
               "No swapchain-bound DisplayManager should report a skipped present.");

        return Corona::Horizon::Tests::TestResult::pass();
    }

    [[nodiscard]] Corona::Horizon::Tests::TestResult test_cross_device_present_fallback_and_strict_dependency()
    {
        Corona::Horizon::ResourceHandle src = test_resource(501);
        Corona::Horizon::ResourceHandle dst = test_resource(502);

        {
            Corona::Horizon::CommandRecorder recorder;
            recorder.copy({ src }, { dst }, { 0, 0, 16 });
            recorder.present({ 91 }, { dst }, { 1 }, true);

            Corona::Horizon::ExecutionCompiler compiler;
            Corona::Horizon::ExecutionPlan plan = compiler.compile(recorder.close());
            expect(plan.cross_device_dependencies.size() == 1,
                   "Cross-device present should record a dependency bridge.");
            expect(plan.cross_device_dependencies[0].present_cpu_bridge_fallback,
                   "Present may use the CPU bridge fallback when imported timeline sync is unavailable.");
        }

        {
            Corona::Horizon::CommandRecorder recorder;
            recorder.copy({ src }, { dst }, { 0, 0, 16 });
            recorder.copy({ dst }, { src }, { 0, 0, 16 }, { 2 });

            bool threw = false;
            try
            {
                Corona::Horizon::ExecutionCompiler compiler;
                (void)compiler.compile(recorder.close());
            }
            catch (const std::logic_error&)
            {
                threw = true;
            }

            expect(threw, "Non-present cross-device resource dependency should fail without explicit sync.");
        }

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
                "execution.stream_facade_commit",
                "HardwareStream records ocarina-style commands and commits them through executor queues.",
                test_stream_facade_records_and_commits,
            },
            {
                "execution.stream_batch_order",
                "CommandBatch and HardwareStream preserve typed IR order before compile.",
                test_stream_batch_and_close_for_tests_preserve_order,
            },
            {
                "execution.compiler_dag_order",
                "ExecutionCompiler keeps non-contiguous queue batches ordered with explicit DAG dependencies.",
                test_compiler_keeps_non_contiguous_queue_batches_ordered,
            },
            {
                "execution.host_callback_retire",
                "Host callbacks are retained by the command buffer and run when the timeline retires.",
                test_host_callback_runs_when_submission_retires,
            },
            {
                "execution.present_receipt",
                "Present nodes submit through the present queue and report status through SubmitReceipt.",
                test_present_node_reports_receipt_status,
            },
            {
                "execution.cross_device_present",
                "Present cross-device dependencies allow a fallback while ordinary cross-device hazards fail.",
                test_cross_device_present_fallback_and_strict_dependency,
            },
            {
                "execution.parallel_record_and_submit",
                "Independent recorders close concurrently and Queue serializes parallel fake submissions.",
                test_parallel_recorders_and_serialized_queue_submit,
            },
        };
    }
}
