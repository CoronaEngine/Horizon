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

[[nodiscard]] inline Uint primitive_index()
{
    return Uint{detail::current_raster_builder().primitive_index()};
}
[[nodiscard]] inline Float fragment_depth()
{
    return Float{detail::current_raster_builder().fragment_depth()};
}
[[nodiscard]] inline Float fragment_depth_greater_equal()
{
    return Float{detail::current_raster_builder().fragment_depth_greater_equal()};
}
[[nodiscard]] inline Float fragment_depth_less_equal()
{
    return Float{detail::current_raster_builder().fragment_depth_less_equal()};
}
[[nodiscard]] inline Uint sample_index()
{
    return Uint{detail::current_raster_builder().sample_index()};
}
[[nodiscard]] inline Uint sample_mask()
{
    return Uint{detail::current_raster_builder().sample_mask()};
}
[[nodiscard]] inline Uint sample_mask_output()
{
    return Uint{detail::current_raster_builder().sample_mask_output()};
}
[[nodiscard]] inline Uint render_target_array_index()
{
    return Uint{detail::current_raster_builder().render_target_array_index()};
}
[[nodiscard]] inline Uint viewport_array_index()
{
    return Uint{detail::current_raster_builder().viewport_array_index()};
}
[[nodiscard]] inline Uint stencil_ref()
{
    return Uint{detail::current_raster_builder().stencil_ref()};
}
[[nodiscard]] inline Uint shading_rate()
{
    return Uint{detail::current_raster_builder().shading_rate()};
}

// Distances are separate builtins and never consume ordinary interface locations.
template <size_t N>
requires(N > 0 && N <= 8)
[[nodiscard]] inline Var<array<float, N>> clip_distances()
{
    return Var<array<float, N>>{detail::current_raster_builder().clip_distances(static_cast<uint>(N))};
}
template <size_t N>
requires(N > 0 && N <= 8)
[[nodiscard]] inline Var<array<float, N>> cull_distances()
{
    return Var<array<float, N>>{detail::current_raster_builder().cull_distances(static_cast<uint>(N))};
}
}  // namespace horizon::dsl
