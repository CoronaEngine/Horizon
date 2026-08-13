#include "execution.h"

#include <horizon.h>  // profiling macros merged from horizon_profiling.h

#include "device_manager.h"
#include "hardware_wrapper/diagnostics.h"
#include "hardware_wrapper_vulkan/display/display_manager.h"
#include "hardware_wrapper_vulkan/hardware_context.h"
#include "hardware_wrapper_vulkan/pipeline/vulkan_compute_pipeline.h"
#include "hardware_wrapper_vulkan/pipeline/vulkan_rasterizer_pipeline.h"
#include "hardware_wrapper_vulkan/resource_pool.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <cstdlib>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

namespace Corona::Horizon
{
    SubmissionSync::SubmissionSync(VkSemaphore timeline) noexcept
        : timeline_(timeline)
    {
    }

    std::shared_ptr<SubmissionSync> SubmissionSync::make(VkSemaphore timeline)
    {
        if (timeline == VK_NULL_HANDLE)
            return {};

        return std::shared_ptr<SubmissionSync>(new SubmissionSync(timeline));
    }

    namespace
    {
        [[nodiscard]] VkSemaphore timeline_semaphore(const SubmissionToken& token) noexcept
        {
            return token.sync ? token.sync->timeline() : VK_NULL_HANDLE;
        }

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

        [[nodiscard]] bool same_queue(const QueueId& left, const QueueId& right) noexcept
        {
            return left.device == right.device &&
                   left.family_index == right.family_index &&
                   left.queue_index == right.queue_index;
        }

        void add_wait_once(std::vector<SubmitWait>& waits, SubmitWait wait)
        {
            if (wait.semaphore == VK_NULL_HANDLE)
            {
                return;
            }

            auto found = std::find_if(waits.begin(), waits.end(), [wait](const SubmitWait& item) {
                return item.semaphore == wait.semaphore;
            });
            if (found != waits.end())
            {
                found->value = std::max(found->value, wait.value);
                found->stages |= wait.stages;
                return;
            }

            waits.push_back(wait);
        }

        void add_signal_once(std::vector<SubmitSignal>& signals, SubmitSignal signal)
        {
            if (signal.semaphore == VK_NULL_HANDLE)
            {
                return;
            }

            auto found = std::find_if(signals.begin(), signals.end(), [signal](const SubmitSignal& item) {
                return item.semaphore == signal.semaphore;
            });
            if (found != signals.end())
            {
                found->value = std::max(found->value, signal.value);
                found->stages |= signal.stages;
                return;
            }

            signals.push_back(signal);
        }

        [[nodiscard]] const char* command_op_name(CommandOp op) noexcept
        {
            switch (op)
            {
            case CommandOp::CopyBuffer: return "CopyBuffer";
            case CommandOp::CopyImage: return "CopyImage";
            case CommandOp::CopyBufferToImage: return "CopyBufferToImage";
            case CommandOp::Dispatch: return "Dispatch";
            case CommandOp::BeginRendering: return "BeginRendering";
            case CommandOp::EndRendering: return "EndRendering";
            case CommandOp::DrawIndexed: return "DrawIndexed";
            case CommandOp::DrawIndexedBatch: return "DrawIndexedBatch";
            case CommandOp::Present: return "Present";
            case CommandOp::KeepAlive: return "KeepAlive";
            default: return "Unknown";
            }
        }

        [[nodiscard]] const char* queue_capability_name(QueueCapability capability) noexcept
        {
            switch (capability)
            {
            case QueueCapability::Graphics: return "Graphics";
            case QueueCapability::Compute: return "Compute";
            case QueueCapability::Transfer: return "Transfer";
            case QueueCapability::Present: return "Present";
            default: return "Unknown";
            }
        }

        [[nodiscard]] std::string shorten_label(std::string label)
        {
            constexpr size_t kMaxLabel = 512;
            if (label.size() > kMaxLabel)
            {
                label.resize(kMaxLabel);
                label += "...";
            }
            return label;
        }

        [[nodiscard]] std::string describe_command_for_diagnostics(const CommandIR& command)
        {
            std::ostringstream out;
            out << "#" << command.sequence << " " << command_op_name(command.op);
            if (!command.debug_label.empty())
            {
                out << " label=\"" << shorten_label(command.debug_label) << "\"";
            }

            if (command.op == CommandOp::Dispatch)
            {
                const DispatchDesc& dispatch = command.payload.dispatch;
                out << " groups=" << dispatch.groups_x << "x" << dispatch.groups_y << "x" << dispatch.groups_z;
                if (!command.resources.empty())
                {
                    out << " pipeline=" << resource_id(command.resources[0].handle);
                }
            }
            else if (command.op == CommandOp::DrawIndexed)
            {
                const DrawIndexedDesc& draw = command.payload.draw_indexed;
                out << " index_count=" << draw.index_count
                    << " instances=" << draw.instance_count
                    << " first_index=" << draw.first_index
                    << " vertex_offset=" << draw.vertex_offset
                    << " pipeline=" << resource_id(*draw.pipeline);
                const size_t buffer_offset = draw.pipeline ? 1u : 0u;
                if (command.resources.size() > buffer_offset)
                {
                    out << " index_resource=" << resource_id(command.resources[buffer_offset].handle);
                }
                if (command.resources.size() > buffer_offset + 1u)
                {
                    out << " vertex_resource=" << resource_id(command.resources[buffer_offset + 1u].handle);
                }
            }
            else if (command.op == CommandOp::DrawIndexedBatch)
            {
                out << " draws=" << command.payload.draw_indexed_batch.draws.size()
                    << " unique_resources=" << command.resources.size();
            }
            else if (command.op == CommandOp::Present)
            {
                out << " displayer=" << command.payload.present.displayer.id
                    << " image=" << resource_id(command.payload.present.image.handle)
                    << " swapchain_image=" << resource_id(command.payload.present.swapchain_image.handle);
            }

            return out.str();
        }

        [[nodiscard]] std::string describe_submission_for_diagnostics(const CompiledSubmission& submission)
        {
            std::ostringstream out;
            out << "submission device=" << submission.device.value
                << " capability=" << queue_capability_name(submission.queue)
                << " commands=" << submission.commands.size();

            constexpr size_t kMaxCommands = 24;
            const size_t begin = submission.commands.size() > kMaxCommands
                ? submission.commands.size() - kMaxCommands
                : 0;
            for (size_t i = begin; i < submission.commands.size(); ++i)
            {
                out << "\n  " << describe_command_for_diagnostics(submission.commands[i]);
            }
            return out.str();
        }

        class DebugUtilsLabelScope
        {
        public:
            DebugUtilsLabelScope(VkCommandBuffer command_buffer, const std::string& label) noexcept
                : command_buffer_(command_buffer)
            {
                if (vkCmdBeginDebugUtilsLabelEXT == nullptr ||
                    vkCmdEndDebugUtilsLabelEXT == nullptr ||
                    command_buffer_ == VK_NULL_HANDLE ||
                    label.empty())
                {
                    return;
                }

                label_ = shorten_label(label);
                VkDebugUtilsLabelEXT info {};
                info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
                info.pLabelName = label_.c_str();
                info.color[0] = 0.12f;
                info.color[1] = 0.72f;
                info.color[2] = 1.0f;
                info.color[3] = 1.0f;
                vkCmdBeginDebugUtilsLabelEXT(command_buffer_, &info);
                active_ = true;
            }

            ~DebugUtilsLabelScope()
            {
                if (active_)
                {
                    vkCmdEndDebugUtilsLabelEXT(command_buffer_);
                }
            }

            DebugUtilsLabelScope(const DebugUtilsLabelScope&) = delete;
            DebugUtilsLabelScope& operator=(const DebugUtilsLabelScope&) = delete;

        private:
            VkCommandBuffer command_buffer_ { VK_NULL_HANDLE };
            std::string label_;
            bool active_ { false };
        };

        [[nodiscard]] AccessKind merge_access(AccessKind left, AccessKind right) noexcept
        {
            if (left == right)
            {
                return left;
            }
            if (left == AccessKind::ReadWrite || right == AccessKind::ReadWrite)
            {
                return AccessKind::ReadWrite;
            }
            return AccessKind::ReadWrite;
        }

        using BufferStore = ResourceStore<BufferWrap, BufferReleaser>;
        using ImageStore = ResourceStore<ImageWrap, ImageReleaser>;
        using ComputePipelineStore = ResourceStore<ComputePipelineWrap, NoopReleaser>;
        using RasterizerPipelineStore = ResourceStore<RasterizerPipelineWrap, NoopReleaser>;

        [[nodiscard]] BufferStore::Read read_buffer(const ResourceHandle& handle)
        {
            return read<BufferStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] BufferStore::Write write_buffer(const ResourceHandle& handle)
        {
            return write<BufferStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] ImageStore::Write write_image(const ResourceHandle& handle)
        {
            return write<ImageStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] RasterizerPipelineStore::Read read_rasterizer_pipeline(const ResourceHandle& handle)
        {
            return read<RasterizerPipelineStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] ComputePipelineStore::Read read_compute_pipeline(const ResourceHandle& handle)
        {
            return read<ComputePipelineStore>(ResourceBridge::token(handle));
        }

        [[nodiscard]] uint32_t resolve_layer_count(const ImageWrap& image) noexcept
        {
            const ImageSubresourceRange& range = image.range;
            if (range.layer_count == ImageSubresourceRange::remaining)
                return range.base_layer < image.desc.array_layers ? image.desc.array_layers - range.base_layer : 0;

            return range.layer_count;
        }

        [[nodiscard]] uint32_t resolve_mip_count(const ImageWrap& image) noexcept
        {
            const ImageSubresourceRange& range = image.range;
            if (range.mip_count == ImageSubresourceRange::remaining)
                return range.base_mip < image.desc.mip_levels ? image.desc.mip_levels - range.base_mip : 0;

            return range.mip_count;
        }

        [[nodiscard]] VkImageSubresourceRange vk_subresource_range(const ImageWrap& image) noexcept
        {
            return {
                .aspectMask = image.aspect_mask,
                .baseMipLevel = image.range.base_mip,
                .levelCount = resolve_mip_count(image),
                .baseArrayLayer = image.range.base_layer,
                .layerCount = resolve_layer_count(image),
            };
        }

        [[nodiscard]] VkImageSubresourceLayers vk_subresource_layers(const ImageWrap& image, uint32_t layer, uint32_t mip) noexcept
        {
            return {
                .aspectMask = image.aspect_mask,
                .mipLevel = mip,
                .baseArrayLayer = layer,
                .layerCount = 1,
            };
        }

