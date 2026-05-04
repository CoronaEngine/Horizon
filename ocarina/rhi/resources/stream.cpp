//
// Created by Zero on 06/07/2022.
//

#include "stream.h"
#include "rhi/device.h"
#include "rhi/command.h"

namespace ocarina {
Stream::Stream(Device::Impl *device)
    : RHIResource(device, Tag::STREAM,
                  device->create_stream()) {}

Stream &Stream::operator<<(Command *command) noexcept {
    impl()->add_command(command);
    return *this;
}

Stream &Stream::operator<<(const CommandBatch &commands) noexcept {
    for (Command *cmd : commands) {
        (*this) << cmd;
    }
    return *this;
}

void Stream::commit(const Commit &commit) noexcept {
    impl()->commit(commit);
}

Stream &Stream::operator<<(std::function<void()> f) noexcept {
    return (*this) << HostFunctionCommand::create(true, ocarina::move(f));
}

Stream &Stream::operator<<(const Commit &cmt) noexcept {
    commit(cmt);
    return *this;
}
}// namespace ocarina