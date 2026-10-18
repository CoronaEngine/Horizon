#include "command_graph.h"

namespace ocarina {

CommandGraph::CommandGraph(Device::Impl *device) noexcept
    : RHIResource(device, Tag::COMMAND_GRAPH, 0u) {
        handle_ = device->create_command_graph(&creation_status_);
}

const CommandGraphStatus &CommandGraph::prepare(
    const CommandBatch &nodes) noexcept {
    auto status = ocarina::make_shared<CommandGraphStatus>();
    if (!valid()) {
        *status = creation_status_;
        if (status->ok()) {
            status->code = CommandGraphStatusCode::InvalidGraph;
        }
    } else if (nodes.empty()) {
        status->code = CommandGraphStatusCode::EmptyTopology;
    } else {
        const bool prepared = device_->prepare_command_graph(
            handle_,
            span<Command *const>{nodes.data(), nodes.size()},
            status.get());
        prepared_ = prepared && status->ok();
    }
    last_status_ = status;
    return *last_status_;
}

CommandGraphDispatchCommand *CommandGraph::dispatch(
    CommandBatch nodes) noexcept {
    auto status = ocarina::make_shared<CommandGraphStatus>();
    if (!valid()) {
        *status = creation_status_;
        if (status->ok()) {
            status->code = CommandGraphStatusCode::InvalidGraph;
        }
    } else if (!prepared_) {
        status->code = CommandGraphStatusCode::InstantiationFailed;
    } else if (nodes.empty()) {
        status->code = CommandGraphStatusCode::EmptyTopology;
    }
    vector<Command *> owned_nodes;
    owned_nodes.reserve(nodes.size());
    for (Command *node : nodes) {
        owned_nodes.push_back(node);
    }
    nodes.clear();
    last_status_ = status;
    return CommandGraphDispatchCommand::create(
        handle_, ocarina::move(owned_nodes), ocarina::move(status));
}

} // namespace ocarina
