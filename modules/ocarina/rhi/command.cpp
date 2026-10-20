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

BufferReallocateCommand::BufferReallocateCommand(RHIResource *rhi_resource,
                                                 size_t new_size, bool async)
    : Command(async), new_size_(new_size),
      rhi_resource_(rhi_resource) {}

span<void *> ShaderDispatchCommand::args() noexcept {
    // A reusable ShaderInvoke shares its immutable user-argument snapshot, but
    // each command owns its launch dimensions and argument-pointer list.
    auto arguments = argument_list_->ptr();
    args_.assign(arguments.begin(), arguments.end());
    if (argument_list_->has_accel_arguments()) {
        auto original = argument_list_->argument_data();
        auto patched = argument_data();
        for (auto &arg : args_) {
            const auto offset = static_cast<const std::byte *>(arg) - original.data();
            arg = const_cast<std::byte *>(patched.data()) + offset;
        }
    }
    if (!argument_list_->function().is_raytracing()) {
        args_.push_back(&dispatch_dim_);
    }
    return args_;
}

span<const std::byte> ShaderDispatchCommand::argument_data() noexcept {
    auto original = argument_list_->argument_data();
    if (!argument_list_->has_accel_arguments()) return original;
    if (patched_argument_data_.empty()) {
        patched_argument_data_.assign(original.begin(), original.end());
    }
    argument_list_->refresh_accel_arguments(patched_argument_data_);
    return patched_argument_data_;
}

size_t ShaderDispatchCommand::params_size() noexcept {
    return structure_size(argument_list_->blocks());
}

}// namespace ocarina