        [[nodiscard]] VkExtent3D mip_extent(const ImageWrap& image, uint32_t mip) noexcept
        {
            if (mip >= image.desc.mip_levels)
            {
                return {};
            }

            const auto shrink = [mip](uint32_t value) noexcept {
                if (mip >= std::numeric_limits<uint32_t>::digits)
                    return 1u;

                return std::max(1u, value >> mip);
            };

            return {
                .width = shrink(image.desc.extent.width),
                .height = shrink(image.desc.extent.height),
                .depth = image.desc.dimension == ImageDimension::Image3D ? shrink(image.desc.extent.depth) : 1u,
            };
        }

        [[nodiscard]] VkExtent3D min_extent(VkExtent3D lhs, VkExtent3D rhs) noexcept
        {
            return {
                .width = std::min(lhs.width, rhs.width),
                .height = std::min(lhs.height, rhs.height),
                .depth = std::min(lhs.depth, rhs.depth),
            };
        }

        [[nodiscard]] VkBufferImageCopy buffer_image_region(const ImageWrap& image, BufferImageCopyRegion region) noexcept
        {
            VkBufferImageCopy copy {};
            copy.bufferOffset = region.buffer_offset;
            copy.bufferRowLength = 0;
            copy.bufferImageHeight = 0;
            copy.imageSubresource = vk_subresource_layers(image, region.image_layer, region.image_mip);
            copy.imageOffset = { 0, 0, 0 };
            copy.imageExtent = mip_extent(image, region.image_mip);
            return copy;
        }

        struct ImageBarrierScope
        {
            VkPipelineStageFlags2 stage { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT };
            VkAccessFlags2 access { VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT };
        };

        [[nodiscard]] constexpr VkPipelineStageFlags2 image_transfer_stages() noexcept
        {
            return VK_PIPELINE_STAGE_2_COPY_BIT | VK_PIPELINE_STAGE_2_BLIT_BIT;
        }

        [[nodiscard]] ImageBarrierScope source_scope_for_layout(VkImageLayout layout, VkPipelineStageFlags2 fallback_stage) noexcept
        {
            switch (layout)
            {
            case VK_IMAGE_LAYOUT_UNDEFINED:
                return { fallback_stage, 0 };
            case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
                return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0 };
            case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
                return { image_transfer_stages(), VK_ACCESS_2_TRANSFER_WRITE_BIT };
            case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
                return { image_transfer_stages(), VK_ACCESS_2_TRANSFER_READ_BIT };
            case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
                return { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT };
            case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
            case VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL:
                return {
                    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                };
            case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
                return { VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };
            case VK_IMAGE_LAYOUT_GENERAL:
                return {
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                };
            default:
                return {
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
                };
            }
        }

