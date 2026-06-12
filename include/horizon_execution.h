#pragma once

// ====================================================================
// Horizon 执行 / 命令 IR 层（内部 / 高级 API）
// --------------------------------------------------------------------
// 本头文件集中存放声明式执行模型的内部类型：命令 IR、录制器、
// 提交令牌与同步原语。普通用户通常不需要直接构造这些类型——它们
// 由 horizon.h 中的资源 / 管线 / 命令门面在内部使用。
//
// 仅在以下高级场景需要直接接触本头的类型：
//   - 自定义 QueueResolver / 多队列调度
//   - 手动 record() -> compile() -> submit() 三段式
//   - 跨执行器 / 跨设备依赖编排
//
// horizon.h 会自动包含本头，无需单独引入。
// ====================================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "format.h"
#include "resource.h"

namespace Corona::Horizon
{
    // ================================================================
    // Execution / Command Public Types
    // ================================================================

    class Queue;
    class CommandRecorder;
    class ExecutionCompiler;
    struct ExecutionPlan;
    class SubmissionSync;

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
        Present,
        HostCallback,
        KeepAlive,
        CopyImage,
        CopyImageToBuffer
    };

    enum class FeatureRequirement
    {
        TimelineSemaphore,
        Synchronization2,
        DeferredHostOperations,
        DeviceGroup
    };

    struct DeviceId
    {
        uint32_t value { 0 };

        [[nodiscard]] friend bool operator==(DeviceId left, DeviceId right) noexcept
        {
            return left.value == right.value;
        }
    };

    struct DeviceMask
    {
        uint32_t bits { 1 };
    };

    struct QueueId
    {
        DeviceId device {};
        uint32_t family_index { 0 };
        uint32_t queue_index { 0 };
        QueueCapability capability { QueueCapability::Transfer };
    };

    struct BufferRef
    {
        ResourceHandle handle {};
    };

    struct ShaderRef
    {
        ResourceHandle handle {};
    };

    struct ImageRef
    {
        ResourceHandle handle {};
    };

    struct DisplayerRef
    {
        std::uintptr_t id { 0 };
    };

    struct CopyRegion
    {
        uint64_t src_offset { 0 };
        uint64_t dst_offset { 0 };
        uint64_t size { 0 };
    };

    struct BufferImageCopyRegion
    {
        uint64_t buffer_offset { 0 };
        uint32_t image_layer { 0 };
        uint32_t image_mip { 0 };
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
    };

    struct DispatchDesc
    {
        uint32_t groups_x { 1 };
        uint32_t groups_y { 1 };
        uint32_t groups_z { 1 };
        std::vector<DispatchResourceBinding> bindings;
        std::vector<ResourceUse> resource_uses;
        std::vector<std::byte> push_constant_data;
        std::vector<UniformBufferBindingData> uniform_buffers;
    };

    enum class DrawBindingKind
    {
        SampledImage
    };

    struct DrawResourceBinding
    {
        uint32_t set { 0 };
        uint32_t binding { 0 };
        ResourceHandle resource {};
        DrawBindingKind kind { DrawBindingKind::SampledImage };
        AccessKind access { AccessKind::Read };
    };

    struct RenderingDesc
    {
        ImageRef color {};
        ImageRef depth {};
        uint32_t width { 0 };
        uint32_t height { 0 };
        bool clear_color { false };
        bool clear_depth { false };
    };

    struct DrawIndexedDesc
    {
        ResourceHandle pipeline {};
        uint32_t index_count { 0 };
        uint32_t instance_count { 1 };
        uint32_t first_index { 0 };
        int32_t vertex_offset { 0 };
        uint32_t first_instance { 0 };
        IndexType index_type { IndexType::Auto };
        bool enable_scissor { false };
        ScissorRect scissor {};
        std::vector<DrawResourceBinding> bindings;
        std::vector<ResourceUse> resource_uses;
        std::vector<std::byte> push_constant_data;
        std::vector<UniformBufferBindingData> uniform_buffers;
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
        PresentDesc present {};
    };

    struct CommandIR
    {
        CommandOp op { CommandOp::CopyBuffer };
        DeviceMask devices {};
        QueueCapability queue { QueueCapability::Transfer };
        std::vector<ResourceUse> resources;
        std::vector<FeatureRequirement> features;
        CommandPayload payload {};
        std::function<void()> host_callback {};
        std::shared_ptr<void> keep_alive {};
        uint64_t sequence { 0 };
    };

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

        [[nodiscard]] bool empty() const noexcept { return commands.empty(); }
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
        void merge(SubmissionKeepAlive&& other);
        void clear() noexcept;

        [[nodiscard]] size_t resource_count() const noexcept { return resources_.size(); }
        [[nodiscard]] size_t object_count() const noexcept { return objects_.size(); }
        [[nodiscard]] bool empty() const noexcept { return resources_.empty() && objects_.empty(); }

    private:
        std::vector<std::shared_ptr<const IResourceRef>> resources_;
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
    };

    struct SubmitReceipt
    {
        uint64_t serial { 0 };
        std::vector<SubmissionToken> tokens;
        std::vector<PresentResult> presents;

        [[nodiscard]] bool empty() const noexcept
        {
            return tokens.empty() && presents.empty();
        }
    };

    struct CommitCommand
    {
    };

    [[nodiscard]] CommitCommand commit() noexcept;

    class StreamCommand
    {
    public:
        StreamCommand() = default;
        explicit StreamCommand(std::function<void(CommandRecorder&)> recorder);

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
        [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }

    private:
        std::vector<StreamCommand> commands_;
    };

    class CommandRecorder
    {
    public:
        CommandRecorder() = default;

        void copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices = {});
        void copy_image(ImageRef src, ImageRef dst, ImageCopyRegion region, DeviceMask devices = {});
        void copy_to_image(BufferRef src, ImageRef dst, BufferImageCopyRegion region, DeviceMask devices = {});
        void copy_to_buffer(ImageRef src, BufferRef dst, BufferImageCopyRegion region, DeviceMask devices = {});
        void dispatch(ShaderRef shader, DispatchDesc desc, DeviceMask devices = {});
        void begin_rendering(RenderingDesc desc, DeviceMask devices = {});
        void end_rendering(DeviceMask devices = {});
        void draw_indexed(BufferRef index, BufferRef vertex, DrawIndexedDesc desc, DeviceMask devices = {});
        void present(DisplayerRef displayer, ImageRef image, DeviceId present_device = {}, bool allow_cpu_bridge_fallback = true);
        void host_callback(std::function<void()> callback);
        void keep_alive(std::shared_ptr<void> object);
        void require_feature(FeatureRequirement feature);
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
}
