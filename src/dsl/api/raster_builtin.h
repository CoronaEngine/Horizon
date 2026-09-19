#pragma once

#include "../core/var.h"
#include <stdexcept>

namespace horizon::dsl
{
namespace detail
{
[[nodiscard]] inline ast::Function &current_raster_builder()
{
    auto *function = ast::Function::current();
    if (function == nullptr)
    {
        throw std::logic_error("Raster builtin requires an active Function.");
    }
    return *function;
}
}  // namespace detail

[[nodiscard]] inline Float4 vertex_position()
{
    return Float4{detail::current_raster_builder().vertex_position()};
}
[[nodiscard]] inline Uint vertex_index()
{
    return Uint{detail::current_raster_builder().vertex_index()};
}
[[nodiscard]] inline Uint instance_index()
{
    return Uint{detail::current_raster_builder().instance_index()};
}
[[nodiscard]] inline Uint draw_index()
{
    return Uint{detail::current_raster_builder().draw_index()};
}
[[nodiscard]] inline Float4 fragment_coord()
{
    return Float4{detail::current_raster_builder().fragment_coord()};
}
[[nodiscard]] inline Bool front_facing()
{
    return Bool{detail::current_raster_builder().front_facing()};
}
inline void discard()
{
    detail::current_raster_builder().discard();
}
}  // namespace horizon::dsl