        void transition_image(VkCommandBuffer command_buffer,
                              ImageWrap& image,
                              VkImageLayout new_layout,
                              VkPipelineStageFlags2 dst_stage,
                              VkAccessFlags2 dst_access,
                              VkPipelineStageFlags2 source_fallback_stage = VK_PIPELINE_STAGE_2_NONE)
        {
            if (image.image_handle == VK_NULL_HANDLE || image.image_layout == new_layout)
                return;

            const VkPipelineStageFlags2 fallback_stage = source_fallback_stage != VK_PIPELINE_STAGE_2_NONE ? source_fallback_stage : dst_stage;
            const ImageBarrierScope source_scope = source_scope_for_layout(image.image_layout, fallback_stage);

            VkImageMemoryBarrier2 barrier {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = source_scope.stage;
            barrier.srcAccessMask = source_scope.access;
            barrier.dstStageMask = dst_stage;
            barrier.dstAccessMask = dst_access;
            barrier.oldLayout = image.image_layout;
            barrier.newLayout = new_layout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.image_handle;
            barrier.subresourceRange = vk_subresource_range(image);

            VkDependencyInfo dependency {};
            dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers = &barrier;

            if (std::getenv("HORIZON_DEBUG_SWAPCHAIN_BARRIERS") != nullptr &&
                image.desc.debug_name.find("horizon.swapchain") != std::string::npos)
            {
                Diagnostics::write(Diagnostics::Level::Info,
                                   "HORIZON BARRIER",
                                   image.desc.debug_name +
                                       " old=" + std::to_string(static_cast<int>(barrier.oldLayout)) +
                                       " new=" + std::to_string(static_cast<int>(barrier.newLayout)) +
                                       " srcStage=" + std::to_string(static_cast<unsigned long long>(barrier.srcStageMask)) +
                                       " srcAccess=" + std::to_string(static_cast<unsigned long long>(barrier.srcAccessMask)) +
                                       " dstStage=" + std::to_string(static_cast<unsigned long long>(barrier.dstStageMask)) +
                                       " dstAccess=" + std::to_string(static_cast<unsigned long long>(barrier.dstAccessMask)));
            }

            vkCmdPipelineBarrier2(command_buffer, &dependency);
            image.image_layout = new_layout;
        }

        [[nodiscard]] VkIndexType resolve_index_type(const DrawIndexedDesc& draw, const BufferWrap& index_buffer) noexcept
        {
            switch (draw.index_type)
            {
            case IndexType::UInt32:
                return VK_INDEX_TYPE_UINT32;
            case IndexType::UInt16:
                return VK_INDEX_TYPE_UINT16;
            case IndexType::Auto:
            default:
                return index_buffer.desc.element_size == sizeof(uint32_t) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
            }
        }

        [[nodiscard]] VkRect2D full_scissor(uint32_t width, uint32_t height) noexcept
        {
            return {
                .offset = { 0, 0 },
                .extent = { width, height },
            };
        }

        [[nodiscard]] VkRect2D draw_scissor(const DrawIndexedDesc& draw, uint32_t width, uint32_t height) noexcept
        {
            if (!draw.enable_scissor)
                return full_scissor(width, height);

            const int32_t x = std::max<int32_t>(0, draw.scissor.x);
            const int32_t y = std::max<int32_t>(0, draw.scissor.y);
            const int32_t right = std::min<int32_t>(static_cast<int32_t>(width),
                                                    draw.scissor.x + static_cast<int32_t>(draw.scissor.width));
            const int32_t bottom = std::min<int32_t>(static_cast<int32_t>(height),
                                                     draw.scissor.y + static_cast<int32_t>(draw.scissor.height));

            if (right <= x || bottom <= y)
                return { .offset = { 0, 0 }, .extent = { 0, 0 } };

            return {
                .offset = { x, y },
                .extent = { static_cast<uint32_t>(right - x), static_cast<uint32_t>(bottom - y) },
            };
        }

        [[nodiscard]] VkRect2D draw_scissor(bool enable_scissor, const ScissorRect& scissor, uint32_t width, uint32_t height) noexcept
        {
            DrawIndexedDesc draw;
            draw.enable_scissor = enable_scissor;
            draw.scissor = scissor;
            return draw_scissor(draw, width, height);
        }

        [[nodiscard]] std::shared_ptr<VulkanRasterizerPipeline> rasterizer_impl(const ResourceHandle& handle)
        {
            auto pipeline = read_rasterizer_pipeline(handle);
            if (!pipeline || !pipeline->impl)
                throw std::logic_error("DrawIndexed command requires a valid RasterizerPipeline.");

            return std::static_pointer_cast<VulkanRasterizerPipeline>(pipeline->impl);
        }

        [[nodiscard]] std::shared_ptr<VulkanComputePipeline> compute_impl(const ResourceHandle& handle)
        {
            auto pipeline = read_compute_pipeline(handle);
            if (!pipeline || !pipeline->impl)
                throw std::logic_error("Dispatch command requires a valid ComputePipeline.");

            return std::static_pointer_cast<VulkanComputePipeline>(pipeline->impl);
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
                CompiledSubmission& tail = plan.submissions.back();
                if (tail.device == device && tail.queue == queue)
                {
                    return plan.submissions.size() - 1;
                }

                const bool graphics_compute_mix =
                    (tail.queue == QueueCapability::Graphics && queue == QueueCapability::Compute) ||
                    (tail.queue == QueueCapability::Compute && queue == QueueCapability::Graphics);
                if (tail.device == device && graphics_compute_mix)
                {
                    if (queue == QueueCapability::Graphics)
                        tail.queue = QueueCapability::Graphics;
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

        struct SubmittedResourceUse
        {
            AccessKind access { AccessKind::Read };
            SubmissionToken token {};
        };

        class ResourceSubmissionTracker
        {
        public:
            class Guard
            {
            public:
                Guard(ResourceSubmissionTracker& tracker,
                      const CompiledSubmission& submission,
                      const QueueId& queue,
                      std::vector<SubmitWait>& waits)
                    : tracker_(tracker),
                      lock_(tracker_.mutex_),
                      access_(collect_access(submission))
                {
                    tracker_.append_waits_locked(access_, queue, waits);
                }

                Guard(const Guard&) = delete;
                Guard& operator=(const Guard&) = delete;

                void remember(const SubmissionToken& token)
                {
                    tracker_.remember_locked(access_, token);
                }

            private:
                ResourceSubmissionTracker& tracker_;
                std::unique_lock<std::mutex> lock_;
                std::unordered_map<std::uintptr_t, AccessKind> access_;
            };

            [[nodiscard]] Guard lock_submission(const CompiledSubmission& submission,
                                                const QueueId& queue,
                                                std::vector<SubmitWait>& waits)
            {
                return Guard(*this, submission, queue, waits);
            }

        private:
            static void add_access(std::unordered_map<std::uintptr_t, AccessKind>& access,
                                   const ResourceHandle& handle,
                                   AccessKind kind)
            {
                const std::uintptr_t id = resource_id(handle);
                if (id == 0)
                {
                    return;
                }

                auto [position, inserted] = access.emplace(id, kind);
                if (!inserted)
                {
                    position->second = merge_access(position->second, kind);
                }
            }

            [[nodiscard]] static std::unordered_map<std::uintptr_t, AccessKind> collect_access(const CompiledSubmission& submission)
            {
                std::unordered_map<std::uintptr_t, AccessKind> access;

                for (const CommandIR& command : submission.commands)
                {
                    if (command.op == CommandOp::Present && !command.payload.present.swapchain_image.handle)
                    {
                        continue;
                    }

                    for (const ResourceUse& use : command.resources)
                    {
                        add_access(access, use.handle, use.access);
                    }

                    if (command.op == CommandOp::Present)
                    {
                        add_access(access, command.payload.present.swapchain_image.handle, AccessKind::Write);
                    }
                }

                return access;
            }

            void append_waits_locked(const std::unordered_map<std::uintptr_t, AccessKind>& access,
                                     const QueueId& queue,
                                     std::vector<SubmitWait>& waits) const
            {
                for (const auto& [id, current_access] : access)
                {
                    const auto found = last_uses_.find(id);
                    if (found == last_uses_.end())
                    {
                        continue;
                    }

                    const SubmittedResourceUse& last_use = found->second;
                    if (!has_hazard(last_use.access, current_access) || same_queue(last_use.token.queue, queue))
                    {
                        continue;
                    }

                    const VkSemaphore timeline = timeline_semaphore(last_use.token);
                    if (timeline == VK_NULL_HANDLE || last_use.token.value == 0)
                    {
                        continue;
                    }

                    add_wait_once(waits, { timeline, last_use.token.value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT });
                }
            }

            void remember_locked(const std::unordered_map<std::uintptr_t, AccessKind>& access,
                                 const SubmissionToken& token)
            {
                if (timeline_semaphore(token) == VK_NULL_HANDLE || token.value == 0)
                {
                    return;
                }

                for (const auto& [id, current_access] : access)
                {
                    last_uses_[id] = { current_access, token };
                }
            }

            std::mutex mutex_;
            std::unordered_map<std::uintptr_t, SubmittedResourceUse> last_uses_;
        };

        ResourceSubmissionTracker& resource_submission_tracker()
        {
            static ResourceSubmissionTracker tracker;
            return tracker;
        }
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
            recorder_(recorder);
    }

    void CopyBufferToImageCommand::record(CommandRecorder& recorder) const
    {
        recorder.copy_to_image(src, dst, region, devices);
    }

    void PresentCommand::record(CommandRecorder& recorder) const
    {
        recorder.present(displayer, image, present_device, allow_cpu_bridge_fallback);
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
        if (resource_ids_.insert(id).second)
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

    void SubmissionKeepAlive::clear() noexcept
    {
        resources_.clear();
        resource_ids_.clear();
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

    void CommandRecorder::copy_image(ImageRef src, ImageRef dst, ImageCopyRegion region, DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Transfer);

        CommandIR command;
        command.op = CommandOp::CopyImage;
        command.devices = devices;
        command.queue = QueueCapability::Transfer;
        command.payload.image_copy = region;
        command.sequence = next_sequence();
        command.resources.push_back({ src.handle, AccessKind::Read, 0 });
        command.resources.push_back({ dst.handle, AccessKind::Write, 0 });
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::copy_to_image(BufferRef src, ImageRef dst, BufferImageCopyRegion region, DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Transfer);

        CommandIR command;
        command.op = CommandOp::CopyBufferToImage;
        command.devices = devices;
        command.queue = QueueCapability::Transfer;
        command.payload.buffer_image_copy = region;
        command.sequence = next_sequence();
        command.resources.push_back({ src.handle, AccessKind::Read, 0 });
        command.resources.push_back({ dst.handle, AccessKind::Write, 0 });
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::dispatch(DispatchDesc desc, DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Compute);

        CommandIR command;
        command.op = CommandOp::Dispatch;
        command.devices = devices;
        command.queue = QueueCapability::Compute;
        command.payload.dispatch = desc;
        command.sequence = next_sequence();
        command.debug_label = desc.debug_label;
        //command.resources.push_back({ shader.handle, AccessKind::Read, 0 });
        command.resources.insert(command.resources.end(), desc.resource_uses.begin(), desc.resource_uses.end());
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
        for (const ImageRef& extra_color : desc.extra_colors)
        {
            if (extra_color.handle)
            {
                command.resources.push_back({ extra_color.handle, AccessKind::Write, 0 });
            }
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
        command.sequence = next_sequence();
        command.debug_label = desc.debug_label;
        // if (desc.pipeline)
        // {
        //     command.resources.push_back({ desc.pipeline, AccessKind::Read, 0 });
        // }
        command.resources.push_back({ index.handle, AccessKind::Read, 0 });
        command.resources.push_back({ vertex.handle, AccessKind::Read, 0 });
        command.resources.insert(command.resources.end(), desc.resource_uses.begin(), desc.resource_uses.end());
        command.payload.draw_indexed = std::move(desc);
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::draw_indexed_batch(DrawIndexedBatchDesc batch, DeviceMask devices)
    {
        ensure_open();
        if (batch.draws.empty())
            return;
        mark_requirement(QueueCapability::Graphics);

        CommandIR command;
        command.op = CommandOp::DrawIndexedBatch;
        command.devices = devices;
        command.queue = QueueCapability::Graphics;
        command.sequence = next_sequence();
        std::unordered_map<std::uintptr_t, std::size_t> resource_indices;
        resource_indices.reserve(batch.draws.size() * 3u);
        command.resources.reserve(batch.draws.size() * 3u);
        const auto append_resource = [&](const ResourceUse& incoming) {
            const std::uintptr_t id = resource_id(incoming.handle);
            if (id == 0)
                return;
            const auto [position, inserted] =
                resource_indices.emplace(id, command.resources.size());
            if (inserted)
            {
                command.resources.push_back(incoming);
                return;
            }
            ResourceUse& current = command.resources[position->second];
            current.access = merge_access(current.access, incoming.access);
            current.stages |= incoming.stages;
        };
        for (const DrawIndexedBatchItem& item : batch.draws)
        {
            //append_resource({item.draw.pipeline, AccessKind::Read, 0});
            append_resource({item.index.handle, AccessKind::Read, 0});
            append_resource({item.vertex.handle, AccessKind::Read, 0});
            for (const ResourceUse& use : item.draw.resource_uses)
                append_resource(use);
        }
        command.payload.draw_indexed_batch = std::move(batch);
        mark_device_requirements(devices);
        commands_.push_back(std::move(command));
    }

    void CommandRecorder::draw_indexed_indirect(BufferRef index,
                                                BufferRef vertex,
                                                BufferRef indirect,
                                                DrawIndexedIndirectDesc desc,
                                                DeviceMask devices)
    {
        ensure_open();
        mark_requirement(QueueCapability::Graphics);

        CommandIR command;
        command.op = CommandOp::DrawIndexedIndirect;
        command.devices = devices;
        command.queue = QueueCapability::Graphics;
        command.sequence = next_sequence();
        command.debug_label = desc.debug_label;
        command.resources.push_back({ index.handle, AccessKind::Read, 0 });
        command.resources.push_back({ vertex.handle, AccessKind::Read, 0 });
        command.resources.push_back({ indirect.handle, AccessKind::Read, 0 });
        command.resources.insert(command.resources.end(), desc.resource_uses.begin(), desc.resource_uses.end());
        if (desc.stride == 0)
            desc.stride = static_cast<uint32_t>(sizeof(DrawIndexedIndirectCommand));
        command.payload.draw_indexed_indirect = std::move(desc);
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
        command.payload.present.displayer = displayer;
        command.payload.present.image = image;
        command.payload.present.present_device = present_device;
        command.payload.present.allow_cpu_bridge_fallback = allow_cpu_bridge_fallback;
        command.sequence = next_sequence();
        command.resources.push_back({ image.handle, AccessKind::Read, 0 });
        mark_device_requirements(devices);
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

    ExecutionPlan ExecutionCompiler::compile(RecordedTask&& task, ExecutionCommitProfileSample* profile) const
    {
        return compile_owned(task, profile);
    }

    ExecutionPlan ExecutionCompiler::compile_owned(RecordedTask& task, ExecutionCommitProfileSample* profile) const
    {
        HORIZON_PROFILE_SCOPE_N("Horizon::compile");
        ExecutionPlan plan;
        std::unordered_map<std::uintptr_t, LastResourceUse> global_last_access;

        for (CommandIR& command : task.commands)
        {
            if (profile != nullptr)
            {
                ++profile->commands;
                profile->resource_uses += command.resources.size();
                if (command.op == CommandOp::DrawIndexed)
                {
                    ++profile->logical_draws;
                }
                else if (command.op == CommandOp::DrawIndexedBatch)
                {
                    profile->logical_draws += command.payload.draw_indexed_batch.draws.size();
                    ++profile->draw_batches;
                }
                else if (command.op == CommandOp::DrawIndexedIndirect)
                {
                    profile->logical_draws += command.payload.draw_indexed_indirect.draw_count;
                    ++profile->draw_batches;
                }
            }
            const std::vector<DeviceId> devices = devices_from_mask(command.devices);
            std::vector<size_t> submission_indices;
            submission_indices.reserve(devices.size());

            for (DeviceId device : devices)
            {
                const size_t submission_index = find_or_add_submission(plan, device, command.queue);
                submission_indices.push_back(submission_index);
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

                // 只在开了 profile 时查时钟：每命令两次 steady_clock::now() 在
                // 命令数很多时本身就是可观测开销。
                if (profile != nullptr)
                {
                    const auto keep_alive_start = std::chrono::steady_clock::now();
                    collect_keep_alive(submission, command);
                    profile->keep_alive_ms += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - keep_alive_start).count();
                }
                else
                {
                    collect_keep_alive(submission, command);
                }
                if (command.op == CommandOp::Present)
                {
                    submission.presents.push_back(command.payload.present);
                }
            }

            for (size_t device_index = 0; device_index < devices.size(); ++device_index)
            {
                const DeviceId device = devices[device_index];
                const size_t submission_index = submission_indices[device_index];

                for (const ResourceUse& use : command.resources)
                {
                    const std::uintptr_t id = resource_id(use.handle);
                    if (id != 0)
                    {
                        global_last_access[id] = { device, use.access, submission_index };
                    }
                }
            }

            for (size_t submission_position = 0;
                 submission_position < submission_indices.size();
                 ++submission_position)
            {
                CompiledSubmission& submission =
                    plan.submissions[submission_indices[submission_position]];
                if (submission_position + 1u == submission_indices.size())
                    submission.commands.push_back(std::move(command));
                else
                    submission.commands.push_back(command);
            }
        }

        task.commands.clear();
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
    }

    namespace
    {
        // 单个命令缓冲内、单个绑定点上的描述符绑定状态缓存。
        //
        // bindless 的三个 set（0/1/2）是全进程单例、句柄恒定，descriptor 内容通过
        // UPDATE_AFTER_BIND 就地更新，绑定一次即对后续所有 draw/dispatch 有效。
        // UBO 的 set（3+）现在同形：管线侧按 (binding, buffer, range) 签名分配并写入
        // 一次持久 descriptor set，值的变化走 mapped 内存写入而非重写 descriptor，
        // 所以句柄不变即无需重绑。二者原先都是每 draw/dispatch 无条件重发。
        //
        // 失效规则（保守）：layout 句柄一变就整体清空。Vulkan 只在新 layout 与旧
        // layout 对某 set "不兼容"时才扰乱该 set 的绑定，而 layout 兼容性需要比对
        // 各 set layout 与 push constant range；按句柄相等判定是其充分条件，
        // 代价只是切 pipeline 时多绑一次。graphics 与 compute 是独立绑定点，
        // 各自维护一份状态。
        struct DescriptorBindingCache
        {
            struct BoundUniformSet
            {
                uint32_t set { 0 };
                VkDescriptorSet descriptor_set { VK_NULL_HANDLE };
            };

            VkPipelineLayout layout { VK_NULL_HANDLE };
            bool bindless_bound { false };
            std::vector<BoundUniformSet> bound_uniform_sets;

            void reset() noexcept
            {
                layout = VK_NULL_HANDLE;
                bindless_bound = false;
                bound_uniform_sets.clear();
            }

            // layout 变更即丢弃该绑定点的全部缓存状态。
            void use_layout(VkPipelineLayout next) noexcept
            {
                if (layout == next)
                    return;
                layout = next;
                bindless_bound = false;
                bound_uniform_sets.clear();
            }

            template <typename UniformSetRange>
            [[nodiscard]] bool uniform_sets_match(const UniformSetRange& prepared) const noexcept
            {
                if (bound_uniform_sets.size() != prepared.size())
                    return false;
                for (size_t i = 0; i < bound_uniform_sets.size(); ++i)
                {
                    const BoundUniformSet& cached = bound_uniform_sets[i];
                    const auto& next = prepared[i];
                    if (cached.set != next.set || cached.descriptor_set != next.descriptor_set)
                        return false;
                }
                return true;
            }

            template <typename UniformSetRange>
            void remember_uniform_sets(const UniformSetRange& prepared)
            {
                bound_uniform_sets.clear();
                bound_uniform_sets.reserve(prepared.size());
                for (const auto& next : prepared)
                    bound_uniform_sets.push_back({ next.set, next.descriptor_set });
            }
        };

        // 把已按 set 升序排列的 UBO descriptor set 按连续段合并成尽量少的
        // vkCmdBindDescriptorSets（反射出的 set 号可能不连续，中间是空 layout）。
        template <typename UniformSetRange>
        void bind_uniform_descriptor_sets(VkCommandBuffer command_buffer,
                                          VkPipelineBindPoint bind_point,
                                          VkPipelineLayout layout,
                                          const UniformSetRange& uniform_sets)
        {
            std::vector<VkDescriptorSet> run;
            for (size_t begin = 0; begin < uniform_sets.size();)
            {
                size_t end = begin;
                run.clear();
                while (end < uniform_sets.size() &&
                       uniform_sets[end].set == uniform_sets[begin].set + static_cast<uint32_t>(end - begin))
                {
                    run.push_back(uniform_sets[end].descriptor_set);
                    ++end;
                }

                vkCmdBindDescriptorSets(command_buffer,
                                        bind_point,
                                        layout,
                                        uniform_sets[begin].set,
                                        static_cast<uint32_t>(run.size()),
                                        run.data(),
                                        0,
                                        nullptr);
                begin = end;
            }
        }
    } // namespace

    VulkanCommandEncoder::VulkanCommandEncoder(VkDevice device)
        : device_(device)
    {
    }

    void VulkanCommandEncoder::encode(CompiledSubmission& submission) const
    {
        HORIZON_PROFILE_SCOPE_N("Horizon::encode");
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

        struct ActiveRendering
        {
            bool active { false };
            VkFormat color_format { VK_FORMAT_UNDEFINED };
            std::array<VkFormat, 3> extra_color_formats { VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED };
            VkFormat depth_format { VK_FORMAT_UNDEFINED };
            uint32_t width { 0 };
            uint32_t height { 0 };
            std::array<ResourceHandle, 4> color_handles {};
            uint32_t color_handle_count { 0 };
        } active_rendering;

        VkCommandBuffer command_buffer = submission.command_buffer->vk();

        DescriptorBindingCache graphics_descriptors;
        DescriptorBindingCache compute_descriptors;

        // 一个批次里连续的 draw 绝大多数共用同一 pipeline / IB / VB。每 draw 重新
        // 走一遍 resource store（shared_ptr 拷贝 + dynamic_cast + 槽位 shared_mutex）
        // 和 prepare_draw（管线锁 + 线性查找）在压测下是主要剩余开销。
        //
        // 这里只缓存**取出来的值**（VkBuffer / stride / PreparedDraw），不跨 draw
        // 持有 Read 句柄，因此不会把槽位锁按住、不影响其它线程写同一资源。
        //
        // key 用的是 token 地址，本来会有 ABA 风险（token 释放后新 token 复用同一
        // 地址）；但 submission.keep_alive 已持有本次提交用到的每个资源 token 的
        // shared_ptr，encode 期间它们不可能被释放，故地址在本次 encode 内唯一。
        // 缓存生命周期严格限于单次 encode()，不要提升为成员。
        struct DrawEncodeCache
        {
            const IResourceRef* index_token { nullptr };
            VkBuffer index_buffer { VK_NULL_HANDLE };
            IndexType index_type_request { IndexType::Auto };
            VkIndexType index_type { VK_INDEX_TYPE_UINT32 };

            const IResourceRef* vertex_token { nullptr };
            VkBuffer vertex_buffer { VK_NULL_HANDLE };
            uint32_t vertex_stride { 0 };

            const RasterizerPipelineBase* pipeline_key { nullptr };
            std::shared_ptr<VulkanRasterizerPipeline> pipeline_impl;
            bool prepared_valid { false };
            VulkanRasterizerPipeline::PreparedDraw prepared {};

            void reset() noexcept
            {
                index_token = nullptr;
                vertex_token = nullptr;
                pipeline_key = nullptr;
                pipeline_impl.reset();
                prepared_valid = false;
                prepared = {};
            }

            void invalidate_prepared() noexcept
            {
                prepared_valid = false;
                prepared = {};
            }
        } draw_cache;

        // 三个 bindless set 全submission只取一次：省掉每 draw/dispatch 一次
        // ResourceManager 全局锁 + 数组拷贝。
        std::optional<std::array<VkDescriptorSet, ResourceManager::bindless_descriptor_set_count>> bindless_sets_cache;
        auto bindless_sets = [&]() -> const std::array<VkDescriptorSet, ResourceManager::bindless_descriptor_set_count>& {
            if (!bindless_sets_cache.has_value())
                bindless_sets_cache = resource_manager().bindless_descriptor_sets();
            return *bindless_sets_cache;
        };

        auto encode_indexed_draw = [&](BufferRef index_ref,
                                       BufferRef vertex_ref,
                                       const DrawIndexedDesc& draw) {
            DebugUtilsLabelScope debug_label(command_buffer, draw.debug_label);
            if (draw.index_count == 0)
                return;
            if (!draw.pipeline)
                throw std::logic_error("DrawIndexed requires a RasterizerPipeline handle.");
            if (!index_ref.handle || !vertex_ref.handle)
                throw std::logic_error("DrawIndexed command is missing index or vertex buffer resources.");

            // IB / VB：句柄与上一 draw 相同则复用已取出的值，不再进 resource store。
            const IResourceRef* index_token = ResourceBridge::token(index_ref.handle).get();
            if (draw_cache.index_token != index_token || draw_cache.index_type_request != draw.index_type)
            {
                BufferStore::Read index = read_buffer(index_ref.handle);
                if (!index || index->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("DrawIndexed requires a valid index HardwareBuffer.");

                draw_cache.index_token = index_token;
                draw_cache.index_buffer = index->buffer_handle;
                draw_cache.index_type_request = draw.index_type;
                draw_cache.index_type = resolve_index_type(draw, *index);
            }

            const IResourceRef* vertex_token = ResourceBridge::token(vertex_ref.handle).get();
            if (draw_cache.vertex_token != vertex_token)
            {
                BufferStore::Read vertex = read_buffer(vertex_ref.handle);
                if (!vertex || vertex->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("DrawIndexed requires a valid vertex HardwareBuffer.");

                const uint32_t stride = static_cast<uint32_t>(vertex->desc.element_size);
                if (draw_cache.vertex_stride != stride)
                    draw_cache.invalidate_prepared(); // stride 参与 prepare_draw 的 key

                draw_cache.vertex_token = vertex_token;
                draw_cache.vertex_buffer = vertex->buffer_handle;
                draw_cache.vertex_stride = stride;
            }

            auto rasterizerPipeline = draw.pipeline;
            // desc() 深拷贝整个 RasterizerPipelineDesc（含两份 SPIR-V + 反射表），
            // 每 draw 一次在压测下是主要开销。条件信息比较不需要 desc，先比较、
            // 只有真的要 rebuild 时才付这份拷贝。条件相同（或无 pipelineObject，
            // 此时两边都是默认值）时原本也不会 rebuild，语义不变。
            const bool vertex_condition_changed =
                rasterizerPipeline->vertex_condition_info() != draw.vert_condition_info;
            const bool fragment_condition_changed =
                rasterizerPipeline->fragment_condition_info() != draw.frag_condition_info;
            if (vertex_condition_changed || fragment_condition_changed)
            {
                auto desc = rasterizerPipeline->desc();
                if (desc.pipelineObject)
                {
                    auto& vc = desc.pipelineObject->vertex;
                    auto& fc = desc.pipelineObject->fragment;
                    if (vertex_condition_changed)
                        desc.vertex_shader.module = vc->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, vc->getCompilerOption().enableBindless);
                    if (fragment_condition_changed)
                        desc.fragment_shader.module = fc->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, fc->getCompilerOption().enableBindless);

                    desc.pipelineObject->updateAutoBind(vc->getCompilerOption().enableBindless,  draw.vert_condition_info, draw.frag_condition_info);
                    desc.auto_bind_entries = desc.pipelineObject->autoBindEntries;
                    rasterizerPipeline->rebuild_pipeline(std::move(desc), draw.vert_condition_info, draw.frag_condition_info);
                    // 重建可能销毁旧 VulkanRasterizerPipeline 连带其 VkPipelineLayout，
                    // 新建的 layout 有可能复用同一句柄值（ABA）。缓存按句柄相等判定，
                    // 故此处必须整体作废，两个绑定点都清（句柄池是设备级共享的）。
                    graphics_descriptors.reset();
                    compute_descriptors.reset();
                    // rebuild 换掉了 impl 与 layout，memo 里的 pipeline/prepared 全部失效。
                    draw_cache.pipeline_key = nullptr;
                    draw_cache.pipeline_impl.reset();
                    draw_cache.invalidate_prepared();
                }
            }

            if (draw_cache.pipeline_key != rasterizerPipeline || !draw_cache.pipeline_impl)
            {
                draw_cache.pipeline_impl = rasterizer_impl(*draw.pipeline);
                draw_cache.pipeline_key = rasterizerPipeline;
                draw_cache.invalidate_prepared();
            }
            const std::shared_ptr<VulkanRasterizerPipeline>& pipeline = draw_cache.pipeline_impl;

            // prepare_draw 的结果由 (device, color/depth formats, vertex_stride,
            // draw.uniform_buffers) 决定。formats 在 BeginRendering 时固定（那里会清
            // memo），stride 变化上面已作废。uniform_buffers 非空时逐 draw 比较签名不
            // 一定比直接调用便宜，保守起见只在它为空（纹理全 bindless + push constant
            // 的常见路径）时复用。
            if (!draw_cache.prepared_valid || !draw.uniform_buffers.empty())
            {
                const std::array<VkFormat, 4> color_formats {
                    active_rendering.color_format,
                    active_rendering.extra_color_formats[0],
                    active_rendering.extra_color_formats[1],
                    active_rendering.extra_color_formats[2],
                };
                draw_cache.prepared = pipeline->prepare_draw(device_,
                                                            color_formats,
                                                            active_rendering.depth_format,
                                                            draw_cache.vertex_stride,
                                                            draw);
                draw_cache.prepared_valid = draw.uniform_buffers.empty();
            }
            const VulkanRasterizerPipeline::PreparedDraw& prepared = draw_cache.prepared;
            if (prepared.pipeline == VK_NULL_HANDLE || prepared.layout == VK_NULL_HANDLE)
                throw std::logic_error("DrawIndexed resolved an invalid graphics pipeline.");

            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, prepared.pipeline);
            graphics_descriptors.use_layout(prepared.layout);
            if (prepared.uses_bindless && !graphics_descriptors.bindless_bound)
            {
                const auto& sets = bindless_sets();
                vkCmdBindDescriptorSets(command_buffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        prepared.layout,
                                        0,
                                        static_cast<uint32_t>(sets.size()),
                                        sets.data(),
                                        0,
                                        nullptr);
                graphics_descriptors.bindless_bound = true;
            }

            // 非 bindless 的 UBO set 由管线侧分配并写入一次，这里只绑定；
            // 同 layout 下句柄未变则整体跳过（与 bindless set 0-2 同形）。
            if (!graphics_descriptors.uniform_sets_match(prepared.uniform_sets))
            {
                bind_uniform_descriptor_sets(command_buffer,
                                             VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             prepared.layout,
                                             prepared.uniform_sets);
                graphics_descriptors.remember_uniform_sets(prepared.uniform_sets);
            }

            VkBuffer vertex_buffer = draw_cache.vertex_buffer;
            VkDeviceSize vertex_offset = 0;
            vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vertex_offset);
            vkCmdBindIndexBuffer(command_buffer, draw_cache.index_buffer, 0, draw_cache.index_type);

            if (!draw.push_constant_data.empty())
            {
                vkCmdPushConstants(command_buffer,
                                   prepared.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   static_cast<uint32_t>(draw.push_constant_data.size()),
                                   draw.push_constant_data.data());
            }
            VkRect2D scissor = draw_scissor(draw, active_rendering.width, active_rendering.height);
            if (scissor.extent.width == 0 || scissor.extent.height == 0)
                return;
            vkCmdSetScissor(command_buffer, 0, 1, &scissor);
            vkCmdDrawIndexed(command_buffer,
                             draw.index_count,
                             draw.instance_count,
                             draw.first_index,
                             draw.vertex_offset,
                             draw.first_instance);
        };

        auto encode_indexed_indirect_draw = [&](BufferRef index_ref,
                                                BufferRef vertex_ref,
                                                BufferRef indirect_ref,
                                                const DrawIndexedIndirectDesc& draw) {
            DebugUtilsLabelScope debug_label(command_buffer, draw.debug_label);
            if (draw.draw_count == 0)
                return;
            if (!draw.pipeline)
                throw std::logic_error("DrawIndexedIndirect requires a RasterizerPipeline handle.");
            if (!index_ref.handle || !vertex_ref.handle || !indirect_ref.handle)
                throw std::logic_error("DrawIndexedIndirect command is missing index, vertex, or indirect buffer resources.");

            const IResourceRef* index_token = ResourceBridge::token(index_ref.handle).get();
            DrawIndexedDesc prepare_desc;
            prepare_desc.pipeline = draw.pipeline;
            prepare_desc.vert_condition_info = draw.vert_condition_info;
            prepare_desc.frag_condition_info = draw.frag_condition_info;
            prepare_desc.index_count = 1;
            prepare_desc.instance_count = 1;
            prepare_desc.index_type = draw.index_type;
            prepare_desc.enable_scissor = draw.enable_scissor;
            prepare_desc.scissor = draw.scissor;
            prepare_desc.resource_uses = draw.resource_uses;
            prepare_desc.push_constant_data = draw.push_constant_data;
            prepare_desc.uniform_buffers = draw.uniform_buffers;
            prepare_desc.debug_label = draw.debug_label;
            if (draw_cache.index_token != index_token || draw_cache.index_type_request != draw.index_type)
            {
                BufferStore::Read index = read_buffer(index_ref.handle);
                if (!index || index->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("DrawIndexedIndirect requires a valid index HardwareBuffer.");

                draw_cache.index_token = index_token;
                draw_cache.index_buffer = index->buffer_handle;
                draw_cache.index_type_request = draw.index_type;
                draw_cache.index_type = resolve_index_type(prepare_desc, *index);
            }

            const IResourceRef* vertex_token = ResourceBridge::token(vertex_ref.handle).get();
            if (draw_cache.vertex_token != vertex_token)
            {
                BufferStore::Read vertex = read_buffer(vertex_ref.handle);
                if (!vertex || vertex->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("DrawIndexedIndirect requires a valid vertex HardwareBuffer.");

                const uint32_t stride = static_cast<uint32_t>(vertex->desc.element_size);
                if (draw_cache.vertex_stride != stride)
                    draw_cache.invalidate_prepared();

                draw_cache.vertex_token = vertex_token;
                draw_cache.vertex_buffer = vertex->buffer_handle;
                draw_cache.vertex_stride = stride;
            }

            BufferStore::Read indirect = read_buffer(indirect_ref.handle);
            if (!indirect || indirect->buffer_handle == VK_NULL_HANDLE)
                throw std::logic_error("DrawIndexedIndirect requires a valid indirect HardwareBuffer.");

            auto rasterizerPipeline = draw.pipeline;
            const bool vertex_condition_changed =
                rasterizerPipeline->vertex_condition_info() != draw.vert_condition_info;
            const bool fragment_condition_changed =
                rasterizerPipeline->fragment_condition_info() != draw.frag_condition_info;
            if (vertex_condition_changed || fragment_condition_changed)
            {
                auto desc = rasterizerPipeline->desc();
                if (desc.pipelineObject)
                {
                    auto& vc = desc.pipelineObject->vertex;
                    auto& fc = desc.pipelineObject->fragment;
                    if (vertex_condition_changed)
                        desc.vertex_shader.module = vc->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, vc->getCompilerOption().enableBindless);
                    if (fragment_condition_changed)
                        desc.fragment_shader.module = fc->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, fc->getCompilerOption().enableBindless);

                    desc.pipelineObject->updateAutoBind(vc->getCompilerOption().enableBindless, draw.vert_condition_info, draw.frag_condition_info);
                    desc.auto_bind_entries = desc.pipelineObject->autoBindEntries;
                    rasterizerPipeline->rebuild_pipeline(std::move(desc), draw.vert_condition_info, draw.frag_condition_info);
                    graphics_descriptors.reset();
                    compute_descriptors.reset();
                    draw_cache.pipeline_key = nullptr;
                    draw_cache.pipeline_impl.reset();
                    draw_cache.invalidate_prepared();
                }
            }

            if (draw_cache.pipeline_key != rasterizerPipeline || !draw_cache.pipeline_impl)
            {
                draw_cache.pipeline_impl = rasterizer_impl(*draw.pipeline);
                draw_cache.pipeline_key = rasterizerPipeline;
                draw_cache.invalidate_prepared();
            }
            const std::shared_ptr<VulkanRasterizerPipeline>& pipeline = draw_cache.pipeline_impl;

            if (!draw_cache.prepared_valid || !draw.uniform_buffers.empty())
            {
                const std::array<VkFormat, 4> color_formats {
                    active_rendering.color_format,
                    active_rendering.extra_color_formats[0],
                    active_rendering.extra_color_formats[1],
                    active_rendering.extra_color_formats[2],
                };
                draw_cache.prepared = pipeline->prepare_draw(device_,
                                                            color_formats,
                                                            active_rendering.depth_format,
                                                            draw_cache.vertex_stride,
                                                            prepare_desc);
                draw_cache.prepared_valid = draw.uniform_buffers.empty();
            }
            const VulkanRasterizerPipeline::PreparedDraw& prepared = draw_cache.prepared;
            if (prepared.pipeline == VK_NULL_HANDLE || prepared.layout == VK_NULL_HANDLE)
                throw std::logic_error("DrawIndexedIndirect resolved an invalid graphics pipeline.");

            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, prepared.pipeline);
            graphics_descriptors.use_layout(prepared.layout);
            if (prepared.uses_bindless && !graphics_descriptors.bindless_bound)
            {
                const auto& sets = bindless_sets();
                vkCmdBindDescriptorSets(command_buffer,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        prepared.layout,
                                        0,
                                        static_cast<uint32_t>(sets.size()),
                                        sets.data(),
                                        0,
                                        nullptr);
                graphics_descriptors.bindless_bound = true;
            }

            if (!graphics_descriptors.uniform_sets_match(prepared.uniform_sets))
            {
                bind_uniform_descriptor_sets(command_buffer,
                                             VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             prepared.layout,
                                             prepared.uniform_sets);
                graphics_descriptors.remember_uniform_sets(prepared.uniform_sets);
            }

            VkBuffer vertex_buffer = draw_cache.vertex_buffer;
            VkDeviceSize vertex_offset = 0;
            vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vertex_offset);
            vkCmdBindIndexBuffer(command_buffer, draw_cache.index_buffer, 0, draw_cache.index_type);

            if (!draw.push_constant_data.empty())
            {
                vkCmdPushConstants(command_buffer,
                                   prepared.layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0,
                                   static_cast<uint32_t>(draw.push_constant_data.size()),
                                   draw.push_constant_data.data());
            }

            VkRect2D scissor = draw_scissor(draw.enable_scissor, draw.scissor, active_rendering.width, active_rendering.height);
            if (scissor.extent.width == 0 || scissor.extent.height == 0)
                return;
            vkCmdSetScissor(command_buffer, 0, 1, &scissor);

            const uint32_t stride = draw.stride != 0
                ? draw.stride
                : static_cast<uint32_t>(sizeof(DrawIndexedIndirectCommand));
            vkCmdDrawIndexedIndirect(command_buffer,
                                     indirect->buffer_handle,
                                     static_cast<VkDeviceSize>(draw.indirect_offset),
                                     draw.draw_count,
                                     stride);
        };

        for (size_t command_index = 0; command_index < submission.commands.size(); ++command_index)
        {
            const CommandIR& command = submission.commands[command_index];
            switch (command.op)
            {
            case CommandOp::CopyBuffer:
            {
                if (command.resources.size() < 2u)
                    throw std::logic_error("CopyBuffer command is missing source or destination buffers.");

                const CopyRegion& region = command.payload.copy;
                if (region.size == 0)
                    break;

                BufferStore::Read src = read_buffer(command.resources[0].handle);
                BufferStore::Write dst = write_buffer(command.resources[1].handle);
                if (!src || src->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("CopyBuffer requires a valid source HardwareBuffer.");
                if (!dst || dst->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("CopyBuffer requires a valid destination HardwareBuffer.");

                VkBufferCopy copy {};
                copy.srcOffset = region.src_offset;
                copy.dstOffset = region.dst_offset;
                copy.size = region.size;
                vkCmdCopyBuffer(command_buffer, src->buffer_handle, dst->buffer_handle, 1, &copy);
                break;
            }
            case CommandOp::CopyBufferToImage:
            {
                if (command.resources.size() < 2u)
                    throw std::logic_error("CopyBufferToImage command is missing source buffer or destination image.");

                BufferStore::Read src = read_buffer(command.resources[0].handle);
                ImageStore::Write dst = write_image(command.resources[1].handle);
                if (!src || src->buffer_handle == VK_NULL_HANDLE)
                    throw std::logic_error("CopyBufferToImage requires a valid source HardwareBuffer.");
                if (!dst || dst->image_handle == VK_NULL_HANDLE)
                    throw std::logic_error("CopyBufferToImage requires a valid destination HardwareImage.");

                VkBufferImageCopy copy = buffer_image_region(*dst, command.payload.buffer_image_copy);
                if (copy.imageExtent.width == 0 || copy.imageExtent.height == 0 || copy.imageExtent.depth == 0)
                    break;

                transition_image(command_buffer,
                                 *dst,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT);
                vkCmdCopyBufferToImage(command_buffer,
                                       src->buffer_handle,
                                       dst->image_handle,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       1,
                                       &copy);
                break;
            }
            case CommandOp::CopyImage:
            {
                if (command.resources.size() < 2u)
                    throw std::logic_error("CopyImage command is missing source or destination images.");

                ImageStore::Write src = write_image(command.resources[0].handle);
                ImageStore::Write dst = write_image(command.resources[1].handle);
                if (!src || src->image_handle == VK_NULL_HANDLE)
                    throw std::logic_error("CopyImage requires a valid source HardwareImage.");
                if (!dst || dst->image_handle == VK_NULL_HANDLE)
                    throw std::logic_error("CopyImage requires a valid destination HardwareImage.");

                const ImageCopyRegion& region = command.payload.image_copy;
                const VkExtent3D extent = min_extent(mip_extent(*src, region.src_mip),
                                                     mip_extent(*dst, region.dst_mip));
                if (extent.width == 0 || extent.height == 0 || extent.depth == 0)
                    break;

                transition_image(command_buffer,
                                 *src,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 image_transfer_stages(),
                                 VK_ACCESS_2_TRANSFER_READ_BIT);
                transition_image(command_buffer,
                                 *dst,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 image_transfer_stages(),
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT);

                VkImageCopy copy {};
                copy.srcSubresource = vk_subresource_layers(*src, region.src_layer, region.src_mip);
                copy.srcOffset = { 0, 0, 0 };
                copy.dstSubresource = vk_subresource_layers(*dst, region.dst_layer, region.dst_mip);
                copy.dstOffset = { 0, 0, 0 };
                copy.extent = extent;
                vkCmdCopyImage(command_buffer,
                               src->image_handle,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               dst->image_handle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &copy);
                break;
            }
            case CommandOp::Dispatch:
            {
                DebugUtilsLabelScope debug_label(command_buffer, command.debug_label);
                const DispatchDesc& dispatch = command.payload.dispatch;

                {
                    VkMemoryBarrier2 memory_barrier {};
                    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                    memory_barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
                    memory_barrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
                    memory_barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                    memory_barrier.dstAccessMask =
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT |
                        VK_ACCESS_2_SHADER_READ_BIT;

                    VkDependencyInfo dependency {};
                    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dependency.memoryBarrierCount = 1;
                    dependency.pMemoryBarriers = &memory_barrier;
                    vkCmdPipelineBarrier2(command_buffer, &dependency);
                }

                auto computePipeline = command.payload.dispatch.pipeline;
                // 同光栅路径：desc() 深拷贝整个 ComputePipelineDesc（SPIR-V + 反射），
                // 先比条件、只有要 rebuild 时才付这份拷贝。
                if (dispatch.comp_condition_info != computePipeline->compute_condition_info())
                {
                    auto desc = computePipeline->desc();
                    if (desc.pipelineObject)
                    {
                        auto& cc = desc.pipelineObject->compute;
                        desc.compute_shader.module = cc->getShaderCode(EmbeddedShader::ShaderLanguage::SpirV, cc->getCompilerOption().enableBindless);
                        desc.pipelineObject->updateAutoBind(cc->getCompilerOption().enableBindless, dispatch.comp_condition_info);
                        desc.auto_bind_entries = desc.pipelineObject->autoBindEntries;
                        computePipeline->rebuild_pipeline(std::move(desc), dispatch.comp_condition_info);
                        // 同光栅路径：layout 句柄可能被回收复用，缓存整体作废。
                        graphics_descriptors.reset();
                        compute_descriptors.reset();
                        draw_cache.reset();
                    }
                }

                std::shared_ptr<VulkanComputePipeline> pipeline = compute_impl(*command.payload.dispatch.pipeline);

                for (const DispatchResourceBinding& binding : dispatch.bindings)
                {
                    if (binding.kind != DispatchBindingKind::StorageImage)
                    {
                        continue;
                    }

                    ImageStore::Write image = write_image(binding.resource);
                    if (!image || image->image_handle == VK_NULL_HANDLE || image->image_view == VK_NULL_HANDLE)
                    {
                        throw std::logic_error("Dispatch storage image binding requires a valid HardwareImage.");
                    }

                    transition_image(command_buffer,
                                     *image,
                                     VK_IMAGE_LAYOUT_GENERAL,
                                     VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                     VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                }

                VulkanComputePipeline::PreparedDispatch prepared = pipeline->prepare_dispatch(device_, dispatch);
                if (prepared.pipeline == VK_NULL_HANDLE || prepared.layout == VK_NULL_HANDLE)
                    throw std::logic_error("Dispatch resolved an invalid compute pipeline.");

                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, prepared.pipeline);
                compute_descriptors.use_layout(prepared.layout);
                if (prepared.uses_bindless && !compute_descriptors.bindless_bound)
                {
                    const auto& sets = bindless_sets();
                    vkCmdBindDescriptorSets(command_buffer,
                                            VK_PIPELINE_BIND_POINT_COMPUTE,
                                            prepared.layout,
                                            0,
                                            static_cast<uint32_t>(sets.size()),
                                            sets.data(),
                                            0,
                                            nullptr);
                    compute_descriptors.bindless_bound = true;
                }

                // 非 bindless 的 UBO set 由管线侧分配并写入一次，这里只绑定；
                // 同 layout 下句柄未变则整体跳过（理由同光栅路径）。
                if (!compute_descriptors.uniform_sets_match(prepared.uniform_sets))
                {
                    bind_uniform_descriptor_sets(command_buffer,
                                                 VK_PIPELINE_BIND_POINT_COMPUTE,
                                                 prepared.layout,
                                                 prepared.uniform_sets);
                    compute_descriptors.remember_uniform_sets(prepared.uniform_sets);
                }

                if (!dispatch.push_constant_data.empty())
                {
                    if (dispatch.push_constant_data.size() > std::numeric_limits<uint32_t>::max())
                        throw std::overflow_error("Dispatch push constant data exceeds uint32_t.");

                    vkCmdPushConstants(command_buffer,
                                       prepared.layout,
                                       VK_SHADER_STAGE_COMPUTE_BIT,
                                       0,
                                       static_cast<uint32_t>(dispatch.push_constant_data.size()),
                                       dispatch.push_constant_data.data());
                }

                vkCmdDispatch(command_buffer,
                              std::max(1u, dispatch.groups_x),
                              std::max(1u, dispatch.groups_y),
                              std::max(1u, dispatch.groups_z));
                break;
            }
            case CommandOp::BeginRendering:
            {
                const RenderingDesc& rendering = command.payload.rendering;
                std::array<ImageStore::Write, 4> color_writes;
                ImageStore::Write depth;

                {
                    VkMemoryBarrier2 memory_barrier {};
                    memory_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                    memory_barrier.srcStageMask =
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                    memory_barrier.srcAccessMask =
                        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
                    memory_barrier.dstStageMask =
                        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
                        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    memory_barrier.dstAccessMask =
                        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
                        VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                        VK_ACCESS_2_SHADER_READ_BIT;

                    VkDependencyInfo dependency {};
                    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    dependency.memoryBarrierCount = 1;
                    dependency.pMemoryBarriers = &memory_barrier;
                    vkCmdPipelineBarrier2(command_buffer, &dependency);
                }

                // 注意：光栅路径的采样图像不在这里做 layout 转换。纹理已全走 bindless
                // （索引写进 push constant），draw 不再携带 per-draw 的 sampled image
                // 绑定，所以"进 rendering scope 前把后续 draw 要采样的图像转成
                // SHADER_READ_ONLY_OPTIMAL"这件事已无从下手，原先的前瞻实现恒为
                // no-op，已删除。约束仍然成立：不能在 rendering scope 内采样当前
                // attachment（无 local-read 支持），但现在没有任何地方会检查它。
                // 纯 bindless 图像的 layout 转换整体是缺失的，属已知待办。

                std::array<VkRenderingAttachmentInfo, 4> color_attachments {};
                uint32_t color_attachment_count = 0;

                VkRenderingAttachmentInfo depth_attachment {};
                depth_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;

                const std::array<ImageRef, 4> color_refs {
                    rendering.color, rendering.extra_colors[0], rendering.extra_colors[1], rendering.extra_colors[2]
                };
                for (const ImageRef& color_ref : color_refs)
                {
                    if (!color_ref.handle)
                        break;

                    ImageStore::Write& color = color_writes[color_attachment_count];
                    color = write_image(color_ref.handle);
                    if (!color || color->image_handle == VK_NULL_HANDLE || color->image_view == VK_NULL_HANDLE)
                        throw std::logic_error("BeginRendering requires a valid color HardwareImage.");

                    const bool clear_attachment = rendering.clear_color || color->image_layout == VK_IMAGE_LAYOUT_UNDEFINED;
                    // LOAD_OP_LOAD 会在 COLOR_ATTACHMENT_OUTPUT 阶段读取既有内容，所以布局转换
                    // 本身（它是一次写）必须让后续的读可见；只给 WRITE 会被同步校验判为
                    // READ_AFTER_WRITE（例如 assao：present 后的 PRESENT_SRC → COLOR_ATTACHMENT_OPTIMAL
                    // 紧接一次 LOAD）。CLEAR 不读旧内容，无需 READ。
                    VkAccessFlags2 color_dst_access = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                    if (!clear_attachment)
                        color_dst_access |= VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;

                    transition_image(command_buffer,
                                     *color,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     color_dst_access);

                    VkRenderingAttachmentInfo& color_attachment = color_attachments[color_attachment_count];
                    color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                    color_attachment.imageView = color->image_view;
                    color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    color_attachment.loadOp = clear_attachment ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    color_attachment.clearValue = color->clear_value;

                    if (color_attachment_count == 0)
                        active_rendering.color_format = color->image_format;
                    else
                        active_rendering.extra_color_formats[color_attachment_count - 1] = color->image_format;
                    active_rendering.color_handles[color_attachment_count] = color_ref.handle;
                    ++color_attachment_count;
                }
                active_rendering.color_handle_count = color_attachment_count;

                if (rendering.depth.handle)
                {
                    depth = write_image(rendering.depth.handle);
                    if (!depth || depth->image_handle == VK_NULL_HANDLE || depth->image_view == VK_NULL_HANDLE)
                        throw std::logic_error("BeginRendering requires a valid depth HardwareImage.");

                    const bool clear_attachment = rendering.clear_depth || depth->image_layout == VK_IMAGE_LAYOUT_UNDEFINED;
                    // 同 color：LOAD_OP_LOAD 的深度读发生在 EARLY_FRAGMENT_TESTS，转换需一并放开 READ。
                    VkAccessFlags2 depth_dst_access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                    if (!clear_attachment)
                        depth_dst_access |= VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

                    transition_image(command_buffer,
                                     *depth,
                                     VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                     VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                     depth_dst_access);

                    depth_attachment.imageView = depth->image_view;
                    depth_attachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
                    depth_attachment.loadOp = clear_attachment ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
                    depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    depth_attachment.clearValue = depth->clear_value;
                    active_rendering.depth_format = depth->image_format;
                }

                VkRenderingInfo rendering_info {};
                rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
                rendering_info.renderArea = full_scissor(rendering.width, rendering.height);
                rendering_info.layerCount = 1;
                rendering_info.colorAttachmentCount = color_attachment_count;
                rendering_info.pColorAttachments = color_attachment_count != 0 ? color_attachments.data() : nullptr;
                rendering_info.pDepthAttachment = rendering.depth.handle ? &depth_attachment : nullptr;

                vkCmdBeginRendering(command_buffer, &rendering_info);

                VkViewport viewport {};
                viewport.x = 0.0f;
                viewport.y = 0.0f;
                viewport.width = static_cast<float>(rendering.width);
                viewport.height = static_cast<float>(rendering.height);
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(command_buffer, 0, 1, &viewport);

                VkRect2D scissor = full_scissor(rendering.width, rendering.height);
                vkCmdSetScissor(command_buffer, 0, 1, &scissor);

                active_rendering.active = true;
                active_rendering.width = rendering.width;
                active_rendering.height = rendering.height;
                // 新的 rendering scope 意味着 attachment 格式可能变了，而格式是
                // prepare_draw 的 key 之一；memo 整体作废。
                draw_cache.reset();
                break;
            }
            case CommandOp::EndRendering:
                if (active_rendering.active)
                {
                    vkCmdEndRendering(command_buffer);

                    for (uint32_t i = 0; i < active_rendering.color_handle_count; ++i)
                    {
                        const ResourceHandle handle = active_rendering.color_handles[i];
                        if (!handle)
                            continue;

                        ImageStore::Write color = write_image(handle);
                        if (!color || color->image_handle == VK_NULL_HANDLE)
                            continue;

                        const bool hi_z_style =
                            color->desc.format == Format::R32_FLOAT &&
                            has_flag(color->desc.usage, ImageUsageFlags::Storage) &&
                            has_flag(color->desc.usage, ImageUsageFlags::Sampled);
                        if (!hi_z_style)
                            continue;

                        transition_image(command_buffer,
                                         *color,
                                         VK_IMAGE_LAYOUT_GENERAL,
                                         VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                         VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT);
                    }
                    active_rendering = {};
                }
                break;
            case CommandOp::DrawIndexed:
            case CommandOp::DrawIndexedBatch:
                if (!active_rendering.active)
                    throw std::logic_error("DrawIndexed requires an active rendering scope.");
                (void)visit_indexed_draws(command, encode_indexed_draw);
                break;
            case CommandOp::DrawIndexedIndirect:
            {
                if (!active_rendering.active)
                    throw std::logic_error("DrawIndexedIndirect requires an active rendering scope.");
                if (command.resources.size() < 3u)
                    throw std::logic_error("DrawIndexedIndirect command is missing index, vertex, or indirect buffers.");
                encode_indexed_indirect_draw(BufferRef{ command.resources[0].handle },
                                             BufferRef{ command.resources[1].handle },
                                             BufferRef{ command.resources[2].handle },
                                             command.payload.draw_indexed_indirect);
                break;
            }
            case CommandOp::Present:
            {
                const PresentDesc& present = command.payload.present;
                if (!present.swapchain_image.handle)
                    break;

                ImageStore::Write src = write_image(present.image.handle);
                ImageStore::Write dst = write_image(present.swapchain_image.handle);
                if (!src || src->image_handle == VK_NULL_HANDLE)
                    throw std::logic_error("Present requires a valid source HardwareImage.");
                if (!dst || dst->image_handle == VK_NULL_HANDLE)
                    throw std::logic_error("Present requires a valid swapchain HardwareImage.");

                const VkExtent3D src_extent = mip_extent(*src, 0);
                const VkExtent3D dst_extent = mip_extent(*dst, 0);
                const VkExtent3D extent = min_extent(src_extent, dst_extent);
                if (extent.width == 0 || extent.height == 0 || extent.depth == 0)
                    break;

                transition_image(command_buffer,
                                 *src,
                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                 image_transfer_stages(),
                                 VK_ACCESS_2_TRANSFER_READ_BIT);
                transition_image(command_buffer,
                                 *dst,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 image_transfer_stages(),
                                 VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

                if (src->image_format == dst->image_format &&
                    src_extent.width == dst_extent.width &&
                    src_extent.height == dst_extent.height &&
                    src_extent.depth == dst_extent.depth)
                {
                    VkImageCopy copy {};
                    copy.srcSubresource = vk_subresource_layers(*src, 0, 0);
                    copy.srcOffset = { 0, 0, 0 };
                    copy.dstSubresource = vk_subresource_layers(*dst, 0, 0);
                    copy.dstOffset = { 0, 0, 0 };
                    copy.extent = extent;
                    vkCmdCopyImage(command_buffer,
                                   src->image_handle,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   dst->image_handle,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1,
                                   &copy);
                }
                else
                {
                    VkImageBlit blit {};
                    blit.srcSubresource = vk_subresource_layers(*src, 0, 0);
                    blit.srcOffsets[0] = { 0, 0, 0 };
                    blit.srcOffsets[1] = {
                        static_cast<int32_t>(src_extent.width),
                        static_cast<int32_t>(src_extent.height),
                        static_cast<int32_t>(src_extent.depth),
                    };
                    blit.dstSubresource = vk_subresource_layers(*dst, 0, 0);
                    blit.dstOffsets[0] = { 0, 0, 0 };
                    blit.dstOffsets[1] = {
                        static_cast<int32_t>(dst_extent.width),
                        static_cast<int32_t>(dst_extent.height),
                        static_cast<int32_t>(dst_extent.depth),
                    };
                    vkCmdBlitImage(command_buffer,
                                   src->image_handle,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   dst->image_handle,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1,
                                   &blit,
                                   VK_FILTER_NEAREST);
                }

                transition_image(command_buffer,
                                 *dst,
                                 VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                 VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                 0);
                break;
            }
            default:
                break;
            }
        }

        result = vkEndCommandBuffer(submission.command_buffer->vk());
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("vkEndCommandBuffer failed while encoding execution plan.");
        }
    }

    HardwareStream::HardwareStream(HardwareExecutor& executor)
        : executor_(&executor),
          recorder_(std::make_unique<CommandRecorder>())
    {
    }

    // CommandRecorder 仅在 command_ir.h 完整可见，故析构须在此处out-of-line定义，
    // 否则例子里 unique_ptr<CommandRecorder> 会在不完整类型上实例化 default_delete。
    HardwareStream::~HardwareStream() = default;

    HardwareStream& HardwareStream::operator<<(const StreamCommand& command)
    {
        ensure_open();
        command.record(*recorder_);
        return *this;
    }

    HardwareStream& HardwareStream::operator<<(const CommandBatch& commands)
    {
        ensure_open();
        for (const StreamCommand& command : commands.commands())
        {
            command.record(*recorder_);
        }
        return *this;
    }

    HardwareStream& HardwareStream::operator<<(ComputePipelineBase& pipeline)
    {
        return *this << pipeline.command_batch();
    }

    HardwareStream& HardwareStream::operator<<(RasterizerPipelineBase& pipeline)
    {
        ensure_open();
        pipeline.record_into(*recorder_);
        return *this;
    }

    SubmitReceipt HardwareStream::operator<<(CommitCommand)
    {
        ensure_open();
        committed_ = true;
        return executor_->commit(recorder_->close());
    }

    void HardwareStream::ensure_open() const
    {
        if (committed_)
        {
            throw std::logic_error("HardwareStream has already been committed.");
        }
    }

    HardwareExecutor::HardwareExecutor()
        : compiler_(std::make_shared<ExecutionCompiler>())
    {
    }

    HardwareExecutor::HardwareExecutor(QueueResolver queue_resolver)
        : compiler_(std::make_shared<ExecutionCompiler>()),
          queue_resolver_(std::move(queue_resolver))
    {
    }

    HardwareStream HardwareExecutor::stream()
    {
        return HardwareStream(*this);
    }

    HardwareStream HardwareExecutor::operator<<(RasterizerPipelineBase& pipeline)
    {
        HardwareStream s(*this);
        s << pipeline;
        return s;
    }

    std::vector<SubmissionToken> HardwareExecutor::submit(ExecutionPlan& plan,
                                                          std::vector<PresentResult>* present_results,
                                                          std::span<const SubmissionToken> wait_tokens,
                                                          ExecutionCommitProfileSample* profile) const
    {
        const auto resolve_queue = [this](DeviceId device, QueueCapability capability) -> Queue& {
            if (queue_resolver_)
            {
                return queue_resolver_(device, capability);
            }

            if (device.value != 0)
            {
                throw std::logic_error("Default HardwareExecutor currently resolves only the main device.");
            }

            Queue* queue = main_device_context().device_manager.queue_for(capability);
            if (queue == nullptr)
            {
                throw std::runtime_error("Default HardwareExecutor could not resolve a queue for the requested capability.");
            }
            return *queue;
        };

        struct PreparedQueuePresent
        {
            std::shared_ptr<DisplayManager> manager;
            PresentDesc desc {};
        };

        const auto skipped_present = [](const PresentDesc& desc, std::string message) {
            PresentResult result;
            result.status = PresentStatus::Skipped;
            result.displayer = desc.displayer;
            result.image = desc.image;
            result.message = std::move(message);
            return result;
        };

        std::vector<SubmissionToken> tokens;
        tokens.reserve(plan.submissions.size());
        std::vector<std::optional<SubmissionToken>> submitted_tokens(plan.submissions.size());

        for (size_t submission_index = 0; submission_index < plan.submissions.size(); ++submission_index)
        {
            CompiledSubmission& compiled_submission = plan.submissions[submission_index];

            std::vector<PreparedQueuePresent> prepared_presents;
            // prepare_present 没给出可提交帧的 manager（最小化 / acquire 超时 / 无
            // present queue）。命令依然提交给了 GPU，帧环必须照样推进并设卡，
            // 否则下一帧 CPU 会覆写仍在飞的 UBO 槽。
            std::vector<std::shared_ptr<DisplayManager>> skipped_managers;
            std::vector<SubmitWait> present_waits;
            std::vector<SubmitSignal> present_signals;
            Queue* present_queue = nullptr;
            size_t present_index = 0;
            for (CommandIR& command : compiled_submission.commands)
            {
                if (command.op != CommandOp::Present)
                {
                    continue;
                }

                PresentDesc& desc = command.payload.present;
                desc.swapchain_image = {};
                std::shared_ptr<DisplayManager> manager = display_manager_for(desc.displayer);
                if (!manager)
                {
                    if (present_index < compiled_submission.presents.size())
                    {
                        compiled_submission.presents[present_index] = desc;
                    }
                    if (present_results != nullptr)
                    {
                        present_results->push_back(skipped_present(desc, "Present node has no registered DisplayManager."));
                    }
                    ++present_index;
                    continue;
                }

                PreparedPresent prepared = manager->prepare_present(desc);
                command.payload.present = desc;
                if (present_index < compiled_submission.presents.size())
                {
                    compiled_submission.presents[present_index] = desc;
                }

                if (!prepared.ready_for_submit)
                {
                    if (present_results != nullptr)
                    {
                        present_results->push_back(std::move(prepared.immediate_result));
                    }
                    skipped_managers.push_back(std::move(manager));
                    ++present_index;
                    continue;
                }

                Queue* manager_present_queue = prepared.present_queue;
                if (manager_present_queue == nullptr)
                {
                    manager->cancel_prepared_present();
                    desc.swapchain_image = {};
                    command.payload.present = desc;
                    if (present_index < compiled_submission.presents.size())
                    {
                        compiled_submission.presents[present_index] = desc;
                    }
                    if (present_results != nullptr)
                    {
                        present_results->push_back(skipped_present(desc, "DisplayManager has no present queue for this present."));
                    }
                    skipped_managers.push_back(std::move(manager));
                    ++present_index;
                    continue;
                }
                if (present_queue != nullptr && present_queue != manager_present_queue)
                {
                    manager->cancel_prepared_present();
                    throw std::logic_error("A single Vulkan present submission cannot target multiple native present queues.");
                }
                present_queue = manager_present_queue;

                for (const SubmitWait& wait : prepared.waits)
                {
                    add_wait_once(present_waits, wait);
                }
                add_signal_once(present_signals, prepared.signal);
                compiled_submission.keep_alive.add_resource(ResourceBridge::keep_alive(desc.swapchain_image.handle));
                command.payload.present = desc;
                if (present_index < compiled_submission.presents.size())
                {
                    compiled_submission.presents[present_index] = desc;
                }
                prepared_presents.push_back({ std::move(manager), desc });
                ++present_index;
            }

            Queue& queue = present_queue != nullptr
                ? *present_queue
                : resolve_queue(compiled_submission.device, compiled_submission.queue);

            if (!compiled_submission.command_buffer)
            {
                HORIZON_PROFILE_SCOPE_N("submit::acquire_cmdbuf");
                compiled_submission.command_buffer = queue.acquire(profile);
            }

            try
            {
                std::vector<SubmitWait> waits = compiled_submission.waits;
                for (const SubmitWait& wait : present_waits)
                {
                    add_wait_once(waits, wait);
                }
                for (const SubmissionToken& token : wait_tokens)
                {
                    const VkSemaphore timeline = timeline_semaphore(token);
                    if (timeline != VK_NULL_HANDLE && token.value != 0)
                    {
                        add_wait_once(waits, { timeline, token.value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT });
                    }
                }

                for (const SubmissionDependency& dependency : plan.dependencies)
                {
                    if (dependency.consumer != submission_index)
                    {
                        continue;
                    }

                    const std::optional<SubmissionToken>& token = submitted_tokens[dependency.producer];
                    const VkSemaphore timeline = token ? timeline_semaphore(*token) : VK_NULL_HANDLE;
                    if (timeline != VK_NULL_HANDLE && token->value != 0)
                    {
                        add_wait_once(waits, { timeline, token->value, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT });
                    }
                }

                std::vector<SubmitSignal> signals = compiled_submission.signals;
                for (const SubmitSignal& signal : present_signals)
                {
                    add_signal_once(signals, signal);
                }

                HORIZON_PROFILE_SCOPE_N("submit::tracker_lock");
                const auto tracker_start = std::chrono::steady_clock::now();
                auto tracked_submission = resource_submission_tracker().lock_submission(compiled_submission, queue.id(), waits);
                if (profile != nullptr)
                {
                    profile->tracker_wait_ms += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - tracker_start).count();
                }

                VulkanCommandEncoder encoder(queue.device());
                const auto encode_start = std::chrono::steady_clock::now();
                encoder.encode(compiled_submission);
                if (profile != nullptr)
                {
                    profile->encode_ms += std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - encode_start).count();
                }

                QueueSubmission queue_submission;
                queue_submission.command_buffer = std::move(compiled_submission.command_buffer);
                queue_submission.keep_alive = std::move(compiled_submission.keep_alive);
                queue_submission.debug_summary = describe_submission_for_diagnostics(compiled_submission);
                tokens.push_back(queue.submit(queue_submission, waits, signals, profile));
                submitted_tokens[submission_index] = tokens.back();
                tracked_submission.remember(tokens.back());
            }
            catch (std::exception e)
            {
                std::cerr << e.what() << std::endl;
                for (PreparedQueuePresent& present : prepared_presents)
                {
                    if (present.manager)
                    {
                        present.manager->cancel_prepared_present();
                    }
                }
                throw;
            }

            for (size_t index = 0; index < prepared_presents.size(); ++index)
            {
                PresentResult result;
                try
                {
                    const auto present_start = std::chrono::steady_clock::now();
                    result = prepared_presents[index].manager->present(prepared_presents[index].desc, tokens.back());
                    if (profile != nullptr)
                    {
                        profile->present_ms += std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - present_start).count();
                    }
                }
                catch (...)
                {
                    for (size_t remaining = index + 1; remaining < prepared_presents.size(); ++remaining)
                    {
                        if (prepared_presents[remaining].manager)
                        {
                            prepared_presents[remaining].manager->cancel_prepared_present();
                        }
                    }
                    throw;
                }

                if (present_results != nullptr)
                {
                    present_results->push_back(std::move(result));
                }
            }

            // 没能 present 的 manager：帧环照样推进并等它的第 N 帧前的卡。
            if (!tokens.empty())
            {
                for (std::shared_ptr<DisplayManager>& manager : skipped_managers)
                {
                    if (manager)
                    {
                        manager->note_skipped_frame(tokens.back());
                    }
                }
            }
        }

        return tokens;
    }

