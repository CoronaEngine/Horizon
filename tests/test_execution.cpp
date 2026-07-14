#include "hardware_wrapper_vulkan/hardware/execution.h"
#include "hardware_wrapper_vulkan/hardware/execution_profile.h"
#include "hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>

namespace
{
    class TestResource final : public Corona::Horizon::IResourceRef
    {
    public:
        explicit TestResource(std::uintptr_t id) noexcept
            : id_(id)
        {
        }

        [[nodiscard]] std::uintptr_t id() const noexcept override { return id_; }
        [[nodiscard]] bool valid() const noexcept override { return id_ != 0; }

    private:
        std::uintptr_t id_ { 0 };
    };

    [[nodiscard]] Corona::Horizon::ResourceHandle make_resource(std::uintptr_t id)
    {
        Corona::Horizon::ResourceHandle handle;
        Corona::Horizon::ResourceBridge::set(handle, std::make_shared<TestResource>(id));
        return handle;
    }

    [[nodiscard]] Corona::Horizon::HardwareBuffer make_buffer(std::uintptr_t id)
    {
        Corona::Horizon::HardwareBuffer buffer;
        Corona::Horizon::ResourceBridge::set(buffer, std::make_shared<TestResource>(id));
        return buffer;
    }

    [[nodiscard]] bool close_enough(double left, double right) noexcept
    {
        return std::abs(left - right) < 0.0001;
    }

    int test_compiler_does_not_build_unused_barriers()
    {
        using namespace Corona::Horizon;

        const ResourceHandle resource = make_resource(17);
        RecordedTask task;
        for (std::uint64_t sequence = 0; sequence < 3; ++sequence)
        {
            CommandIR command;
            command.op = CommandOp::CopyBuffer;
            command.queue = QueueCapability::Transfer;
            command.sequence = sequence;
            command.resources.push_back({ resource, AccessKind::Write, 0 });
            task.commands.push_back(std::move(command));
        }

        const ExecutionPlan plan = ExecutionCompiler {}.compile(task);
        if (plan.submissions.size() != 1)
        {
            std::cerr << "expected one compiled submission\n";
            return 1;
        }
        if (!plan.submissions.front().barriers.empty())
        {
            std::cerr << "compiler retained unused resource barriers\n";
            return 1;
        }
        return 0;
    }

    int test_profile_configuration_and_aggregation()
    {
        using namespace Corona::Horizon;

        if (execution_commit_profile_enabled(nullptr) ||
            execution_commit_profile_enabled("") ||
            execution_commit_profile_enabled("0") ||
            execution_commit_profile_enabled("false"))
        {
            std::cerr << "execution profiling must be disabled by default\n";
            return 1;
        }
        if (!execution_commit_profile_enabled("1") ||
            !execution_commit_profile_enabled("true"))
        {
            std::cerr << "execution profiling enable values were rejected\n";
            return 1;
        }

        ExecutionCommitProfileWindow window;
        ExecutionCommitProfileSample first;
        first.compile_ms = 2.0;
        first.keep_alive_ms = 3.0;
        first.tracker_wait_ms = 5.0;
        first.encode_ms = 7.0;
        first.queue_retire_ms = 11.0;
        first.queue_submit_ms = 13.0;
        first.present_ms = 17.0;
        first.total_ms = 58.0;
        first.commands = 20;
        first.logical_draws = 40;
        first.draw_batches = 2;
        first.resource_uses = 40;
        window.add(first);

        ExecutionCommitProfileSample second = first;
        second.compile_ms = 4.0;
        second.total_ms = 62.0;
        second.commands = 24;
        window.add(second);

        const ExecutionCommitProfileSnapshot snapshot = window.snapshot();
        if (snapshot.samples != 2 ||
            !close_enough(snapshot.avg.compile_ms, 3.0) ||
            !close_enough(snapshot.max.compile_ms, 4.0) ||
            !close_enough(snapshot.avg.total_ms, 60.0) ||
            snapshot.avg.commands != 22 ||
            snapshot.max.commands != 24 ||
            snapshot.avg.logical_draws != 40 ||
            snapshot.avg.draw_batches != 2)
        {
            std::cerr << "execution profile aggregation is incorrect\n";
            return 1;
        }
        return 0;
    }

    int test_profile_classifies_large_native_submissions()
    {
        using namespace Corona::Horizon;
        if (classify_execution_commit(0) != ExecutionCommitSize::Small ||
            classify_execution_commit(127) != ExecutionCommitSize::Small ||
            classify_execution_commit(128) != ExecutionCommitSize::Medium ||
            classify_execution_commit(1023) != ExecutionCommitSize::Medium ||
            classify_execution_commit(1024) != ExecutionCommitSize::Large)
        {
            std::cerr << "execution profile command-count buckets are incorrect\n";
            return 1;
        }
        if (classify_execution_commit(8, 2048) != ExecutionCommitSize::Large)
        {
            std::cerr << "batched logical draws must keep a submission in the large bucket\n";
            return 1;
        }
        return 0;
    }

