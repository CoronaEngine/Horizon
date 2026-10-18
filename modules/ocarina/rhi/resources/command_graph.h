#pragma once

#include "resource.h"
#include "rhi/command.h"
#include "rhi/command_queue.h"
#include "rhi/device.h"

namespace ocarina {

class OC_RHI_API CommandGraph final : public RHIResource {
private:
    CommandGraphStatus creation_status_{};
    SP<CommandGraphStatus> last_status_{};
    bool prepared_{};

public:
    CommandGraph() = default;
    explicit CommandGraph(Device::Impl *device) noexcept;

    [[nodiscard]] bool valid() const noexcept {
        return RHIResource::valid() && handle_ != 0u && creation_status_.ok();
    }
    [[nodiscard]] const CommandGraphStatus &creation_status() const noexcept {
        return creation_status_;
    }
    [[nodiscard]] const SP<CommandGraphStatus> &last_status() const noexcept {
        return last_status_;
    }
    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] const CommandGraphStatus &prepare(
        const CommandBatch &nodes) noexcept;
    [[nodiscard]] CommandGraphDispatchCommand *dispatch(
        CommandBatch nodes) noexcept;
};

} // namespace ocarina
