#include "vk_stream.h"
#include "vk_compute_device.h"
#include "vk_command_visitor.h"
#include "vk_shader.h"
#include "vulkan_interop.h"
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <thread>

namespace ocarina {
namespace {
thread_local const VulkanComputeDevice *completion_device = nullptr;
std::mutex &profile_output_mutex() {
    static std::mutex mutex;
    return mutex;
}
uint64_t &profile_batch_serial() {
    static uint64_t serial{};
    return serial;
}
void memory_barrier(VkCommandBuffer cb, bool host_read = false) {
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkPipelineStageFlags destination = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    if (host_read) {
        destination |= VK_PIPELINE_STAGE_HOST_BIT;
        barrier.dstAccessMask |= VK_ACCESS_HOST_READ_BIT;
    }
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, destination,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
}
}

struct VkComputeStream::State {
    // Temporary opt-in profiling. No query resources are created when
    // REBLUR_GPU_PROFILE is unset. MODE=filter measures complete filter chains;
    // MODE=nrd measures the EDSL core between conversion passes. Other modes
    // time each dispatch. Full pools spill before a new range.
    static constexpr uint32_t profile_dispatch_limit = 1024u;
    static constexpr uint32_t profile_query_count = profile_dispatch_limit * 2u;
    struct ProfileOutput {
        std::ofstream file;
        double timestamp_period{};
        uint64_t timestamp_mask{};