    SubmitReceipt HardwareExecutor::commit(RecordedTask task)
    {
        HORIZON_PROFILE_SCOPE_N("Horizon::commit");
        const auto total_start = std::chrono::steady_clock::now();
        ExecutionCommitProfileSample profile;
        ExecutionCommitProfileScope profile_scope(profile);
        const auto compile_start = std::chrono::steady_clock::now();
        ExecutionPlan plan = compiler_->compile(std::move(task), &profile);
        profile.compile_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - compile_start).count();
        std::vector<std::shared_ptr<SubmissionToken>> wait_tokens = consume_pending_waits();

        auto receipt_data = std::make_shared<SubmitReceiptData>();
        SubmitReceipt receipt;
        {
            std::lock_guard lock(mutex_);
            receipt.serial = ++next_submit_serial_;
        }

        // submit 需要的是扁平 span<const SubmissionToken>，把待等 token 解引用成一份
        // 临时拷贝（数量很小，不构成提交路径热点）。
        std::vector<SubmissionToken> flat_wait_tokens;
        flat_wait_tokens.reserve(wait_tokens.size());
        for (const std::shared_ptr<SubmissionToken>& token : wait_tokens)
            flat_wait_tokens.push_back(*token);

        receipt_data->tokens = submit(plan, &receipt_data->presents, flat_wait_tokens, &profile);
        receipt.data = receipt_data;
        profile.total_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - total_start).count();
        record_execution_commit_profile(profile);
        HORIZON_PROFILE_PLOT("commit total (ms)", profile.total_ms);
        HORIZON_PROFILE_PLOT("commit compile (ms)", profile.compile_ms);
        HORIZON_PROFILE_PLOT("commit encode (ms)", profile.encode_ms);
        HORIZON_PROFILE_PLOT("commit vk submit (ms)", profile.queue_submit_ms);
        HORIZON_PROFILE_PLOT("commit present (ms)", profile.present_ms);
        HORIZON_PROFILE_PLOT("commit commands", profile.commands);
        HORIZON_PROFILE_PLOT("commit tracker wait (ms)", profile.tracker_wait_ms);
        HORIZON_PROFILE_PLOT("commit queue retire (ms)", profile.queue_retire_ms);
        HORIZON_PROFILE_PLOT("commit queue lock wait (ms)", profile.queue_lock_wait_ms);
        return receipt;
    }

    HardwareExecutor& HardwareExecutor::wait(const SubmitReceipt& receipt)
    {
        const auto* data = static_cast<const SubmitReceiptData*>(receipt.data.get());
        std::lock_guard lock(mutex_);
        for (const SubmissionToken& token : data->tokens)
            pending_waits_.push_back(std::make_shared<SubmissionToken>(token));
        return *this;
    }

    HardwareExecutor& HardwareExecutor::wait_idle(const SubmitReceipt& receipt)
    {
        const auto* data = static_cast<const SubmitReceiptData*>(receipt.data.get());
        for (const SubmissionToken& token : data->tokens)
        {
            if (token.value == 0)
            {
                continue;
            }

            Queue* queue = nullptr;
            if (queue_resolver_)
            {
                queue = &queue_resolver_(token.device, token.queue.capability);
            }
            else
            {
                if (token.device.value != 0)
                {
                    throw std::logic_error("Default HardwareExecutor currently waits only on the main device.");
                }
                queue = main_device_context().device_manager.queue_for(token.queue.capability);
            }

            if (queue == nullptr)
            {
                throw std::runtime_error("HardwareExecutor could not resolve a queue for receipt wait.");
            }

            queue->wait_idle();
        }
        return *this;
    }

    std::vector<std::shared_ptr<SubmissionToken>> HardwareExecutor::consume_pending_waits()
    {
        std::lock_guard lock(mutex_);
        return std::move(pending_waits_);
    }
}
