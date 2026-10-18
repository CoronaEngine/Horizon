#pragma once

#include "rhi/command.h"

#include <cuda.h>

namespace ocarina {

class CUDADevice;

class CUDACommandGraph final {
private:
    enum class NodeKind : uint8_t {
        Shader,
        ExternalCompute,
    };

    struct NodeSignature {
        NodeKind kind{};
        CUfunction function{};
        uint3 grid{};
        uint3 block{};
        uint32_t shared_memory_bytes{};

        [[nodiscard]] bool operator==(
            const NodeSignature &other) const noexcept;
    };

    struct KernelDescription {
        CUDA_KERNEL_NODE_PARAMS params{};
        std::array<void *, 1u> external_parameters{};
        NodeSignature signature{};
    };

    CUDADevice *device_{};
    CUgraph graph_{};
    CUgraphExec executable_{};
    vector<CUgraphNode> nodes_{};
    vector<NodeSignature> signatures_{};
    uint64_t generation_{};
    uint64_t launch_count_{};

    void reset() noexcept;
    [[nodiscard]] bool describe(
        Command *command,
        KernelDescription &description,
        CommandGraphStatus *status) noexcept;
    [[nodiscard]] bool build(
        span<Command *const> commands,
        CommandGraphStatus *status) noexcept;
    [[nodiscard]] bool update(
        span<Command *const> commands,
        CommandGraphStatus *status) noexcept;
    [[nodiscard]] bool execute_in_context(
        span<Command *const> commands,
        CUstream stream,
        CommandGraphStatus *status) noexcept;

public:
    explicit CUDACommandGraph(CUDADevice *device) noexcept : device_(device) {}
    ~CUDACommandGraph() noexcept;

    CUDACommandGraph(const CUDACommandGraph &) = delete;
    CUDACommandGraph &operator=(const CUDACommandGraph &) = delete;

    [[nodiscard]] bool prepare(
        span<Command *const> commands,
        CommandGraphStatus *status) noexcept;

    [[nodiscard]] bool execute(
        span<Command *const> commands,
        CUstream stream,
        CommandGraphStatus *status) noexcept;
};

} // namespace ocarina