        ProfileOutput(VulkanComputeDevice *device, const char *path) {
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(device->physical_device(), &properties);
            uint32_t count{};
            vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device(), &count, nullptr);
            vector<VkQueueFamilyProperties> queues(count);
            vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device(), &count, queues.data());
            const auto bits = queues[device->queue_family()].timestampValidBits;
            OC_ERROR_IF(!bits || properties.limits.timestampPeriod <= 0.f,
                "REBLUR_GPU_PROFILE requires Vulkan compute queue timestamp support");
            timestamp_period = properties.limits.timestampPeriod;
            timestamp_mask = bits == 64u ? UINT64_MAX : (uint64_t(1u) << bits) - 1u;
            // Streams may share one output. Append and flush under the same
            // process mutex so headers and CSV rows cannot interleave.
            std::lock_guard lock(profile_output_mutex());
            file.open(std::filesystem::path(path), std::ios::app);
            OC_ERROR_IF(!file, "Cannot open REBLUR_GPU_PROFILE output");
            if (file.tellp() == std::streampos(0)) file << "batch,name,width,height,ms\n";
            file << std::setprecision(12);
            file.flush();
        }
    };
    struct ProfileQueries {
        struct Dispatch {
            string name;
            uint32_t width{}, height{};
        };
        VkQueryPool pool{};
        vector<Dispatch> dispatches;
        bool filter_open{};
        std::array<uint64_t, profile_query_count> timestamps;
    };
    struct Arena {
        VkBufferAllocation allocation;
        void *mapped{};
        size_t used{};
    };
    struct Batch {
        VkCommandPool pool{};
        VkCommandBuffer cb{};
        VkFence fence{};
        uint64_t serial{};
        bool gpu_submitted{};
        const VkShaderBase *bound_shader{};
        vector<VkBufferAllocation> retained;
        vector<std::function<void()>> completions;
        // Submission-time callbacks are kept alive through this batch's fence,
        // without turning them into host completion dependencies.
        vector<NativeCallbackFunction> native_submissions;
        vector<Arena> arenas;
        std::unique_ptr<ProfileQueries> profile;
    };
    VulkanComputeDevice *device;
    std::mutex mutex;
    std::recursive_mutex operations;
    std::condition_variable cv;
    std::condition_variable work_ready;
    std::deque<Batch *> pending;
    vector<Batch *> available;
    vector<std::unique_ptr<Batch>> batches;
    Batch *current{};
    uint64_t next_serial{}, submitted{}, completed{}, last_host_serial{};
    bool retirement_active{}, worker_requested{};
    bool deferred_submission{};
    bool stopping{};
    std::unique_ptr<ProfileOutput> profile;
    bool profile_filter_mode{};
    bool profile_nrd_mode{};
    std::thread worker;

    explicit State(VulkanComputeDevice *d) : device(d) {
        const char *path = std::getenv("REBLUR_GPU_PROFILE");
        if (path && *path) {
            profile = std::make_unique<ProfileOutput>(device, path);
            const char *mode = std::getenv("REBLUR_GPU_PROFILE_MODE");
            profile_filter_mode = mode && string_view(mode) == "filter";
            profile_nrd_mode = mode && string_view(mode) == "nrd";
        }
        worker = std::thread([this] { retire_batches(); });
    }
    ~State() {
        synchronize();
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        cv.notify_all();
        work_ready.notify_one();
        worker.join();
        for (auto &batch : batches) {
            if (batch->profile)
                vkDestroyQueryPool(device->logical_device(), batch->profile->pool, nullptr);
            for (auto &arena : batch->arenas) {
                vmaUnmapMemory(device->vma_allocator(), arena.allocation.alloc);
                device->free_buffer(arena.allocation);
            }
            vkDestroyFence(device->logical_device(), batch->fence, nullptr);
            vkDestroyCommandPool(device->logical_device(), batch->pool, nullptr);
        }
    }
    void resolve_profile(Batch &batch) {
        if (!batch.profile || batch.profile->dispatches.empty()) return;
        auto &queries = *batch.profile;
        const auto count = uint32_t(queries.dispatches.size()) * 2u;
        // Called only after this batch's existing fence has completed. Reading
        // the results needs no WAIT_BIT and does not add a GPU round trip.
        OC_VK_CHECK(vkGetQueryPoolResults(device->logical_device(), queries.pool, 0, count,
            size_t(count) * sizeof(uint64_t), queries.timestamps.data(), sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT));
        std::lock_guard lock(profile_output_mutex());
        const auto serial = ++profile_batch_serial();
        for (size_t i = 0; i < queries.dispatches.size(); ++i) {
            const auto &dispatch = queries.dispatches[i];
            const auto ticks = (queries.timestamps[i * 2u + 1u] - queries.timestamps[i * 2u]) &
                               profile->timestamp_mask;
            profile->file << serial << ",\"";
            for (char c : dispatch.name) {
                if (c == '"') profile->file << '"';
                profile->file << c;
            }
            profile->file << "\"," << dispatch.width << ',' << dispatch.height << ',' <<
                double(ticks) * profile->timestamp_period / 1000000.0 << '\n';
        }
        profile->file.flush();
        OC_ERROR_IF(!profile->file, "Cannot write REBLUR_GPU_PROFILE output");
        // Metadata must be consumed before this batch's arenas and command
        // pool become available to the recording thread again.
        queries.dispatches.clear();
    }
    Batch &begin() {
        if (current) return *current;
        {
            std::unique_lock lock(mutex);
            // Bound allocation growth when the CPU outruns the GPU.
            if (available.empty() && batches.size() >= 64) {
                worker_requested = true;
                work_ready.notify_one();
                cv.wait(lock, [&] { return !available.empty(); });
            }
            if (!available.empty()) {
                current = available.back();
                available.pop_back();
            }
        }
        if (!current) {
            auto batch = std::make_unique<Batch>();
            VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
            pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
            pool.queueFamilyIndex = device->queue_family();
            OC_VK_CHECK(vkCreateCommandPool(device->logical_device(), &pool, nullptr, &batch->pool));
            VkCommandBufferAllocateInfo allocate{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            allocate.commandPool = batch->pool;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1;
            OC_VK_CHECK(vkAllocateCommandBuffers(device->logical_device(), &allocate, &batch->cb));
            VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            OC_VK_CHECK(vkCreateFence(device->logical_device(), &fence, nullptr, &batch->fence));
            current = batch.get();
            batches.push_back(std::move(batch));
        } else {
            OC_VK_CHECK(vkResetCommandPool(device->logical_device(), current->pool, 0));
            OC_VK_CHECK(vkResetFences(device->logical_device(), 1, &current->fence));
        }
        for (auto &arena : current->arenas) arena.used = 0;
        current->bound_shader = nullptr;
        current->gpu_submitted = false;
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        OC_VK_CHECK(vkBeginCommandBuffer(current->cb, &bi));
        memory_barrier(current->cb);
        return *current;
    }
    void submit(Batch &batch) {
        auto submit_engine_batch = [&] {
            device->submit(batch.cb, batch.fence, VK_NULL_HANDLE, 0);
        };
        if (batch.native_submissions.empty()) {
            submit_engine_batch();
        } else {
            VulkanInteropContext vk;
            vk.instance = device->instance();
            vk.physical_device = device->physical_device();
            vk.device = device->logical_device();
            vk.compute_queue = device->compute_queue();
            vk.queue_family = device->queue_family();
            vk.get_instance_proc_addr = vkGetInstanceProcAddr;
            vk.backend = device;
            vk.resolve_buffer = [](void *backend, uint64_t address, VkDeviceSize *offset) {
                return static_cast<VulkanComputeDevice *>(backend)->get_vk_buffer(address, offset);
            };
            const NativeInteropContext context{NativeInteropBackend::VulkanCompute, &vk};
            // Keep external queue submission and its following engine fence
            // in one queue critical section. submit() uses a recursive mutex.
            device->run_commit_callback([&](void *) {
                for (const auto &fn : batch.native_submissions) fn(context);
                submit_engine_batch();
            });
        }
        batch.gpu_submitted = true;
    }
    void flush() {
        if (!current) return;
        // An aggregate range owns one pair in this batch's query pool. Do not
        // submit an unfinished pair or let retirement read an unwritten end.
        OC_ERROR_IF(current->profile && current->profile->filter_open,
            "REBLUR aggregate GPU profiling requires a complete filter/core range in one batch");
        Batch *batch = current;
        current = nullptr;
        // Host visibility is needed for readbacks/callbacks, not GPU-only work.
        // Subsequent GPU commands carry their own execution/memory dependency.
        if (!batch->completions.empty()) memory_barrier(batch->cb, true);
        OC_VK_CHECK(vkEndCommandBuffer(batch->cb));
        batch->serial = ++next_serial;
        bool background_work;
        {
            std::lock_guard lock(mutex);
            // Preserve nonblocking commit without a GPU wait on a host-signaled
            // timeline. That wait can stall vkSignalSemaphore on Windows/NVIDIA
            // when the engine uses a second Vulkan device. The retirement worker
            // submits dependent batches after the preceding callbacks finish.
            deferred_submission |= completed < last_host_serial;
            if (!batch->completions.empty()) last_host_serial = batch->serial;
            if (!deferred_submission) {
                submit(*batch);
            }
            pending.push_back(batch);
            submitted = batch->serial;
            // Pure GPU commits stay asynchronous without waking a CPU worker.
            // A synchronizing caller can retire them directly. Host callbacks
            // and allocation pressure request autonomous background progress.
            worker_requested |= deferred_submission || !batch->completions.empty();
            background_work = worker_requested;
        }
        if (background_work) work_ready.notify_one();
    }
    void synchronize() {
        flush();
        wait_idle();
    }
    void wait_idle() {
        std::unique_lock lock(mutex);
        const auto target = submitted;
        while (completed < target) {
            if (!retirement_active && !pending.empty() && pending.front()->completions.empty()) {
                auto *batch = pending.front();
                pending.pop_front();
                retirement_active = true;
                lock.unlock();
                retire(batch);
                lock.lock();
            } else {
                cv.wait(lock);
            }
        }
    }
    void retire(Batch *batch) {
        // Retirement may run on the recording thread for GPU-only batches.
        // Native capture destructors can release Ocarina resources; prevent
        // those destructors from recursively waiting on this in-progress batch.
        struct CompletionScope {
            const VulkanComputeDevice *previous;
            explicit CompletionScope(const VulkanComputeDevice *d)
                : previous(completion_device) { completion_device = d; }
            ~CompletionScope() { completion_device = previous; }
        } completion_scope(device);
        if (!batch->gpu_submitted) {
            submit(*batch);
            if (batch->completions.empty()) {
                // Feed the queue through the next host callback before waiting
                // for fences. Serial submit/wait for every deferred GPU-only
                // batch causes a CPU/GPU round trip for each kernel commit.
                std::lock_guard lock(mutex);
                for (auto* next : pending) {
                    if (!next->gpu_submitted) {
                        submit(*next);
                    }
                    if (!next->completions.empty()) break;
                }
                deferred_submission = !pending.empty() && !pending.back()->gpu_submitted;
            }
        }
        OC_VK_CHECK(vkWaitForFences(device->logical_device(), 1, &batch->fence, VK_TRUE, UINT64_MAX));
        if (profile) resolve_profile(*batch);
        for (auto &fn : batch->completions) fn();
        batch->completions.clear();
        batch->native_submissions.clear();
        for (auto &allocation : batch->retained) device->free_buffer(allocation);
        batch->retained.clear();
        bool background_work;
        {
            std::lock_guard lock(mutex);
            completed = batch->serial;
            available.push_back(batch);
            retirement_active = false;
            if (pending.empty()) {
                worker_requested = false;
                deferred_submission = false;
            }
            background_work = worker_requested;
        }
        cv.notify_all();
        if (background_work) work_ready.notify_one();
    }
    void retire_batches() {
        completion_device = device;
        for (;;) {
            Batch *batch;
            {
                std::unique_lock lock(mutex);
                work_ready.wait(lock, [&] {
                    return stopping || (!retirement_active && worker_requested && !pending.empty());
                });
                if (pending.empty()) return;
                batch = pending.front();
                pending.pop_front();
                retirement_active = true;
            }
            retire(batch);
        }
    }
};

