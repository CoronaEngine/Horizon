#include "execution.h"

#include "device_manager.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace Corona::Horizon
{
    namespace
    {
        [[nodiscard]] bool is_write_access(AccessKind access) noexcept
        {
            return access == AccessKind::Write || access == AccessKind::ReadWrite;
        }

        [[nodiscard]] bool has_hazard(AccessKind before, AccessKind after) noexcept
        {
            return is_write_access(before) || is_write_access(after);
        }

        [[nodiscard]] std::uintptr_t resource_id(const ResourceHandle& handle) noexcept
        {
            std::shared_ptr<IResourceRef> token = ResourceBridge::token(handle);
            return token ? token->id() : 0;
        }

        [[nodiscard]] std::vector<DeviceId> devices_from_mask(DeviceMask mask)
        {
            if (mask.bits == 0)
            {
                throw std::logic_error("DeviceMask must name at least one device.");
            }

            std::vector<DeviceId> devices;
            devices.reserve(32);
            for (uint32_t bit = 0; bit < 32; ++bit)
            {
                if ((mask.bits & (uint32_t { 1 } << bit)) != 0)
                {
                    devices.push_back({ bit });
                }
            }

            return devices;
        }

        [[nodiscard]] bool multi_device(DeviceMask mask) noexcept
        {
            return mask.bits != 0 && (mask.bits & (mask.bits - 1)) != 0;
        }

        [[nodiscard]] size_t find_or_add_submission(ExecutionPlan& plan,
                                                    DeviceId device,
                                                    QueueCapability queue)
        {
            if (!plan.submissions.empty())
            {
                const CompiledSubmission& tail = plan.submissions.back();
                if (tail.device == device && tail.queue == queue)
                {
                    return plan.submissions.size() - 1;
                }
            }

            CompiledSubmission submission;
            submission.device = device;
            submission.queue = queue;
            plan.submissions.push_back(std::move(submission));
            return plan.submissions.size() - 1;
        }

        void add_dependency_once(ExecutionPlan& plan, size_t producer, size_t consumer, std::uintptr_t resource_id)
        {
            if (producer == consumer)
            {
                return;
            }

            const auto found = std::find_if(plan.dependencies.begin(), plan.dependencies.end(), [producer, consumer, resource_id](const SubmissionDependency& dependency) {
                return dependency.producer == producer && dependency.consumer == consumer && dependency.resource_id == resource_id;
            });
            if (found == plan.dependencies.end())
            {
                plan.dependencies.push_back({ producer, consumer, resource_id });
            }
        }

        struct LastResourceUse
        {
            DeviceId device {};
            AccessKind access { AccessKind::Read };
            size_t submission { 0 };
        };

        struct RetireCallback
        {
            explicit RetireCallback(std::function<void()> callback)
                : callback_(std::move(callback))
            {
            }

            ~RetireCallback()
            {
                if (callback_)
                {
                    callback_();
                }
            }

            std::function<void()> callback_;
        };
    }

    CommitCommand commit() noexcept
    {
        return {};
    }

    StreamCommand::StreamCommand(std::function<void(CommandRecorder&)> recorder)
        : recorder_(std::move(recorder))
    {
    }

    void StreamCommand::record(CommandRecorder& recorder) const
    {
        if (recorder_)
        {
            recorder_(recorder);
        }
    }

    CommandBatch& CommandBatch::operator<<(StreamCommand command)
    {
        if (command)
        {
            commands_.push_back(std::move(command));
        }
        return *this;
    }

    void SubmissionKeepAlive::add_resource(std::shared_ptr<const IResourceRef> resource)
    {
        if (!resource)
        {
            return;
        }

        const std::uintptr_t id = resource->id();
        const auto duplicate = std::find_if(resources_.begin(), resources_.end(), [id](const std::shared_ptr<const IResourceRef>& existing) {
            return existing && existing->id() == id;
        });

        if (duplicate == resources_.end())
        {
            resources_.push_back(std::move(resource));
        }
    }

    void SubmissionKeepAlive::add_object(std::shared_ptr<void> object)
    {
        if (object)
        {
            objects_.push_back(std::move(object));
        }
    }

    void SubmissionKeepAlive::merge(SubmissionKeepAlive&& other)
    {
        for (auto& resource : other.resources_)
        {
            add_resource(std::move(resource));
        }

        objects_.insert(objects_.end(),
                        std::make_move_iterator(other.objects_.begin()),
                        std::make_move_iterator(other.objects_.end()));

        other.clear();
    }

    void SubmissionKeepAlive::clear() noexcept
    {
        resources_.clear();
        objects_.clear();
    }

    void CommandRecorder::copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Transfer);

        CommandIR command;
        command.op = CommandOp::CopyBuffer;
        command.devices = devices;
        command.queue = QueueCapability::Transfer;
        command.payload.copy = region;
        command.sequence = next_sequence();
        command.resources.push_back({ src.handle, AccessKind::Read, 0 });
        command.resources.push_back({ dst.handle, AccessKind::Write, 0 });
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::dispatch(ShaderRef shader, DispatchDesc desc, DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Compute);

        CommandIR command;
        command.op = CommandOp::Dispatch;
        command.devices = devices;
        command.queue = QueueCapability::Compute;
        command.payload.dispatch = desc;
        command.sequence = next_sequence();
        command.resources.push_back({ shader.handle, AccessKind::Read, 0 });
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::begin_rendering(RenderingDesc desc, DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Graphics);

        CommandIR command;
        command.op = CommandOp::BeginRendering;
        command.devices = devices;
        command.queue = QueueCapability::Graphics;
        command.payload.rendering = desc;
        command.sequence = next_sequence();
        if (desc.color.handle)
        {
            command.resources.push_back({ desc.color.handle, AccessKind::Write, 0 });
        }
        if (desc.depth.handle)
        {
            command.resources.push_back({ desc.depth.handle, AccessKind::Write, 0 });
        }
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::end_rendering(DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Graphics);

        CommandIR command;
        command.op = CommandOp::EndRendering;
        command.devices = devices;
        command.queue = QueueCapability::Graphics;
        command.sequence = next_sequence();
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::draw_indexed(BufferRef index, BufferRef vertex, DrawIndexedDesc desc, DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Graphics);

        CommandIR command;
        command.op = CommandOp::DrawIndexed;
        command.devices = devices;
        command.queue = QueueCapability::Graphics;
        command.payload.draw_indexed = desc;
        command.sequence = next_sequence();
        command.resources.push_back({ index.handle, AccessKind::Read, 0 });
        command.resources.push_back({ vertex.handle, AccessKind::Read, 0 });
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::present(DisplayerRef displayer, ImageRef image, DeviceId present_device, bool allow_cpu_bridge_fallback)
    {
        ensure_open();
        mark_requirement(QueueCapability::Present);

        DeviceMask devices;
        devices.bits = present_device.value < 32 ? (uint32_t { 1 } << present_device.value) : 0;

        CommandIR command;
        command.op = CommandOp::Present;
        command.devices = devices;
        command.queue = QueueCapability::Present;
        command.payload.present = { displayer, image, present_device, allow_cpu_bridge_fallback };
        command.sequence = next_sequence();
        command.resources.push_back({ image.handle, AccessKind::Read, 0 });
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::host_callback(std::function<void()> callback)
    {
        ensure_open();
        mark_requirement(QueueCapability::Transfer);

        CommandIR command;
        command.op = CommandOp::HostCallback;
        command.queue = QueueCapability::Transfer;
        command.sequence = next_sequence();
        command.host_callback = std::move(callback);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::keep_alive(std::shared_ptr<void> object)
    {
        ensure_open();
        mark_requirement(QueueCapability::Transfer);

        CommandIR command;
        command.op = CommandOp::KeepAlive;
        command.queue = QueueCapability::Transfer;
        command.sequence = next_sequence();
        command.keep_alive = std::move(object);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::require_feature(FeatureRequirement feature)
    {
        ensure_open();
        mark_requirement(feature);
    }

    RecordedTask CommandRecorder::close()
    {
        ensure_open();
        closed_ = true;

        RecordedTask task;
        task.commands = std::move(commands_);
        task.requirements = requirements_;
        return task;
    }

    void CommandRecorder::ensure_open() const
    {
        if (closed_)
        {
            throw std::logic_error("CommandRecorder is already closed.");
        }
    }

    void CommandRecorder::mark_requirement(QueueCapability capability)
    {
        switch (capability)
        {
        case QueueCapability::Graphics:
            requirements_.graphics = true;
            break;
        case QueueCapability::Compute:
            requirements_.compute = true;
            break;
        case QueueCapability::Transfer:
            requirements_.transfer = true;
            break;
        case QueueCapability::Present:
            requirements_.graphics = true;
            break;
        }
    }

    void CommandRecorder::mark_requirement(FeatureRequirement feature)
    {
        switch (feature)
        {
        case FeatureRequirement::TimelineSemaphore:
            requirements_.timeline_semaphore = true;
            break;
        case FeatureRequirement::Synchronization2:
            requirements_.synchronization_2 = true;
            break;
        case FeatureRequirement::DeferredHostOperations:
            requirements_.deferred_host_operations = true;
            break;
        case FeatureRequirement::DeviceGroup:
            requirements_.device_group = true;
            break;
        }
    }

    uint64_t CommandRecorder::next_sequence() noexcept
    {
        return next_sequence_++;
    }

    void CommandRecorder::mark_device_requirements(DeviceMask devices)
    {
        if (multi_device(devices))
        {
            mark_requirement(FeatureRequirement::DeviceGroup);
        }
    }

    StreamCommand copy(BufferRef src, BufferRef dst, CopyRegion region, DeviceMask devices)
    {
        return StreamCommand([src, dst, region, devices](CommandRecorder& recorder) {
            recorder.copy(src, dst, region, devices);
        });
    }

    StreamCommand dispatch(ShaderRef shader, DispatchDesc desc, DeviceMask devices)
    {
        return StreamCommand([shader, desc, devices](CommandRecorder& recorder) {
            recorder.dispatch(shader, desc, devices);
        });
    }

    StreamCommand begin_rendering(RenderingDesc desc, DeviceMask devices)
    {
        return StreamCommand([desc, devices](CommandRecorder& recorder) {
            recorder.begin_rendering(desc, devices);
        });
    }

    StreamCommand end_rendering(DeviceMask devices)
    {
        return StreamCommand([devices](CommandRecorder& recorder) {
            recorder.end_rendering(devices);
        });
    }

    StreamCommand draw_indexed(BufferRef index, BufferRef vertex, DrawIndexedDesc desc, DeviceMask devices)
    {
        return StreamCommand([index, vertex, desc, devices](CommandRecorder& recorder) {
            recorder.draw_indexed(index, vertex, desc, devices);
        });
    }

    StreamCommand present(DisplayerRef displayer, ImageRef image, DeviceId present_device, bool allow_cpu_bridge_fallback)
    {
        return StreamCommand([displayer, image, present_device, allow_cpu_bridge_fallback](CommandRecorder& recorder) {
            recorder.present(displayer, image, present_device, allow_cpu_bridge_fallback);
        });
    }

    StreamCommand host_callback(std::function<void()> callback)
    {
        return StreamCommand([callback = std::move(callback)](CommandRecorder& recorder) mutable {
            recorder.host_callback(std::move(callback));
        });
    }

    StreamCommand keep_alive(std::shared_ptr<void> object)
    {
        return StreamCommand([object = std::move(object)](CommandRecorder& recorder) {
            recorder.keep_alive(object);
        });
    }

    ExecutionPlan ExecutionCompiler::compile(const RecordedTask& task) const
    {
        ExecutionPlan plan;
        std::unordered_map<std::uintptr_t, LastResourceUse> global_last_access;

        for (const CommandIR& command : task.commands)
        {
            const std::vector<DeviceId> devices = devices_from_mask(command.devices);

            for (DeviceId device : devices)
            {
                const size_t submission_index = find_or_add_submission(plan, device, command.queue);
                CompiledSubmission& submission = plan.submissions[submission_index];

                for (const ResourceUse& use : command.resources)
                {
                    const std::uintptr_t id = resource_id(use.handle);
                    if (id == 0)
                    {
                        continue;
                    }

                    const auto found = global_last_access.find(id);
                    if (found == global_last_access.end())
                    {
                        continue;
                    }

                    if (!has_hazard(found->second.access, use.access))
                    {
                        continue;
                    }

                    if (found->second.device == device)
                    {
                        add_dependency_once(plan, found->second.submission, submission_index, id);
                        continue;
                    }

                    if (command.op == CommandOp::Present && command.payload.present.allow_cpu_bridge_fallback)
                    {
                        plan.cross_device_dependencies.push_back({ id,
                                                                    found->second.device,
                                                                    device,
                                                                    true,
                                                                    true });
                        continue;
                    }

                    throw std::logic_error("Cross-device resource dependency requires an imported timeline or explicit present fallback.");
                }

                collect_barriers(submission, command);
                collect_keep_alive(submission, command);
                if (command.op == CommandOp::Present)
                {
                    submission.presents.push_back(command.payload.present);
                }
                if (command.host_callback)
                {
                    submission.host_callbacks.push_back(command.host_callback);
                }
                submission.commands.push_back(command);
            }

            for (DeviceId device : devices)
            {
                const auto submission_found = std::find_if(plan.submissions.begin(), plan.submissions.end(), [device, &command](const CompiledSubmission& submission) {
                    return submission.device == device && submission.queue == command.queue &&
                           !submission.commands.empty() && submission.commands.back().sequence == command.sequence;
                });
                const size_t submission_index = submission_found == plan.submissions.end()
                    ? 0
                    : static_cast<size_t>(std::distance(plan.submissions.begin(), submission_found));

                for (const ResourceUse& use : command.resources)
                {
                    const std::uintptr_t id = resource_id(use.handle);
                    if (id != 0)
                    {
                        global_last_access[id] = { device, use.access, submission_index };
                    }
                }
            }
        }

        return plan;
    }

    void ExecutionCompiler::collect_keep_alive(CompiledSubmission& submission, const CommandIR& command)
    {
        for (const ResourceUse& use : command.resources)
        {
            submission.keep_alive.add_resource(ResourceBridge::keep_alive(use.handle));
        }

        if (command.keep_alive)
        {
            submission.keep_alive.add_object(command.keep_alive);
        }

        if (command.host_callback)
        {
            submission.keep_alive.add_object(std::make_shared<RetireCallback>(command.host_callback));
        }
    }

    void ExecutionCompiler::collect_barriers(CompiledSubmission& submission, const CommandIR& command)
    {
        std::unordered_map<std::uintptr_t, AccessKind> last_access;
        last_access.reserve(submission.commands.size() + command.resources.size());

        for (const CommandIR& prior_command : submission.commands)
        {
            for (const ResourceUse& use : prior_command.resources)
            {
                const std::uintptr_t id = resource_id(use.handle);
                if (id != 0)
                {
                    last_access[id] = use.access;
                }
            }
        }

        for (const ResourceUse& use : command.resources)
        {
            const std::uintptr_t id = resource_id(use.handle);
            if (id == 0)
            {
                continue;
            }

            auto found = last_access.find(id);
            if (found != last_access.end() && has_hazard(found->second, use.access))
            {
                submission.barriers.push_back({ id, found->second, use.access });
            }
        }
    }

    void VulkanCommandEncoder::encode(CompiledSubmission& submission) const
    {
        if (!submission.command_buffer || submission.command_buffer->vk() == VK_NULL_HANDLE)
        {
            return;
        }

        VkCommandBufferBeginInfo begin_info {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult result = vkBeginCommandBuffer(submission.command_buffer->vk(), &begin_info);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkBeginCommandBuffer failed while encoding execution plan.");
        }

        result = vkEndCommandBuffer(submission.command_buffer->vk());
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkEndCommandBuffer failed while encoding execution plan.");
        }
    }

    void CrossDeviceSync::remember_imported_timeline(DeviceId local_device, VkSemaphore foreign, VkSemaphore imported)
    {
        if (foreign == VK_NULL_HANDLE || imported == VK_NULL_HANDLE)
        {
            return;
        }

        auto found = std::find_if(imported_timelines_.begin(), imported_timelines_.end(), [local_device, foreign](const ImportedTimeline& item) {
            return item.local_device == local_device && item.foreign == foreign;
        });

        if (found != imported_timelines_.end())
        {
            found->imported = imported;
            return;
        }

        imported_timelines_.push_back({ local_device, foreign, imported });
    }

    VkSemaphore CrossDeviceSync::resolve_imported_timeline(DeviceId local_device, VkSemaphore foreign) const noexcept
    {
        auto found = std::find_if(imported_timelines_.begin(), imported_timelines_.end(), [local_device, foreign](const ImportedTimeline& item) {
            return item.local_device == local_device && item.foreign == foreign;
        });

        return found == imported_timelines_.end() ? VK_NULL_HANDLE : found->imported;
    }

    HardwareStream::HardwareStream(HardwareExecutor& executor)
        : executor_(&executor)
    {
    }

    HardwareStream& HardwareStream::operator<<(const StreamCommand& command)
    {
        ensure_open();
        command.record(recorder_);
        return *this;
    }

    HardwareStream& HardwareStream::operator<<(const CommandBatch& commands)
    {
        ensure_open();
        for (const StreamCommand& command : commands.commands())
        {
            command.record(recorder_);
        }
        return *this;
    }

    SubmitReceipt HardwareStream::operator<<(CommitCommand)
    {
        return commit();
    }

    SubmitReceipt HardwareStream::commit()
    {
        ensure_open();
        committed_ = true;
        return executor_->commit(recorder_.close());
    }

    RecordedTask HardwareStream::close_for_tests()
    {
        ensure_open();
        committed_ = true;
        return recorder_.close();
    }

    void HardwareStream::ensure_open() const
    {
        if (committed_)
        {
            throw std::logic_error("HardwareStream has already been committed.");
        }
    }

    HardwareExecutor::HardwareExecutor(QueueResolver queue_resolver)
        : queue_resolver_(std::move(queue_resolver))
    {
    }

    HardwareStream HardwareExecutor::stream()
    {
        return HardwareStream(*this);
    }

    ExecutionPlan HardwareExecutor::compile(const RecordedTask& task) const
    {
        return compiler_.compile(task);
    }

    std::vector<SubmissionToken> HardwareExecutor::submit(ExecutionPlan& plan) const
    {
        if (!queue_resolver_)
        {
            throw std::logic_error("HardwareExecutor requires a queue resolver before submit().");
        }

        std::vector<SubmissionToken> tokens;
        tokens.reserve(plan.submissions.size());
        std::vector<std::optional<SubmissionToken>> submitted_tokens(plan.submissions.size());

        for (size_t submission_index = 0; submission_index < plan.submissions.size(); ++submission_index)
        {
            CompiledSubmission& compiled_submission = plan.submissions[submission_index];
            Queue& queue = queue_resolver_(compiled_submission.device, compiled_submission.queue);
            if (!compiled_submission.command_buffer)
            {
                compiled_submission.command_buffer = queue.acquire();
            }

            VulkanCommandEncoder encoder;
            encoder.encode(compiled_submission);

            std::vector<SubmitWait> waits = compiled_submission.waits;
            for (const SubmissionDependency& dependency : plan.dependencies)
            {
                if (dependency.consumer != submission_index)
                {
                    continue;
                }

                const std::optional<SubmissionToken>& token = submitted_tokens[dependency.producer];
                if (token && token->timeline != VK_NULL_HANDLE)
                {
                    waits.push_back({ token->timeline, token->value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT });
                }
            }

            QueueSubmission queue_submission;
            queue_submission.command_buffer = std::move(compiled_submission.command_buffer);
            queue_submission.keep_alive = std::move(compiled_submission.keep_alive);
            tokens.push_back(queue.submit(queue_submission, waits, compiled_submission.signals));
            submitted_tokens[submission_index] = tokens.back();
        }

        return tokens;
    }

    SubmitReceipt HardwareExecutor::commit(const RecordedTask& task)
    {
        ExecutionPlan plan = compile(task);

        SubmitReceipt receipt;
        receipt.serial = ++next_submit_serial_;
        receipt.tokens = submit(plan);

        for (const CompiledSubmission& submission : plan.submissions)
        {
            for (const PresentDesc& present_desc : submission.presents)
            {
                PresentResult result;
                result.status = PresentStatus::Skipped;
                result.displayer = present_desc.displayer;
                result.image = present_desc.image;
                result.message = "Present node was recorded in the execution graph; no swapchain display manager is bound yet.";
                receipt.presents.push_back(std::move(result));
            }
        }

        return receipt;
    }
}
