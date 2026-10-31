//
// Created by Zero on 2023/11/23.
//

#include "../dsl.h"

namespace ocarina {

OC_MAKE_INSTANCE_FUNC_DEF(Env, s_env)

void clear_global_vars() noexcept {
    Env::instance().clear_global_vars();
}

namespace detail {
[[nodiscard]] Var<uint> correct_index(Var<uint> index, Var<uint> size, const string &desc,
                                      const string &tb) noexcept {
    if (Env::valid_check()) {
        if_(index >= size, [&] {
            string tips = ocarina::format("buffer access over boundary : {}, ", desc.c_str());
            $warn_with_location(tips + "index = {}, size = {}, current thread will be terminated \n" + tb, index, size);
            index = 0;
            $return();
        });
    }
    return index;
}

[[nodiscard]] Var<uint> correct_index(Var<uint> index, uint size, const string &desc,
                                      const string &tb) noexcept {
    if (Env::valid_check()) {
        if_(index >= size, [&] {
            string tips = ocarina::format("buffer access over boundary : {}, ", desc.c_str());
            $warn_with_location(tips + "index = {}, size = {}, current thread will be terminated \n" + tb, index, size);
            index = 0;
            $return();
        });
    }
    return index;
}

[[nodiscard]] Var<uint> divide(Var<uint> lhs, Var<uint> rhs) noexcept {
    return lhs / rhs;
}

}// namespace detail
}// namespace ocarina