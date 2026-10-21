#include "tests/backend_test.h"
#include "dsl/dsl.h"
#include "rhi/command.h"
#include "backends/vulkan_compute/vulkan_interop.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace ocarina;

namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "native callback: " << message << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

struct NativeWork {
    // Releasing the last callback capture must safely release engine resources,
    // including when the recording thread retires a GPU-only batch.
    Buffer<uint> lifetime_buffer;
    std::atomic<bool> &destroyed;
    VkDevice device{};
    VkCommandPool pool{};
    PFN_vkDestroyCommandPool destroy_pool{};
    NativeWork(Device &d, std::atomic<bool> &flag)
        : lifetime_buffer(d.create_buffer<uint>(1u)), destroyed(flag) {}
    ~NativeWork() {
        if (pool) destroy_pool(device, pool, nullptr);
        destroyed = true;
    }
};

NativeCallbackFunction make_native_copy(std::shared_ptr<NativeWork> owner, uint64_t source,
                                       uint64_t destination, uint count,
                                       std::atomic<bool> &called, std::atomic<bool> *host_ready = nullptr) {
    return [owner = std::move(owner), source, destination, count, &called, host_ready]
           (const NativeInteropContext &context) {
        require(context.backend == NativeInteropBackend::VulkanCompute, "Vulkan context missing");
        require(!host_ready || host_ready->load(), "native callback ran before its preceding host callback");
        const auto &vk = *static_cast<const VulkanInteropContext *>(context.native);
        auto get_device_proc = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
            vk.get_instance_proc_addr(vk.instance, "vkGetDeviceProcAddr"));
        auto create_pool = reinterpret_cast<PFN_vkCreateCommandPool>(get_device_proc(vk.device, "vkCreateCommandPool"));
        auto allocate = reinterpret_cast<PFN_vkAllocateCommandBuffers>(get_device_proc(vk.device, "vkAllocateCommandBuffers"));
        auto begin = reinterpret_cast<PFN_vkBeginCommandBuffer>(get_device_proc(vk.device, "vkBeginCommandBuffer"));
        auto barrier = reinterpret_cast<PFN_vkCmdPipelineBarrier>(get_device_proc(vk.device, "vkCmdPipelineBarrier"));
        auto copy = reinterpret_cast<PFN_vkCmdCopyBuffer>(get_device_proc(vk.device, "vkCmdCopyBuffer"));
        auto fill = reinterpret_cast<PFN_vkCmdFillBuffer>(get_device_proc(vk.device, "vkCmdFillBuffer"));
        auto end = reinterpret_cast<PFN_vkEndCommandBuffer>(get_device_proc(vk.device, "vkEndCommandBuffer"));
        auto submit = reinterpret_cast<PFN_vkQueueSubmit>(get_device_proc(vk.device, "vkQueueSubmit"));
        owner->device = vk.device;
        owner->destroy_pool = reinterpret_cast<PFN_vkDestroyCommandPool>(get_device_proc(vk.device, "vkDestroyCommandPool"));
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pool.queueFamilyIndex = vk.queue_family;
        require(create_pool(vk.device, &pool, nullptr, &owner->pool) == VK_SUCCESS, "native command pool creation failed");
        VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        info.commandPool = owner->pool; info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; info.commandBufferCount = 1u;
        VkCommandBuffer cb{};
        require(allocate(vk.device, &info, &cb) == VK_SUCCESS, "native command allocation failed");
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(begin(cb, &bi) == VK_SUCCESS, "native command begin failed");
        VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        memory.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0u, 1u, &memory, 0u, nullptr, 0u, nullptr);
        VkDeviceSize source_offset{}, destination_offset{};
        const auto src = vk.resolve_buffer(vk.backend, source, &source_offset);
        const auto dst = vk.resolve_buffer(vk.backend, destination, &destination_offset);
        require(src && dst, "native buffer lookup failed");
        VkBufferCopy region{source_offset, destination_offset, VkDeviceSize(count) * sizeof(uint)};
        copy(cb, src, dst, 1u, &region);
        memory.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0u, 1u, &memory, 0u, nullptr, 0u, nullptr);
        fill(cb, dst, destination_offset + VkDeviceSize(count - 1u) * sizeof(uint), sizeof(uint), 99u);
        require(end(cb) == VK_SUCCESS, "native command end failed");
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1u; si.pCommandBuffers = &cb;
        require(submit(vk.compute_queue, 1u, &si, VK_NULL_HANDLE) == VK_SUCCESS, "native queue submission failed");
        called = true;
    };
}
}// namespace

int main() {
    auto device = create_test_device();
    auto stream = device.create_stream();
    constexpr uint count = 257u;
    auto source = device.create_buffer<uint>(count), destination = device.create_buffer<uint>(count);
    auto result = device.create_buffer<uint>(count);
    Kernel produce = [](BufferVar<uint> out) { Uint i = dispatch_id(); out.write(i, i + 1u); };
    Kernel consume = [](BufferVar<uint> in, BufferVar<uint> out) {
        Uint i = dispatch_id(); out.write(i, in.read(i) * 3u);
    };
    auto producer = device.compile(produce, "native-callback-produce");
    auto consumer = device.compile(consume, "native-callback-consume");
    vector<uint> actual(count);
    for (bool deferred : {false, true}) {
        std::atomic<bool> destroyed{}, called{}, host_ready{};
        auto owner = std::make_shared<NativeWork>(device, destroyed);
        auto fn = make_native_copy(owner, source.handle(), destination.handle(), count,
                                   called, deferred ? &host_ready : nullptr);
        owner.reset();
        stream << producer(source).dispatch(count);
        if (deferred) stream << HostFunctionCommand::create(true, [&] { host_ready = true; });
        stream << native_callback(std::move(fn)) << consumer(destination, result).dispatch(count)
               << result.download(actual.data()) << synchronize() << commit();
        require(called && destroyed, "native callback/capture retirement did not finish");
        for (uint i = 0u; i < count; ++i)
            require(actual[i] == (i + 1u == count ? 99u : i + 1u) * 3u,
                    "engine/native/engine ordering or memory visibility failed");
    }
    // A terminal native callback must still be included in stream synchronize,
    // with its capture kept alive until the post-native engine fence completes.
    std::atomic<bool> destroyed{}, called{};
    auto owner = std::make_shared<NativeWork>(device, destroyed);
    auto fn = make_native_copy(owner, source.handle(), destination.handle(), count, called);
    owner.reset();
    stream << native_callback(std::move(fn)) << commit();
    require(called && !destroyed, "terminal native capture was not retained through its fence");
    stream << synchronize() << commit();
    require(destroyed, "terminal native capture was not released after synchronization");
    std::cout << "native queue ordering, deferred host work, capture lifetime and terminal fence passed" << std::endl;
}
