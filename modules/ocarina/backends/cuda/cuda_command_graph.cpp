#include "cuda_command_graph.h"

#include "cuda_device.h"
#include "cuda_shader.h"

namespace ocarina {
namespace {

void fail(
    CommandGraphStatus *status,
    CommandGraphStatusCode code,
    CUresult result = CUDA_SUCCESS) noexcept {
    if (status) {
        status->code = code;
        status->backend_code = static_cast<int64_t>(result);
    }
}

[[nodiscard]] bool same_dim(uint3 lhs, uint3 rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

} // namespace

bool CUDACommandGraph::NodeSignature::operator==(
    const NodeSignature &other) const noexcept {
    return kind == other.kind && function == other.function &&
        same_dim(grid, other.grid) && same_dim(block, other.block) &&
        shared_memory_bytes == other.shared_memory_bytes;
}

CUDACommandGraph::~CUDACommandGraph() noexcept {
    device_->use_context([&] { reset(); });
}

void CUDACommandGraph::reset() noexcept {
    if (executable_ != nullptr) {
        const CUresult result = cuGraphExecDestroy(executable_);
        if (result != CUDA_SUCCESS) {
            OC_WARNING_FORMAT(
                "CUDA command graph executable destroy failed with code {}.",
                static_cast<int>(result));
        }
        executable_ = nullptr;
    }
    if (graph_ != nullptr) {
        const CUresult result = cuGraphDestroy(graph_);
        if (result != CUDA_SUCCESS) {
            OC_WARNING_FORMAT(
                "CUDA command graph destroy failed with code {}.",
                static_cast<int>(result));
        }
        graph_ = nullptr;
    }
    nodes_.clear();
    signatures_.clear();
}

bool CUDACommandGraph::describe(
    Command *command,
    KernelDescription &description,
    CommandGraphStatus *status) noexcept {
    if (auto *shader = dynamic_cast<ShaderDispatchCommand *>(command)) {
        auto *cuda_shader = shader->entry<CUDAShader *>();
        if (cuda_shader == nullptr ||
            !cuda_shader->describe_graph_kernel(shader, description.params)) {
            fail(status, CommandGraphStatusCode::NodeDescriptionFailed);
            return false;
        }
        description.signature.kind = NodeKind::Shader;
    } else if (auto *external =
                   dynamic_cast<ExternalComputeDispatchCommand *>(command)) {
        const auto &external_status = external->status();
        if (!external_status || !external_status->ok() ||
            external->entry() == 0u || external->parameter_data().empty()) {
            fail(status, CommandGraphStatusCode::NodeDescriptionFailed);
            return false;
        }
        description.external_parameters[0u] = const_cast<std::byte *>(
            external->parameter_data().data());
        description.params.func = reinterpret_cast<CUfunction>(
            external->entry());
        const uint3 grid = external->grid_dim();
        const uint3 block = external->block_dim();
        description.params.gridDimX = grid.x;
        description.params.gridDimY = grid.y;
        description.params.gridDimZ = grid.z;
        description.params.blockDimX = block.x;
        description.params.blockDimY = block.y;
        description.params.blockDimZ = block.z;
        description.params.sharedMemBytes = external->shared_memory_bytes();
        description.params.kernelParams = description.external_parameters.data();
        description.params.extra = nullptr;
        description.signature.kind = NodeKind::ExternalCompute;
    } else {
        fail(status, CommandGraphStatusCode::UnsupportedNode);
        return false;
    }

    description.signature.function = description.params.func;
    description.signature.grid = make_uint3(
        description.params.gridDimX,
        description.params.gridDimY,
        description.params.gridDimZ);
    description.signature.block = make_uint3(
        description.params.blockDimX,
        description.params.blockDimY,
        description.params.blockDimZ);
    description.signature.shared_memory_bytes =
        description.params.sharedMemBytes;
    return description.signature.function != nullptr &&
        description.signature.grid.x != 0u &&
        description.signature.grid.y != 0u &&
        description.signature.grid.z != 0u &&
        description.signature.block.x != 0u &&
        description.signature.block.y != 0u &&
        description.signature.block.z != 0u;
}

bool CUDACommandGraph::build(
    span<Command *const> commands,
    CommandGraphStatus *status) noexcept {
    const CUresult create_result = cuGraphCreate(&graph_, 0u);
    if (create_result != CUDA_SUCCESS) {
        fail(status, CommandGraphStatusCode::GraphCreationFailed, create_result);
        reset();
        return false;
    }

    nodes_.reserve(commands.size());
    signatures_.reserve(commands.size());
    CUgraphNode dependency{};
    for (Command *command : commands) {
        KernelDescription description{};
        if (!describe(command, description, status)) {
            reset();
            return false;
        }
        CUgraphNode node{};
        const CUresult node_result = cuGraphAddKernelNode(
            &node,
            graph_,
            dependency != nullptr ? &dependency : nullptr,
            dependency != nullptr ? 1u : 0u,
            &description.params);
        if (node_result != CUDA_SUCCESS) {
            fail(status, CommandGraphStatusCode::NodeCreationFailed, node_result);
            reset();
            return false;
        }
        nodes_.push_back(node);
        signatures_.push_back(description.signature);
        dependency = node;
    }

    const CUresult instantiate_result = cuGraphInstantiate(
        &executable_, graph_, 0u);
    if (instantiate_result != CUDA_SUCCESS) {
        fail(status, CommandGraphStatusCode::InstantiationFailed,
             instantiate_result);
        reset();
        return false;
    }
    ++generation_;
    if (status) {
        status->graph_generation = generation_;
        status->launch_count = launch_count_;
        status->node_count = static_cast<uint32_t>(nodes_.size());
    }
    return true;
}

bool CUDACommandGraph::update(
    span<Command *const> commands,
    CommandGraphStatus *status) noexcept {
    if (commands.size() != signatures_.size() ||
        commands.size() != nodes_.size()) {
        fail(status, CommandGraphStatusCode::TopologyMismatch);
        return false;
    }
    for (size_t index = 0u; index < commands.size(); ++index) {
        KernelDescription description{};
        if (!describe(commands[index], description, status)) {
            return false;
        }
        if (!(description.signature == signatures_[index])) {
            fail(status, CommandGraphStatusCode::TopologyMismatch);
            return false;
        }
        const CUresult update_result = cuGraphExecKernelNodeSetParams(
            executable_, nodes_[index], &description.params);
        if (update_result != CUDA_SUCCESS) {
            fail(status, CommandGraphStatusCode::NodeUpdateFailed, update_result);
            return false;
        }
    }
    return true;
}

bool CUDACommandGraph::execute_in_context(
    span<Command *const> commands,
    CUstream stream,
    CommandGraphStatus *status) noexcept {
    if (!status || stream == nullptr || commands.empty()) {
        fail(status, commands.empty()
            ? CommandGraphStatusCode::EmptyTopology
            : CommandGraphStatusCode::InvalidGraph);
        return false;
    }
    status->code = CommandGraphStatusCode::Success;
    status->backend_code = 0;
    if (executable_ == nullptr) {
        fail(status, CommandGraphStatusCode::InstantiationFailed);
        return false;
    }
    if (!update(commands, status)) {
        return false;
    }

    const CUresult launch_result = cuGraphLaunch(executable_, stream);
    if (launch_result != CUDA_SUCCESS) {
        fail(status, CommandGraphStatusCode::LaunchFailed, launch_result);
        return false;
    }
    ++launch_count_;
    status->graph_generation = generation_;
    status->launch_count = launch_count_;
    status->node_count = static_cast<uint32_t>(nodes_.size());
    return true;
}

bool CUDACommandGraph::prepare(
    span<Command *const> commands,
    CommandGraphStatus *status) noexcept {
    if (!status || commands.empty()) {
        fail(status, commands.empty()
            ? CommandGraphStatusCode::EmptyTopology
            : CommandGraphStatusCode::InvalidGraph);
        return false;
    }
    status->code = CommandGraphStatusCode::Success;
    status->backend_code = 0;
    status->graph_generation = 0u;
    status->launch_count = launch_count_;
    status->node_count = 0u;
    if (executable_ == nullptr) {
        return build(commands, status);
    }
    if (!update(commands, status)) {
        return false;
    }
    status->graph_generation = generation_;
    status->launch_count = launch_count_;
    status->node_count = static_cast<uint32_t>(nodes_.size());
    return true;
}

bool CUDACommandGraph::execute(
    span<Command *const> commands,
    CUstream stream,
    CommandGraphStatus *status) noexcept {
    return device_->use_context([&] {
        return execute_in_context(commands, stream, status);
    });
}

} // namespace ocarina
