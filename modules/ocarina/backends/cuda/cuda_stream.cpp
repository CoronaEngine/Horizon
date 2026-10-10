//
// Created by Zero on 09/07/2022.
//

#include "cuda_stream.h"
#include "cuda_command_visitor.h"
#include "util.h"
#include "cuda_device.h"
#include <condition_variable>
#include <deque>
#include <thread>

namespace ocarina {

struct CUDAStream::CallbackState {
    struct Task {
        CallbackState *owner;
        uint64_t serial;
        std::function<void()> function;
    };

    CUDADevice *device;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Task *> pending;
    uint64_t issued = 0;
    uint64_t retired = 0;
    bool stopping = false;
    std::thread worker;

    explicit CallbackState(CUDADevice *device) : device(device) {
        worker = std::thread([this] {
            for (;;) {
                Task *task;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock, [&] { return stopping || !pending.empty(); });
                    if (pending.empty()) return;
                    task = pending.front();
                    pending.pop_front();
                }
                const auto serial = task->serial;
                // Destroying a keep_alive capture may call cuModuleUnload,
                // cuMemFree, etc. CUDA forbids these calls inside its host
                // callback, so only this ordinary host thread releases it.
                this->device->use_context([&] { delete task; });
                {
                    std::lock_guard lock(mutex);
                    retired = serial;
                }
                changed.notify_all();
            }
        });
    }

    ~CallbackState() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        changed.notify_all();
        worker.join();
    }

    void enqueue(CUstream stream, std::function<void()> function) {
        uint64_t serial;
        {
            std::lock_guard lock(mutex);
            serial = ++issued;
        }
        auto *task = new Task{this, serial, std::move(function)};
        OC_CU_CHECK(cuLaunchHostFunc(stream, [](void *data) {
            auto *task = static_cast<Task *>(data);
            task->function();
            auto *owner = task->owner;
            {
                std::lock_guard lock(owner->mutex);
                owner->pending.push_back(task);
            }
            owner->changed.notify_all();
            // Do not touch task after publishing it, and do not wait for its
            // destruction here: the reaper may be inside a blocking CUDA API.
        }, task));
    }

    void drain() {
        std::unique_lock lock(mutex);
        const auto target = issued;
        changed.wait(lock, [&] { return retired >= target; });
    }
};

CUDAStream::CUDAStream(CUDADevice *device) noexcept
    : device_(device) {
    OC_CU_CHECK(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING));
    OC_CU_CHECK(cuEventCreate(&event_, CU_EVENT_DISABLE_TIMING));
    callbacks_ = std::make_unique<CallbackState>(device_);
}

CUDAStream::~CUDAStream() noexcept {
    synchronize();
    callbacks_.reset();
    device_->use_context([&] {
        OC_CU_CHECK(cuStreamDestroy(stream_));
        OC_CU_CHECK(cuEventDestroy(event_));
    });
}

void CUDAStream::commit(const Commit &commit) noexcept {
    CUDACommandVisitor cmd_visitor{device_, stream_, this};
    for (auto &cmd : command_queue_) {
        cmd->accept(cmd_visitor);
    }
    command_queue_.clear();
    if (commit.callback) commit.callback(reinterpret_cast<void *>(stream_));
}

void CUDAStream::host_function(std::function<void()> function) noexcept {
    callbacks_->enqueue(stream_, std::move(function));
    mark_host_work_pending();
}

void CUDAStream::prepare_host_upload() noexcept {
    // CUDA can stage a pageable source immediately, before an earlier stream
    // callback/download has produced it. Only uploads following host output
    // need this wait; ordinary independent uploads retain the async fast path.
    if (pending_host_work_) synchronize();
}

void CUDAStream::synchronize() noexcept {
    device_->use_context([&] { OC_CU_CHECK(cuStreamSynchronize(stream_)); });
    // cuStreamSynchronize finishes callback execution, but capture destruction
    // continues on our reaper. RHI synchronization includes both operations.
    callbacks_->drain();
    pending_host_work_ = false;
}

void CUDAStream::barrier() noexcept {
    constexpr CUevent_wait_flags_enum flags = CU_EVENT_WAIT_DEFAULT;
    OC_CU_CHECK(cuEventRecord(event_, stream_));
    OC_CU_CHECK(cuStreamWaitEvent(stream_, event_, flags));
}
}// namespace ocarina
