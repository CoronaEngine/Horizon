//
// Created by Zero on 09/07/2022.
//

#include "cuda_stream.h"
#include "cuda_command_visitor.h"
#include "util.h"
#include "cuda_device.h"

namespace ocarina {

CUDAStream::CUDAStream(CUDADevice *device) noexcept
    : device_(device) {
    OC_CU_CHECK(cuStreamCreate(&stream_, CU_STREAM_NON_BLOCKING));
    OC_CU_CHECK(cuEventCreate(&event_, CU_EVENT_DISABLE_TIMING));
}

CUDAStream::~CUDAStream() noexcept {
    OC_CU_CHECK(cuStreamDestroy(stream_));
    OC_CU_CHECK(cuEventDestroy(event_));
}

void CUDAStream::commit(const Commit &commit) noexcept {
    CUDACommandVisitor cmd_visitor{device_, stream_};
    for (auto &cmd : command_queue_) {
        cmd->accept(cmd_visitor);
    }
    command_queue_.clear();
}

void CUDAStream::barrier() noexcept {
    constexpr CUevent_wait_flags_enum flags = CU_EVENT_WAIT_DEFAULT;
    OC_CU_CHECK(cuEventRecord(event_, stream_));
    OC_CU_CHECK(cuStreamWaitEvent(stream_, event_, flags));
}
}// namespace ocarina