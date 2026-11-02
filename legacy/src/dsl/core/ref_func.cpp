#include "dsl/dsl.h"

namespace horizon::dsl::detail {
using namespace horizon::core;
using namespace horizon::math;
using namespace horizon::ast;

namespace {

template<typename Size>
[[nodiscard]] Var<uint> correct_index_impl(Var<uint> index, Size size,
                                           const string &desc,
                                           const string &traceback) noexcept {
    if_(index >= size, [&] {
        comment(horizon::dsl::format("buffer access out of bounds: {}, {}", desc, traceback));
        index = 0u;
        return_();
    });
    return index;
}

}// namespace

Var<uint> correct_index(Var<uint> index, Var<uint> size,
                        const string &desc, const string &traceback) noexcept {
    return correct_index_impl(index, size, desc, traceback);
}

Var<uint> correct_index(Var<uint> index, uint size,
                        const string &desc, const string &traceback) noexcept {
    return correct_index_impl(index, size, desc, traceback);
}

Var<uint> divide(Var<uint> lhs, Var<uint> rhs) noexcept {
    return lhs / rhs;
}

}// namespace horizon::dsl::detail
