//
// Created by Zero on 10/07/2022.
//

#include "command_queue.h"
#include "command.h"
#include "core/util/util.h"

namespace ocarina {

CommandBatch &CommandBatch::operator<<(ocarina::Command *command) noexcept {
    push_back(command);
    return *this;
}

CommandBatch &CommandBatch::operator<<(const vector<Command *> &commands) noexcept {
    append(*this, commands);
    return *this;
}

CommandBatch &CommandBatch::operator<<(std::function<void()> func) noexcept {
    return (*this) << HostFunctionCommand::create(true, ocarina::move(func));
}

void CommandBatch::accept(CommandVisitor &visitor) const noexcept {
    for (const Command *command : (*this)) {
        command->accept(visitor);
    }
}

void CommandBatch::recycle() noexcept {
    for (Command *command : (*this)) {
        command->recycle();
    }
}

void CommandQueue::recycle() noexcept {
    for (Command *command : commands_) {
        command->recycle();
    }
}

void CommandQueue::pop_back() {
    commands_.pop_back();
}

void CommandQueue::clear() noexcept {
    recycle();
    commands_.clear();
}

}// namespace ocarina