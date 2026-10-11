//
// Created by Zero on 09/07/2022.
//

#pragma once

#include "core/stl.h"
#include "rhi/resources/stream.h"
#include <cuda.h>

namespace ocarina {
class CUDADevice;
class CUDAStream : public Stream::Impl {
private:
    CUstream stream_{};
    CUevent event_{};
    CUDADevice *device_{};
    bool pending_host_work_{};
    struct CallbackState;
    std::unique_ptr<CallbackState> callbacks_;
public:
    explicit CUDAStream(CUDADevice *device) noexcept;

    ~CUDAStream() noexcept;

    void add_command(Command *cmd) noexcept override {
        command_queue_.push_back(cmd);
    }

    void barrier() noexcept override;
    void commit(const Commit &cmt) noexcept override;
    void host_function(std::function<void()> function) noexcept;
    void mark_host_work_pending() noexcept { pending_host_work_ = true; }
    void prepare_host_upload() noexcept;
    void synchronize() noexcept;
};
}// namespace ocarina
