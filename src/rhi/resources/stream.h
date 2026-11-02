//
// Created by Zero on 06/07/2022.
//

#pragma once

#include "resource.h"
#include "rhi/device.h"
#include "rhi/command_queue.h"

namespace ocarina {

struct OC_RHI_API Commit {
    using func_ty = std::function<void(void *)>;
    func_ty callback;
    Commit() = default;
    explicit Commit(func_ty &&func) : callback(std::forward<func_ty>(func)) {}
};

template<typename... Args>
[[nodiscard]] Commit commit(Args &&...args) {
    return Commit(OC_FORWARD(args)...);
}

class OC_RHI_API Stream : public RHIResource {
public:
    class Impl {
    protected:
        CommandQueue command_queue_;

    public:
        virtual void barrier() noexcept = 0;
        virtual void commit(const Commit &commit) noexcept = 0;
        virtual void add_command(Command *cmd) noexcept = 0;
        virtual ~Impl() = default;

    private:
        [[nodiscard]] const CommandQueue &command_queue() const noexcept {
            return command_queue_;
        }
        friend class Stream;
    };

public:
    explicit Stream(Device::Impl *device);
    [[nodiscard]] Impl *impl() noexcept { return reinterpret_cast<Impl *>(handle_); }
    [[nodiscard]] const Impl *impl() const noexcept { return reinterpret_cast<const Impl *>(handle_); }
    [[nodiscard]] const CommandQueue &command_queue() const noexcept {
        return impl()->command_queue();
    }
    Stream &operator<<(Command *command) noexcept;
    Stream &operator<<(const CommandBatch &commands) noexcept;
    template<typename... Ts>
    Stream &operator<<(tuple<Ts...> &&tp) noexcept {
        auto func = [this]<size_t... i>(tuple<Ts...> &&tp,
                                        std::index_sequence<i...>) noexcept -> decltype(auto) {
            return (std::move(*this) << ... << std::move(std::get<i>(OC_FORWARD(tp))));
        };
        return func(OC_FORWARD(tp), std::index_sequence_for<Ts...>());
    }
    Stream &operator<<(const Commit &commit) noexcept;
    Stream &operator<<(std::function<void()> f) noexcept;
    void commit(const Commit &cmt) noexcept;
};

}// namespace ocarina