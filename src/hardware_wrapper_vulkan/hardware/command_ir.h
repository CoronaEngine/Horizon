#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <volk.h>

#include "horizon.h"

// 内部 Command IR 层。
//
// 这些类型曾经内嵌在公共头 include/horizon.h 里。P0/P1 把整个 IR 层移出公共头：
// horizon.h 只按值持有三个叶子类型（DeviceId / DisplayerRef / BufferImageCopyRegion），
// 其余引用/IR 类型均在此文件。
//
// 本文件仅被硬件层（hardware_wrapper_vulkan/）内部 include：command.h 在 horizon.h
// 之后包含它，execution.h / 各 pipeline / display_manager / device_manager 也依赖它。
// 典型 user 只 include horizon.h，不会触达这里——这就是"IR 移出"的边界。

namespace Corona::Horizon
{
    // ExecutionCommitProfileSample 定义在 execution_profile.h（依赖很轻），这里只反声明，
    // 供 ExecutionCompiler::compile 的默认 nullptr 形参使用。
    struct ExecutionCommitProfileSample;

    // ================================================================
    // Submit 同步原语（原 execution.h）
    // ----------------------------------------------------------------
    // SubmissionSync 是 timelinesemaphore 的轻量包装。horizon.h 里 SecurityRef 的
    // SubmissionToken 通过 shared_ptr<const SubmissionSync> 引用它，故其声明要
    // 在 SubmissionToken 之前。
    // ================================================================

    class TrackedCommandBuffer;

    class SubmissionSync
    {
    public:
        [[nodiscard]] VkSemaphore timeline() const noexcept { return timeline_.load(std::memory_order_acquire); }

    private:
        friend class Queue;

        explicit SubmissionSync(VkSemaphore timeline) noexcept;
        [[nodiscard]] static std::shared_ptr<SubmissionSync> make(VkSemaphore timeline);
        void invalidate() noexcept { timeline_.store(VK_NULL_HANDLE, std::memory_order_release); }

        std::atomic<VkSemaphore> timeline_ { VK_NULL_HANDLE };
    };

    struct SubmitWait
    {
        VkSemaphore semaphore { VK_NULL_HANDLE };
        uint64_t value { 0 };
        VkPipelineStageFlags2 stages { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
    };

    struct SubmitSignal
    {
        VkSemaphore semaphore { VK_NULL_HANDLE };
        uint64_t value { 0 };
        VkPipelineStageFlags2 stages { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
    };

    // ================================================================
    // Command IR 层（原 horizon.h 719–1175 区间整体移入）
    // ----------------------------------------------------------------
    // 依赖序：先叶子类型（QueueCapability / AccessKind / CommandOp / ref 结构 /
    // copy 结构），再 *Desc 结构，再 CommandPayload / CommandIR / visitor，
    // 再 RequirementSet / RecordedTask / barrier / present / dependency /
    // keep-alive / token / submission，最后 CommandRecorder 与内部门面。
    // ================================================================

    enum class QueueCapability
    {
        Graphics,
        Compute,
        Transfer,
        Present
    };

    enum class AccessKind
    {
        Read,
        Write,
        ReadWrite
    };

    enum class CommandOp
    {
        CopyBuffer,
        CopyBufferToImage,
        Dispatch,
        BeginRendering,
        EndRendering,
        DrawIndexed,
        DrawIndexedBatch,
        DrawIndexedIndirect,
        Present,
        KeepAlive,
        CopyImage
    };

    // DeviceId / DisplayerRef / BufferImageCopyRegion 定义在 horizon.h（公共头按值持有）。
    // 以下是仅内部使用的引用/掩码类型。

    struct DeviceMask
    {
        uint32_t bits { 1 };
    };

    struct BufferRef
    {
        ResourceHandle handle {};
    };

    struct ImageRef
    {
        ResourceHandle handle {};
    };

    struct ImageSubresourceLayout
    {
        uint64_t byte_offset = 0;
        uint64_t byte_size = 0;
        uint64_t row_pitch = 0;
        uint64_t slice_pitch = 0;
        ImageExtent extent {};
    };

    enum class FeatureRequirement
    {
        TimelineSemaphore,
        Synchronization2,
        DeferredHostOperations,
        DeviceGroup
    };

    struct QueueId
    {
        DeviceId device {};
        uint32_t family_index { 0 };
        uint32_t queue_index { 0 };
        QueueCapability capability { QueueCapability::Transfer };
    };

    struct CopyRegion
    {
        uint64_t src_offset { 0 };
        uint64_t dst_offset { 0 };
        uint64_t size { 0 };
    };

