#include "vk_stream.h"
#include "vk_compute_device.h"
#include "vk_command_visitor.h"
#include <condition_variable>
#include <deque>
#include <thread>

namespace ocarina {
namespace {
thread_local const VulkanComputeDevice *completion_device = nullptr;
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
        const VkShaderBase *bound_shader{};
        vector<VkBufferAllocation> retained;
        vector<std::function<void()>> completions;
        vector<Arena> arenas;
    };
    VulkanComputeDevice *device;
    VkSemaphore timeline{};
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
    bool stopping{};
    std::thread worker;

    explicit State(VulkanComputeDevice *d) : device(d) {
        VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
        type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        ci.pNext = &type;
        OC_VK_CHECK(vkCreateSemaphore(device->logical_device(), &ci, nullptr, &timeline));
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
            for (auto &arena : batch->arenas) {
                vmaUnmapMemory(device->vma_allocator(), arena.allocation.alloc);
                device->free_buffer(arena.allocation);
            }
            vkDestroyFence(device->logical_device(), batch->fence, nullptr);
            vkDestroyCommandPool(device->logical_device(), batch->pool, nullptr);
        }
        vkDestroySemaphore(device->logical_device(), timeline, nullptr);
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
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        OC_VK_CHECK(vkBeginCommandBuffer(current->cb, &bi));
        memory_barrier(current->cb);
        return *current;
    }
    void flush() {
        if (!current) return;
        Batch *batch = current;
        current = nullptr;
        // Host visibility is needed for readbacks/callbacks, not GPU-only work.
        // Subsequent GPU commands carry their own execution/memory dependency.
        if (!batch->completions.empty()) memory_barrier(batch->cb, true);
        OC_VK_CHECK(vkEndCommandBuffer(batch->cb));
        batch->serial = ++next_serial;
        uint64_t host_dependency;
        {
            std::lock_guard lock(mutex);
            host_dependency = completed < last_host_serial ? last_host_serial : 0;
        }
        if (!batch->completions.empty()) last_host_serial = batch->serial;
        // GPU-only batches are ordered by queue barriers. Wait on the CPU
        // timeline only when preceding host work is part of stream ordering.
        device->submit(batch->cb, batch->fence, timeline, host_dependency);
        bool background_work;
        {
            std::lock_guard lock(mutex);
            pending.push_back(batch);
            submitted = batch->serial;
            // Pure GPU commits stay asynchronous without waking a CPU worker.
            // A synchronizing caller can retire them directly. Host callbacks
            // and allocation pressure request autonomous background progress.
            worker_requested |= !batch->completions.empty();
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
        OC_VK_CHECK(vkWaitForFences(device->logical_device(), 1, &batch->fence, VK_TRUE, UINT64_MAX));
        const bool has_host_work = !batch->completions.empty();
        for (auto &fn : batch->completions) fn();
        batch->completions.clear();
        for (auto &allocation : batch->retained) device->free_buffer(allocation);
        batch->retained.clear();
        // Only host work creates timeline dependencies. GPU-only batches are
        // ordered by queue barriers and do not need a host semaphore signal.
        if (has_host_work) {
            VkSemaphoreSignalInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};
            signal.semaphore = timeline;
            signal.value = batch->serial;
            OC_VK_CHECK(vkSignalSemaphore(device->logical_device(), &signal));
        }
        bool background_work;
        {
            std::lock_guard lock(mutex);
            completed = batch->serial;
            available.push_back(batch);
            retirement_active = false;
            if (pending.empty()) worker_requested = false;
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
        // A native queue submission must observe preceding host callbacks.
        // The marker waits for their timeline value without blocking commit.
        state_->begin();
        state_->flush();
        device_->run_commit_callback(cmt.callback);
        // Include native submissions in the next synchronize() boundary.
        state_->begin();
        state_->flush();
    }
}
}// namespace ocarina
