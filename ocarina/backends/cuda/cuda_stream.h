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
public:
    explicit CUDAStream(CUDADevice *device) noexcept;

    ~CUDAStream() noexcept;

    void add_command(Command *cmd) noexcept override {
        command_queue_.push_back(cmd);
    }

    void barrier() noexcept override;
    void commit(const Commit &cmt) noexcept override;
};
}// namespace ocarina