    struct ImageCopyRegion
    {
        uint32_t src_layer { 0 };
        uint32_t dst_layer { 0 };
        uint32_t src_mip { 0 };
        uint32_t dst_mip { 0 };
    };

    struct ResourceUse
    {
        ResourceHandle handle {};
        AccessKind access { AccessKind::Read };
        uint64_t stages { 0 };
    };

    enum class DispatchBindingKind
    {
        StorageBuffer,
        StorageImage
    };

    struct DispatchResourceBinding
    {
        uint32_t set { 0 };
        uint32_t binding { 0 };
        ResourceHandle resource {};
        DispatchBindingKind kind { DispatchBindingKind::StorageBuffer };
        AccessKind access { AccessKind::ReadWrite };
    };

    struct UniformBufferBindingData
    {
        uint32_t set { 0 };
        uint32_t binding { 0 };
        std::vector<std::byte> data;
        // 持久化 GPU buffer，在管线初始化时通过反射创建一次，之后只 write_bytes。
        // 使用 ResourceHandle（基类）存储句柄，避免与 horizon.h 的循环依赖。
        ResourceHandle gpu_buffer {};
    };

    struct DispatchDesc
    {
        ComputePipelineBase* pipeline;
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo comp_condition_info;

        uint32_t groups_x { 1 };
        uint32_t groups_y { 1 };
        uint32_t groups_z { 1 };
        std::vector<DispatchResourceBinding> bindings;
        std::vector<ResourceUse> resource_uses;
        std::vector<std::byte> push_constant_data;
        std::vector<UniformBufferBindingData> uniform_buffers;
        std::string debug_label;
    };

    struct RenderingDesc
    {
        ImageRef color {};
        std::array<ImageRef, 3> extra_colors {};
        ImageRef depth {};
        uint32_t width { 0 };
        uint32_t height { 0 };
        bool clear_color { false };
        bool clear_depth { false };
    };

    struct DrawIndexedDesc
    {
        RasterizerPipelineBase* pipeline {};
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo vert_condition_info;
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo frag_condition_info;

        uint32_t index_count { 0 };
        uint32_t instance_count { 1 };
        uint32_t first_index { 0 };
        int32_t vertex_offset { 0 };
        uint32_t first_instance { 0 };
        IndexType index_type { IndexType::Auto };
        bool enable_scissor { false };
        ScissorRect scissor {};
        std::vector<ResourceUse> resource_uses;
        std::vector<std::byte> push_constant_data;
        std::vector<UniformBufferBindingData> uniform_buffers;
        std::string debug_label;
    };

    struct DrawIndexedBatchItem
    {
        BufferRef index {};
        BufferRef vertex {};
        DrawIndexedDesc draw {};
    };

    struct DrawIndexedBatchDesc
    {
        std::vector<DrawIndexedBatchItem> draws;
    };

    struct DrawIndexedIndirectDesc
    {
        RasterizerPipelineBase* pipeline {};
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo vert_condition_info;
        EmbeddedShader::ShaderCodeCompiler::ConditionInfo frag_condition_info;

        uint64_t indirect_offset { 0 };
        uint32_t draw_count { 0 };
        // 0 means sizeof(DrawIndexedIndirectCommand).
        uint32_t stride { 0 };
        IndexType index_type { IndexType::Auto };
        bool enable_scissor { false };
        ScissorRect scissor {};
        std::vector<ResourceUse> resource_uses;
        std::vector<std::byte> push_constant_data;
        std::vector<UniformBufferBindingData> uniform_buffers;
        std::string debug_label;
    };

    struct PresentDesc
    {
        DisplayerRef displayer {};
        ImageRef image {};
        ImageRef swapchain_image {};
        DeviceId present_device {};
        bool allow_cpu_bridge_fallback { true };
    };

    struct CommandPayload
    {
        CopyRegion copy {};
        ImageCopyRegion image_copy {};
        BufferImageCopyRegion buffer_image_copy {};
        DispatchDesc dispatch {};
        RenderingDesc rendering {};
        DrawIndexedDesc draw_indexed {};
        DrawIndexedBatchDesc draw_indexed_batch {};
        DrawIndexedIndirectDesc draw_indexed_indirect {};
        PresentDesc present {};
    };

    struct CommandIR
    {
        CommandOp op { CommandOp::CopyBuffer };
        DeviceMask devices {};
        QueueCapability queue { QueueCapability::Transfer };
        std::vector<ResourceUse> resources;
        CommandPayload payload {};
        std::shared_ptr<void> keep_alive {};
        uint64_t sequence { 0 };
        std::string debug_label;
    };

