//
// Created by Zero on 30/04/2022.
//

#include "core/type.h"
#include "function.h"

namespace ocarina {

namespace {

void on_type_access_bridge(const Type *type) noexcept {
    if (auto f = Function::current(); f != nullptr && type->is_structure()) {
        const Type *resolved = Type::resolve(type, f->storage_policy());
        f->add_used_structure(resolved == nullptr ? type : resolved);
    }
}

void register_type_system_callbacks_once() noexcept {
    static bool registered = [] {
        detail::register_type_system_callbacks(detail::TypeSystemCallbacks{
            .on_type_access = &on_type_access_bridge,
        });
        return true;
    }();
    (void)registered;
}

[[maybe_unused]] const bool type_system_callbacks_registered = [] {
    register_type_system_callbacks_once();
    return true;
}();

}// namespace

}// namespace ocarina