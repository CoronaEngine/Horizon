//
// Created by Zero on 2023/11/17.
//

#pragma once

#include "math/basic_types.h"
#include "../core/type_trait.h"
#include "../core/var.h"
#include "../api/builtin.h"
#include "rhi/resources/managed.h"
#include "math/box.h"
#include "../api/stmt_builder.h"

namespace ocarina {
struct DebugData {
    Box2u range{};
    int enabled{1};
};
}// namespace ocarina

// clang-format off
OC_STRUCT(ocarina,DebugData, range, enabled){};
// clang-format on

namespace ocarina {

class Debugger {
private:
    Managed<DebugData> data_;
    mutable string desc_{};

private:
    Debugger() = default;
    Debugger(const Debugger &) = delete;
    Debugger(Debugger &&) = delete;
    Debugger operator=(const Debugger &) = delete;
    Debugger operator=(Debugger &&) = delete;
    friend class Env;

public:
    void reset() noexcept { data_[0] = DebugData{}; }
    void init(Device &device) noexcept { data_.reset_all(device, 1, "DebugData::data_"); }
    [[nodiscard]] auto &host_data() const noexcept { return data_.host_buffer()[0]; }
    [[nodiscard]] auto &host_data() noexcept { return data_.host_buffer()[0]; }
    [[nodiscard]] Command *upload(bool async = true) const noexcept { return data_.upload(async); }
    void upload_immediately() const noexcept { data_.upload_immediately(); }
    void filp_enabled() noexcept { host_data().enabled = !host_data().enabled; }
    [[nodiscard]] bool is_enabled() const noexcept { return host_data().enabled; }
    void disable() noexcept { host_data().enabled = false; }
    void reset_range() noexcept { host_data().range = Box2u{}; }
    void set_lower(uint2 lower) noexcept { host_data().range.extend(lower); }
    void set_upper(uint2 upper) noexcept { host_data().range.extend(upper); }

protected:
    /// for dsl
    [[nodiscard]] Bool _is_enabled() const noexcept { return cast<bool>(data_.read(0).enabled); }
    /// for dsl
    [[nodiscard]] DebugDataVar _device_data() const noexcept { return data_.read(0); }

public:
    /// for dsl
    template<typename Func>
    void execute(Func &&func) const noexcept {
        if (!desc_.empty()) {
            comment(desc_);
        }
        if_(_is_enabled(), [&] {
            Uint2 idx = dispatch_idx().xy();
            if_(_device_data()->range->contains(idx), [&] {
                if constexpr (std::invocable<Func, Uint2>) {
                    func(idx);
                } else {
                    func();
                }
            });
        });
        desc_ = "";
    }

    Debugger &set_description(const string &desc) {
        desc_ = desc;
        return *this;
    }

    template<typename Func>
    void operator*(Func &&func) const noexcept {
        execute(OC_FORWARD(func));
    }
};

}// namespace ocarina