#include "execution.h"

#include "device_manager.h"

#include <algorithm>
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

        [[nodiscard]] std::uintptr_t resource_id(const ResourceHandle& handle) noexcept
        {
            std::shared_ptr<IResourceRef> token = ResourceBridge::token(handle);
            return token ? token->id() : 0;
        }

        [[nodiscard]] CompiledSubmission& find_or_add_submission(ExecutionPlan& plan,
                                                                 DeviceId device,
                                                                 QueueCapability queue)
        {
            auto found = std::find_if(plan.submissions.begin(), plan.submissions.end(), [device, queue](const CompiledSubmission& submission) {
                return submission.device == device && submission.queue == queue;
            });

            if (found != plan.submissions.end())
            {
                return *found;
            }

            CompiledSubmission submission;
            submission.device = device;
            submission.queue = queue;
            plan.submissions.push_back(std::move(submission));
            return plan.submissions.back();
        }
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
        command.resources.push_back({ src.handle, AccessKind::Read, 0 });
        command.resources.push_back({ dst.handle, AccessKind::Write, 0 });
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
        command.resources.push_back({ shader.handle, AccessKind::Read, 0 });
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

    ExecutionPlan ExecutionCompiler::compile(const RecordedTask& task) const
    {
        ExecutionPlan plan;

        for (const CommandIR& command : task.commands)
        {
            const DeviceId device { 0 };
            CompiledSubmission& submission = find_or_add_submission(plan, device, command.queue);
            collect_barriers(submission, command);
            collect_keep_alive(submission, command);
            submission.commands.push_back(command);
        }

        return plan;
    }

    void ExecutionCompiler::collect_keep_alive(CompiledSubmission& submission, const CommandIR& command)
    {
        for (const ResourceUse& use : command.resources)
        {
            submission.keep_alive.add_resource(ResourceBridge::keep_alive(use.handle));
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
            if (found != last_access.end() && (is_write_access(found->second) || is_write_access(use.access)))
            {
                submission.barriers.push_back({ id, found->second, use.access });
            }
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

    HardwareExecutor::HardwareExecutor(QueueResolver queue_resolver)
        : queue_resolver_(std::move(queue_resolver))
    {
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

        for (CompiledSubmission& compiled_submission : plan.submissions)
        {
            Queue& queue = queue_resolver_(compiled_submission.device, compiled_submission.queue);
            if (!compiled_submission.command_buffer)
            {
                compiled_submission.command_buffer = queue.acquire();
            }

            QueueSubmission queue_submission;
            queue_submission.command_buffer = std::move(compiled_submission.command_buffer);
            queue_submission.keep_alive = std::move(compiled_submission.keep_alive);
            tokens.push_back(queue.submit(queue_submission, {}, {}));
        }

        return tokens;
    }
}