    template <typename Visitor>
    [[nodiscard]] bool visit_indexed_draws(const CommandIR& command, Visitor&& visitor)
    {
        if (command.op == CommandOp::DrawIndexed)
        {
            const DrawIndexedDesc& draw = command.payload.draw_indexed;
            const BufferRef index = command.resources.size() > 0
                ? BufferRef{command.resources[0].handle}
                : BufferRef{};
            const BufferRef vertex = command.resources.size() > 1
                ? BufferRef{command.resources[1].handle}
                : BufferRef{};
            std::forward<Visitor>(visitor)(index, vertex, draw);
            return true;
        }
        if (command.op == CommandOp::DrawIndexedBatch)
        {
            for (const DrawIndexedBatchItem& item : command.payload.draw_indexed_batch.draws)
                visitor(item.index, item.vertex, item.draw);
            return true;
        }
        return false;
    }

    struct RequirementSet
    {
        bool graphics { false };
        bool compute { false };
        bool transfer { false };
        bool timeline_semaphore { true };
        bool synchronization_2 { true };
        bool deferred_host_operations { false };
        bool device_group { false };
    };

    struct RecordedTask
    {
        std::vector<CommandIR> commands;
        RequirementSet requirements {};
    };

    struct ResourceBarrier
    {
        std::uintptr_t resource_id { 0 };
        AccessKind before { AccessKind::Read };
        AccessKind after { AccessKind::Read };
    };

    enum class PresentStatus
    {
        None,
        Presented,
        Suboptimal,
        OutOfDate,
        Skipped
    };

    struct PresentResult
    {
        PresentStatus status { PresentStatus::None };
        DisplayerRef displayer {};
        ImageRef image {};
        std::string message;
    };

    struct CrossDeviceDependency
    {
        std::uintptr_t resource_id { 0 };
        DeviceId producer {};
        DeviceId consumer {};
        bool present_cpu_bridge_fallback { false };
        bool imported_timeline_required { false };
    };

    struct SubmissionDependency
    {
        size_t producer { 0 };
        size_t consumer { 0 };
        std::uintptr_t resource_id { 0 };
    };

    class SubmissionKeepAlive
    {
    public:
        void add_resource(std::shared_ptr<const IResourceRef> resource);
        void add_object(std::shared_ptr<void> object);
        void clear() noexcept;

    private:
        std::vector<std::shared_ptr<const IResourceRef>> resources_;
        std::unordered_set<std::uintptr_t> resource_ids_;
        std::vector<std::shared_ptr<void>> objects_;
    };

    struct SubmissionToken
    {
        DeviceId device {};
        QueueId queue {};
        uint64_t value { 0 };
        std::shared_ptr<const SubmissionSync> sync {};

        [[nodiscard]] bool has_sync() const noexcept { return sync != nullptr; }
    };

    struct QueueSubmission
    {
        std::shared_ptr<class TrackedCommandBuffer> command_buffer;
        SubmissionKeepAlive keep_alive;
        std::string debug_summary;
    };

    // SubmitReceipt 的后台载荷：SubmissionToken 与 PresentResult 现在由
    // horizon.h 的公共 SubmitReceipt 通过 shared_ptr<const void> 不透明持有。
    struct SubmitReceiptData
    {
        std::vector<SubmissionToken> tokens;
        std::vector<PresentResult> presents;
    };

    // ================================================================
    // CommandRecorder
    // ================================================================

    class CommandRecorder
    {
    public:
        CommandRecorder() = default;

        void copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices = {});
        void copy_image(ImageRef src, ImageRef dst, ImageCopyRegion region, DeviceMask devices = {});
        void copy_to_image(BufferRef src, ImageRef dst, BufferImageCopyRegion region, DeviceMask devices = {});
        void dispatch(DispatchDesc desc, DeviceMask devices = {});
        void begin_rendering(RenderingDesc desc, DeviceMask devices = {});
        void end_rendering(DeviceMask devices = {});
        void draw_indexed(BufferRef index, BufferRef vertex, DrawIndexedDesc desc, DeviceMask devices = {});
        void draw_indexed_batch(DrawIndexedBatchDesc batch, DeviceMask devices = {});
        void draw_indexed_indirect(BufferRef index,
                                   BufferRef vertex,
                                   BufferRef indirect,
                                   DrawIndexedIndirectDesc desc,
                                   DeviceMask devices = {});
        void present(DisplayerRef displayer, ImageRef image, DeviceId present_device = {}, bool allow_cpu_bridge_fallback = true);
        void keep_alive(std::shared_ptr<void> object);
        [[nodiscard]] RecordedTask close();

    private:
        void ensure_open() const;
        void mark_requirement(QueueCapability capability);
        void mark_requirement(FeatureRequirement feature);
        [[nodiscard]] uint64_t next_sequence() noexcept;
        void mark_device_requirements(DeviceMask devices);