VkComputeStream::VkComputeStream(VulkanComputeDevice *device) noexcept
    : device_(device), state_(std::make_unique<State>(device)) {
    device_->register_stream(this);
}
VkComputeStream::~VkComputeStream() noexcept {
    synchronize();
    device_->unregister_stream(this);
    state_.reset();
}
void VkComputeStream::record(const std::function<void(VkCommandBuffer)> &fn) noexcept {
    std::lock_guard lock(state_->operations);
    fn(state_->begin().cb);
}
bool VkComputeStream::update_shader_binding(const VkShaderBase *shader) noexcept {
    std::lock_guard lock(state_->operations);
    auto &batch = state_->begin();
    const bool changed = batch.bound_shader != shader;
    batch.bound_shader = shader;
    return changed;
}
void VkComputeStream::dispatch_shader(VkShaderBase *shader, ShaderDispatchCommand *command) noexcept {
    std::lock_guard lock(state_->operations);
    const bool filter_mode = state_->profile && state_->profile_filter_mode;
    const bool nrd_mode = state_->profile && state_->profile_nrd_mode;
    const bool dispatch_mode = state_->profile && !filter_mode && !nrd_mode;
    const auto &name = shader->description();
    const bool starts_legacy_filter = filter_mode && name == "REBLUR-ClassifyAndPack";
    const bool ends_legacy_filter = filter_mode && name == "REBLUR-TemporalStabilization";
    const bool starts_edsl_filter = filter_mode && name == "NRD-EDSL-REBLUR-Pack";
    const bool ends_edsl_filter = filter_mode && name == "NRD-EDSL-REBLUR-Unpack";
    const bool starts_nrd = nrd_mode && name == "NRD-EDSL-REBLUR-Pack";
    const bool ends_nrd = nrd_mode && (name == "NRD-EDSL-REBLUR-TemporalStabilization" ||
                                      name == "NRD-EDSL-REBLUR-PostBlurNoTS");
    const bool starts_range = starts_legacy_filter || starts_edsl_filter || starts_nrd;
    const bool starts_profile = dispatch_mode || starts_range;
    // Spill before staging parameters: their arena address must belong to the
    // same batch as the dispatch that consumes it. Aggregate ranges reserve
    // their complete pair at the start and never spill between filter passes.
    if (starts_profile && state_->current && state_->current->profile &&
        state_->current->profile->dispatches.size() == State::profile_dispatch_limit)
        state_->flush();
    auto data = command->argument_data();
    const auto address = !shader->parameters_inline() ? stage_parameters(data.data(), data.size_bytes()) : 0;
    auto &batch = state_->begin();
    const bool bind_pipeline = update_shader_binding(shader);
    auto *queries = batch.profile.get();
    uint32_t query{};
    if (starts_profile) {
        if (!queries) {
            batch.profile = std::make_unique<State::ProfileQueries>();
            queries = batch.profile.get();
            VkQueryPoolCreateInfo info{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            info.queryType = VK_QUERY_TYPE_TIMESTAMP;
            info.queryCount = State::profile_query_count;
            OC_VK_CHECK(vkCreateQueryPool(device_->logical_device(), &info, nullptr, &queries->pool));
        }
        if (queries->dispatches.empty()) vkCmdResetQueryPool(batch.cb, queries->pool, 0, State::profile_query_count);
        OC_ERROR_IF(queries->filter_open, "REBLUR aggregate GPU profiling encountered a nested range");
        const auto dim = command->dispatch_dim();
        query = uint32_t(queries->dispatches.size()) * 2u;
        queries->dispatches.push_back({starts_nrd ? string("REBLUR-NRD") :
                                       starts_range ? string("REBLUR-Filter") : name, dim.x, dim.y});
        queries->filter_open = starts_range;
        // Preserve the existing dispatch/legacy filter timestamp stages. EDSL
        // aggregate ranges use BOTTOM_OF_PIPE at both ends, as NVRHI does.
        // Core timing starts after Pack's producer/consumer barrier below.
        if (!starts_nrd)
            vkCmdWriteTimestamp(batch.cb, starts_edsl_filter ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT :
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries->pool, query);
    }
    shader->dispatch(batch.cb, command, address, bind_pipeline);
    if (ends_legacy_filter || ends_nrd) {
        OC_ERROR_IF(!queries || !queries->filter_open,
            "REBLUR aggregate GPU profiling encountered a range end without a matching start");
        query = uint32_t(queries->dispatches.size() - 1u) * 2u;
        if (ends_legacy_filter) {
            const auto dim = command->dispatch_dim();
            queries->dispatches.back().width = dim.x;
            queries->dispatches.back().height = dim.y;
        }
        queries->filter_open = false;
        // End core timing before the final pass's trailing visibility barrier,
        // matching NativeReblur's nrd_timer before its Unpack barrier. All
        // intervening clears, dispatches and barriers remain inside the pair.
        vkCmdWriteTimestamp(batch.cb, ends_nrd ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT :
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries->pool, query + 1u);
    }
    if (dispatch_mode)
        vkCmdWriteTimestamp(batch.cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, queries->pool, query + 1u);
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                            VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(batch.cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);
    if (starts_nrd)
        vkCmdWriteTimestamp(batch.cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries->pool, query);
    if (ends_edsl_filter) {
        OC_ERROR_IF(!queries || !queries->filter_open,
            "REBLUR filter GPU profiling encountered Unpack without Pack");
        query = uint32_t(queries->dispatches.size() - 1u) * 2u;
        queries->filter_open = false;
        // Keep the true image dimensions captured at Pack, rather than the
        // padded dimensions of intermediate dispatches. Include Unpack's
        // final visibility barrier in complete-filter timing.
        vkCmdWriteTimestamp(batch.cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries->pool, query + 1u);
    }
}
void VkComputeStream::retain(VkBufferAllocation allocation) noexcept {
    std::lock_guard lock(state_->operations);
    state_->begin().retained.push_back(allocation);
}
void VkComputeStream::after_completion(std::function<void()> fn) noexcept {
    std::lock_guard lock(state_->operations);
    state_->begin().completions.push_back(std::move(fn));
}
void VkComputeStream::host_function(std::function<void()> fn, bool async) noexcept {
    std::lock_guard lock(state_->operations);
    after_completion(std::move(fn));
    state_->flush();
    if (!async) synchronize();
}
void VkComputeStream::native_callback(NativeCallbackFunction fn) noexcept {
    if (!fn) return;
    std::lock_guard lock(state_->operations);
    // End the preceding engine command buffer, but do not wait for its fence.
    // If it depends on pending host work, the existing retirement sequence
    // invokes this callback only after that work and its GPU submission.
    state_->flush();
    // begin() emits an ALL_COMMANDS memory dependency before any subsequent
    // engine work. Its submission happens after the external queue callback,
    // so this batch's fence also covers native work when it is the last command.
    state_->begin().native_submissions.push_back(std::move(fn));
}
void VkComputeStream::prepare_upload(std::function<void()> copy) noexcept {
    std::lock_guard lock(state_->operations);
    bool pending;
    {
        std::lock_guard state_lock(state_->mutex);
        pending = state_->completed < state_->last_host_serial;
    }
    pending |= state_->current && !state_->current->completions.empty();
    // A preceding callback/readback may produce the upload source. Capture
    // its bytes only after that host work, before releasing the next GPU batch.
    if (pending) host_function(std::move(copy), true);
    else copy();
}
VkDeviceAddress VkComputeStream::stage_parameters(const void *data, size_t size) noexcept {
    std::lock_guard lock(state_->operations);
    if (!size) return 0;
    auto &batch = state_->begin();
    const size_t bytes = (size + 15u) & ~size_t(15u);
    State::Arena *arena = nullptr;
    for (auto &candidate : batch.arenas) {
        if (candidate.allocation.size - candidate.used >= bytes) {
            arena = &candidate;
            break;
        }
    }
    if (!arena) {
        State::Arena allocation;
        allocation.allocation = device_->allocate_buffer(std::max(size_t(65536), bytes),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, "stream-parameters", 16);
        OC_VK_CHECK(vmaMapMemory(device_->vma_allocator(), allocation.allocation.alloc, &allocation.mapped));
        batch.arenas.push_back(allocation);
        arena = &batch.arenas.back();
    }
    memcpy(static_cast<std::byte *>(arena->mapped) + arena->used, data, size);
    auto address = arena->allocation.address + arena->used;
    arena->used += bytes;
    return address;
}
void VkComputeStream::synchronize() noexcept {
    std::lock_guard lock(state_->operations);
    state_->synchronize();
}
void VkComputeStream::wait_idle() noexcept { state_->wait_idle(); }
bool VkComputeStream::is_completion_thread(const VulkanComputeDevice *device) noexcept {
    return completion_device == device;
}
void VkComputeStream::add_command(Command *cmd) noexcept { command_queue_.push_back(cmd); }
void VkComputeStream::barrier() noexcept { record([](VkCommandBuffer cb) { memory_barrier(cb); }); }
void VkComputeStream::commit(const Commit &cmt) noexcept {
    std::lock_guard lock(state_->operations);
    VkComputeCommandVisitor visitor(device_, this);
    for (auto *cmd : command_queue_) cmd->accept(visitor);
    command_queue_.clear();
    state_->flush();
    if (cmt.callback) {
        // Native submissions must follow all deferred GPU work and host callbacks.
        state_->wait_idle();
        device_->run_commit_callback(cmt.callback);
        // Include native submissions in the next synchronize() boundary.
        state_->begin();
        state_->flush();
    }
}
}// namespace ocarina
