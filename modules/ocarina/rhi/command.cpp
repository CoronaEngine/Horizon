//
// Created by Zero on 10/07/2022.
//

#include "command.h"
#include <utility>
#include "resources/shader.h"

namespace ocarina {

OC_COMMAND_POOL_FUNCTIONS_IMPL

ShaderDispatchCommand::ShaderDispatchCommand(handle_ty entry, SP<ShaderArgumentPack> argument_list, uint3 dim)
    : Command(true), entry_(entry),
      argument_list_(ocarina::move(argument_list)), dispatch_dim_(dim) {
}

ExternalComputeDispatchCommand::ExternalComputeDispatchCommand(
    handle_ty entry,
    span<const std::byte> parameter_data,
    uint3 grid_dim,
    uint3 block_dim,
    uint32_t shared_memory_bytes,
    SP<ExternalComputeStatus> status) noexcept
    : Command(true),
      entry_(entry),
      parameter_data_(parameter_data.begin(), parameter_data.end()),
      grid_dim_(grid_dim),
      block_dim_(block_dim),
      shared_memory_bytes_(shared_memory_bytes),
      status_(ocarina::move(status)) {}

CommandGraphDispatchCommand::CommandGraphDispatchCommand(
    handle_ty graph,
    vector<Command *> nodes,
    SP<CommandGraphStatus> status) noexcept
    : Command(true),
      graph_(graph),
      nodes_(ocarina::move(nodes)),
      status_(ocarina::move(status)) {}

void CommandGraphDispatchCommand::recycle() noexcept {
    for (Command *node : nodes_) {
        if (node != nullptr) {
            node->recycle();
        }
    }
    nodes_.clear();
    CommandGraphDispatchCommand_pool().recycle(this);
}

BufferReallocateCommand::BufferReallocateCommand(RHIResource *rhi_resource,
                                                 size_t new_size, bool async)
    : Command(async), new_size_(new_size),
      rhi_resource_(rhi_resource) {}

span<void *> ShaderDispatchCommand::args() noexcept {
    return argument_list_->ptr();
}

span<const std::byte> ShaderDispatchCommand::argument_data() noexcept {
    return argument_list_->argument_data();
}

size_t ShaderDispatchCommand::params_size() noexcept {
    return structure_size(argument_list_->blocks());
}

}// namespace ocarina