        bool closed_ { false };
        uint64_t next_sequence_ { 0 };
        std::vector<CommandIR> commands_;
        RequirementSet requirements_ {};
    };

    class StreamCommand
    {
    public:
        StreamCommand() = default;
        explicit StreamCommand(std::function<void(CommandRecorder&)> recorder);

        template <typename Command>
            requires(!std::is_same_v<std::remove_cvref_t<Command>, StreamCommand> &&
                     requires(const std::remove_cvref_t<Command>& command, CommandRecorder& recorder) {
                         command.record(recorder);
                     })
        StreamCommand(Command&& command)
            : recorder_([command = std::remove_cvref_t<Command>(std::forward<Command>(command))](
                            CommandRecorder& recorder) { command.record(recorder); })
        {
        }

        void record(CommandRecorder& recorder) const;
        [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(recorder_); }

    private:
        std::function<void(CommandRecorder&)> recorder_ {};
    };

    class CommandBatch
    {
    public:
        CommandBatch& operator<<(StreamCommand command);
        [[nodiscard]] const std::vector<StreamCommand>& commands() const noexcept { return commands_; }

    private:
        std::vector<StreamCommand> commands_;
    };

    // ================================================================
    // 值命令门面（内部变体，不对外暴露）
    // ----------------------------------------------------------------
    // 仅被 vulkan 层内部使用：ShaderDispatchCommand -> vulkan_compute_pipeline.cpp，
    // Begin/End/DrawIndexedBatch/DrawIndexedIndirectStream -> vulkan_rasterizer_pipeline.cpp。
    // 各自提供 record(CommandRecorder&) const，可经 StreamCommand 的模板折叠构造
    // 隐式转换（requires 子句只需 CommandRecorder 反声明）。放在 CommandRecorder
    // 之后：内联 record() 体里调用 recorder.dispatch(...) 等成员，MSVC 要求
    // CommandRecorder 在成员函数体“定义”时已完整。
    // ================================================================

    struct ShaderDispatchCommand
    {
        DispatchDesc dispatch {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.dispatch(dispatch, devices);
        }
    };

    struct BeginRenderingCommand
    {
        RenderingDesc rendering {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.begin_rendering(rendering, devices);
        }
    };

    struct EndRenderingCommand
    {
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.end_rendering(devices);
        }
    };

    // 批量 indexed draw。payload 由 shared_ptr 持有：StreamCommand 的转换构造会按值
    // 拷贝命令对象，而一个批次可能有数万条 draw，直接内嵌 vector 会让每次
    // StreamCommand 拷贝都变成一次深拷贝。
    struct DrawIndexedBatchCommand
    {
        std::shared_ptr<DrawIndexedBatchDesc> batch {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            if (batch)
                recorder.draw_indexed_batch(*batch, devices);
        }
    };

    struct DrawIndexedIndirectStreamCommand
    {
        BufferRef index {};
        BufferRef vertex {};
        BufferRef indirect {};
        DrawIndexedIndirectDesc draw {};
        DeviceMask devices {};

        void record(CommandRecorder& recorder) const
        {
            recorder.draw_indexed_indirect(index, vertex, indirect, draw, devices);
        }
    };

    // ================================================================
    // 编译与提交产物
    // ================================================================

    struct CompiledSubmission
    {
        DeviceId device {};
        QueueCapability queue { QueueCapability::Transfer };
        std::vector<CommandIR> commands;
        std::vector<ResourceBarrier> barriers;
        std::vector<SubmitWait> waits;
        std::vector<SubmitSignal> signals;
        std::vector<PresentDesc> presents;
        SubmissionKeepAlive keep_alive;
        std::shared_ptr<TrackedCommandBuffer> command_buffer;
    };

    struct ExecutionPlan
    {
        std::vector<CompiledSubmission> submissions;
        std::vector<SubmissionDependency> dependencies;
        std::vector<CrossDeviceDependency> cross_device_dependencies;
    };

    class ExecutionCompiler
    {
    public:
        [[nodiscard]] ExecutionPlan compile(RecordedTask&& task, ExecutionCommitProfileSample* profile = nullptr) const;

    private:
        [[nodiscard]] ExecutionPlan compile_owned(RecordedTask& task, ExecutionCommitProfileSample* profile) const;
        static void collect_keep_alive(CompiledSubmission& submission, const CommandIR& command);
    };

    class VulkanCommandEncoder
    {
    public:
        VulkanCommandEncoder() = default;
        explicit VulkanCommandEncoder(VkDevice device);

        void encode(CompiledSubmission& submission) const;

    private:
        VkDevice device_ { VK_NULL_HANDLE };
    };
}