    int test_draw_indexed_batch_preserves_order_and_deduplicates_resources()
    {
        using namespace Corona::Horizon;

        const ResourceHandle pipeline = make_resource(51);
        const ResourceHandle sampled = make_resource(54);
        const BufferRef index{make_resource(52)};
        const BufferRef vertex{make_resource(53)};

        DrawIndexedBatchDesc batch;
        DrawIndexedBatchItem first;
        first.index = index;
        first.vertex = vertex;
        first.draw.pipeline = pipeline;
        first.draw.index_count = 3;
        first.draw.first_index = 1;
        first.draw.debug_label = "first";
        first.draw.resource_uses.push_back({sampled, AccessKind::Read, 2});
        batch.draws.push_back(std::move(first));

        DrawIndexedBatchItem second;
        second.index = index;
        second.vertex = vertex;
        second.draw.pipeline = pipeline;
        second.draw.index_count = 6;
        second.draw.first_index = 4;
        second.draw.debug_label = "second";
        second.draw.resource_uses.push_back({sampled, AccessKind::Read, 4});
        batch.draws.push_back(std::move(second));

        CommandRecorder recorder;
        recorder.draw_indexed_batch(std::move(batch));
        const RecordedTask task = recorder.close();
        if (task.commands.size() != 1 ||
            task.commands.front().op != CommandOp::DrawIndexedBatch)
        {
            std::cerr << "draw batch did not record as one IR command\n";
            return 1;
        }
        const auto& recorded = task.commands.front();
        if (recorded.payload.draw_indexed_batch.draws.size() != 2 ||
            recorded.payload.draw_indexed_batch.draws[0].draw.first_index != 1 ||
            recorded.payload.draw_indexed_batch.draws[1].draw.first_index != 4 ||
            recorded.payload.draw_indexed_batch.draws[0].draw.debug_label != "first" ||
            recorded.payload.draw_indexed_batch.draws[1].draw.debug_label != "second")
        {
            std::cerr << "draw batch changed draw order or payload ownership\n";
            return 1;
        }
        if (recorded.resources.size() != 4)
        {
            std::cerr << "draw batch did not deduplicate command resources\n";
            return 1;
        }
        const auto sampled_use = std::find_if(
            recorded.resources.begin(), recorded.resources.end(),
            [](const ResourceUse& use) {
                const auto token = ResourceBridge::token(use.handle);
                return token && token->id() == 54;
            });
        if (sampled_use == recorded.resources.end() || sampled_use->stages != 6)
        {
            std::cerr << "draw batch did not merge duplicate resource stages\n";
            return 1;
        }
        return 0;
    }

    int test_draw_batch_iteration_and_dependency_semantics()
    {
        using namespace Corona::Horizon;

        const BufferRef copy_source{make_resource(61)};
        const BufferRef shared_vertex{make_resource(62)};
        const BufferRef index{make_resource(63)};
        const ResourceHandle pipeline = make_resource(64);

        CommandRecorder recorder;
        recorder.copy(copy_source, shared_vertex, {.size = 64});

        DrawIndexedBatchDesc batch;
        for (std::uint32_t first_index : {7u, 11u})
        {
            DrawIndexedBatchItem item;
            item.index = index;
            item.vertex = shared_vertex;
            item.draw.pipeline = pipeline;
            item.draw.index_count = 3;
            item.draw.first_index = first_index;
            batch.draws.push_back(std::move(item));
        }
        recorder.draw_indexed_batch(std::move(batch));
        const RecordedTask task = recorder.close();

        std::vector<std::uint32_t> visited;
        if (!visit_indexed_draws(task.commands[1],
                                 [&](BufferRef visited_index,
                                     BufferRef visited_vertex,
                                     const DrawIndexedDesc& draw) {
                                     if (!visited_index.handle || !visited_vertex.handle)
                                         visited.clear();
                                     visited.push_back(draw.first_index);
                                 }) ||
            visited != std::vector<std::uint32_t>{7u, 11u})
        {
            std::cerr << "batch draw visitor did not preserve order\n";
            return 1;
        }

        const ExecutionPlan plan = ExecutionCompiler{}.compile(task);
        const auto dependency = std::find_if(
            plan.dependencies.begin(), plan.dependencies.end(),
            [](const SubmissionDependency& item) { return item.resource_id == 62; });
        if (plan.submissions.size() != 2 || dependency == plan.dependencies.end())
        {
            std::cerr << "write-to-batch-read dependency was not preserved\n";
            return 1;
        }

        CommandRecorder empty_recorder;
        empty_recorder.draw_indexed_batch({});
        if (!empty_recorder.close().empty())
        {
            std::cerr << "empty draw batch should not emit a command\n";
            return 1;
        }
        return 0;
    }

    int test_consuming_compiler_moves_batch_payload()
    {
        using namespace Corona::Horizon;

        DrawIndexedBatchDesc batch;
        DrawIndexedBatchItem item;
        item.index = {make_resource(71)};
        item.vertex = {make_resource(72)};
        item.draw.pipeline = make_resource(73);
        item.draw.index_count = 3;
        batch.draws.push_back(std::move(item));

        CommandRecorder recorder;
        recorder.draw_indexed_batch(std::move(batch));
        RecordedTask task = recorder.close();
        const ExecutionPlan plan = ExecutionCompiler{}.compile(std::move(task));
        if (!task.commands.empty())
        {
            std::cerr << "consuming compiler left the source batch payload intact\n";
            return 1;
        }
        if (plan.submissions.size() != 1 ||
            plan.submissions[0].commands.size() != 1 ||
            plan.submissions[0].commands[0].payload.draw_indexed_batch.draws.size() != 1)
        {
            std::cerr << "consuming compiler lost the moved batch payload\n";
            return 1;
        }
        return 0;
    }

    int test_consuming_rasterizer_recording_moves_draws_in_order()
    {
        using namespace Corona::Horizon;

        RasterizerPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader.stage = PipelineShaderStage::Vertex;
        pipeline_desc.vertex_shader.module.shaderCode = std::vector<std::uint32_t>{0x07230203u};
        pipeline_desc.fragment_shader.stage = PipelineShaderStage::Fragment;
        pipeline_desc.fragment_shader.module.shaderCode = std::vector<std::uint32_t>{0x07230203u};
        VulkanRasterizerPipeline pipeline(std::move(pipeline_desc));
        const ResourceHandle pipeline_handle = make_resource(41);
        const HardwareBuffer index = make_buffer(42);
        const HardwareBuffer vertex = make_buffer(43);

        DrawIndexedParams first;
        first.index_count = 3;
        first.first_index = 1;
        first.debug_label = "first";
        pipeline.record(pipeline_handle, index, vertex, first);

        DrawIndexedParams second;
        second.index_count = 6;
        second.first_index = 4;
        second.debug_label = "second";
        pipeline.record(pipeline_handle, index, vertex, second);

        if (pipeline.snapshot().draws.size() != 2)
        {
            std::cerr << "test setup did not record two raster draws\n";
            return 1;
        }

        const CommandBatch ordinary_batch = pipeline.command_batch();
        const CommandBatch repeated_batch = pipeline.command_batch();
        if (ordinary_batch.commands().size() != 2 || repeated_batch.commands().size() != 2)
        {
            std::cerr << "ordinary command_batch stopped being repeatable\n";
            return 1;
        }
        CommandRecorder ordinary_recorder;
        for (const StreamCommand& command : ordinary_batch.commands())
            command.record(ordinary_recorder);
        const RecordedTask ordinary_task = ordinary_recorder.close();

        CommandRecorder recorder;
        pipeline.record_consuming(recorder);
        const RecordedTask task = recorder.close();
        if (task.commands.size() != 1 ||
            task.commands[0].op != CommandOp::DrawIndexedBatch ||
            task.commands[0].payload.draw_indexed_batch.draws.size() != 2 ||
            task.commands[0].payload.draw_indexed_batch.draws[0].draw.first_index != 1 ||
            task.commands[0].payload.draw_indexed_batch.draws[1].draw.first_index != 4 ||
            task.commands[0].payload.draw_indexed_batch.draws[0].draw.debug_label != "first" ||
            task.commands[0].payload.draw_indexed_batch.draws[1].draw.debug_label != "second")
        {
            std::cerr << "consuming raster recording changed draw order or payload\n";
            return 1;
        }
        if (ordinary_task.commands.size() != 2 ||
            ordinary_task.commands[0].op != CommandOp::DrawIndexed ||
            ordinary_task.commands[1].op != CommandOp::DrawIndexed)
        {
            std::cerr << "ordinary command_batch no longer emits scalar draws\n";
            return 1;
        }
        const auto& consumed_draws = task.commands[0].payload.draw_indexed_batch.draws;
        for (std::size_t i = 0; i < ordinary_task.commands.size(); ++i)
        {
            const auto& ordinary = ordinary_task.commands[i];
            const auto& consumed = consumed_draws[i].draw;
            if (
                ordinary.payload.draw_indexed.index_count !=
                    consumed.index_count ||
                ordinary.payload.draw_indexed.first_index !=
                    consumed.first_index ||
                ordinary.payload.draw_indexed.debug_label != consumed.debug_label)
            {
                std::cerr << "consuming raster recording differs from command_batch\n";
                return 1;
            }
        }
        if (!pipeline.snapshot().draws.empty())
        {
            std::cerr << "consuming raster recording did not empty the draw list\n";
            return 1;
        }
        return 0;
    }
}

int main()
{
    if (const int result = test_compiler_does_not_build_unused_barriers(); result != 0)
        return result;
    if (const int result = test_profile_configuration_and_aggregation(); result != 0)
        return result;
    if (const int result = test_profile_classifies_large_native_submissions(); result != 0)
        return result;
    if (const int result = test_draw_indexed_batch_preserves_order_and_deduplicates_resources(); result != 0)
        return result;
    if (const int result = test_draw_batch_iteration_and_dependency_semantics(); result != 0)
        return result;
    if (const int result = test_consuming_compiler_moves_batch_payload(); result != 0)
        return result;
    return test_consuming_rasterizer_recording_moves_draws_in_order();
}
