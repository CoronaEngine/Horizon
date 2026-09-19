#pragma once

#include "../types/struct.h"
#include "ast/raster_validation.h"
#include "func.h"
#include "operators.h"
#include "raster_builtin.h"
#include "syntax.h"

namespace horizon::dsl
{
namespace detail
{
template <typename T>
inline constexpr bool raster_argument_v =
    !std::is_pointer_v<std::remove_cvref_t<T>> && !std::is_void_v<T> &&
    (!std::is_reference_v<T> || (std::is_lvalue_reference_v<T> && std::is_const_v<std::remove_reference_t<T>>));

template <typename Ret, typename Func, typename... Args> consteval bool raster_constructor_compatible()
{
    if constexpr (!(raster_argument_v<Args> && ...) || std::is_pointer_v<Ret> || std::is_reference_v<Ret>)
    {
        return false;
    }
    else if constexpr (!std::is_invocable_v<Func, prototype_to_var_t<Args>...>)
    {
        return false;
    }
    else if constexpr (std::is_void_v<Ret>)
    {
        return std::is_void_v<std::invoke_result_t<Func, prototype_to_var_t<Args>...>>;
    }
    else
    {
        return std::is_same_v<std::invoke_result_t<Func, prototype_to_var_t<Args>...>, Var<Ret>>;
    }
}

template <typename T> struct raster_definition_argument : std::false_type
{
};
template <typename T> struct raster_definition_argument<Var<T>> : std::true_type
{
};
template <typename T> struct raster_definition_argument<const Var<T> &> : std::true_type
{
};

template <typename Signature> struct raster_definition_signature;
template <typename Ret, typename... Args> struct raster_definition_signature<Ret(Args...)>
{
    static constexpr bool value = (raster_definition_argument<Args>::value && ...) &&
                                  (std::is_void_v<Ret> || (is_var_v<Ret> && !std::is_reference_v<Ret>));
};

template <typename Func> consteval bool deducible_raster_function()
{
    using F = std::remove_cvref_t<Func>;
    if constexpr (std::is_function_v<F> || (std::is_pointer_v<F> && std::is_function_v<std::remove_pointer_t<F>>) ||
                  requires { &F::operator(); })
    {
        return raster_definition_signature<canonical_signature_t<F>>::value;
    }
    else
    {
        return false;
    }
}

template <ast::Function::Tag Stage, typename Signature> class RasterStage;

template <ast::Function::Tag Stage, typename Ret, typename... Args> class RasterStage<Stage, Ret(Args...)>
{
private:
    shared_ptr<const ast::Function> function_;

public:
    using signature = Ret(Args...);
    using return_type = Ret;

    template <typename Func>
    requires(raster_constructor_compatible<Ret, Func, Args...>())
    explicit RasterStage(Func &&func)
        : function_(ast::Function::define_raster(
              Stage, Type::of<Ret>(),
              [&]
              {
                  if constexpr (std::is_void_v<Ret>)
                  {
                      detail::create<Args...>(std::forward<Func>(func), std::index_sequence_for<Args...>{});
                  }
                  else
                  {
                      auto result =
                          detail::create<Args...>(std::forward<Func>(func), std::index_sequence_for<Args...>{});
                      ast::Function::current()->return_(result.expression());
                  }
              }))
    {
    }

    [[nodiscard]] shared_ptr<const ast::Function> function() const noexcept
    {
        return function_;
    }
};
}  // namespace detail

template <typename Signature> class VertexShader;
template <typename Ret, typename... Args>
class VertexShader<Ret(Args...)> : public detail::RasterStage<ast::Function::Vertex, Ret(Args...)>
{
public:
    using detail::RasterStage<ast::Function::Vertex, Ret(Args...)>::RasterStage;
};

template <typename Signature> class FragmentShader;
template <typename Ret, typename... Args>
class FragmentShader<Ret(Args...)> : public detail::RasterStage<ast::Function::Fragment, Ret(Args...)>
{
public:
    using detail::RasterStage<ast::Function::Fragment, Ret(Args...)>::RasterStage;
};

template <typename Func>
requires(detail::deducible_raster_function<Func>())
VertexShader(Func &&) -> VertexShader<detail::dsl_function_t<Func>>;

template <typename Func>
requires(detail::deducible_raster_function<Func>())
FragmentShader(Func &&) -> FragmentShader<detail::dsl_function_t<Func>>;

namespace detail
{
template <typename VS, typename FS> struct raster_pair_compatible : std::false_type
{
};

template <typename VRet, typename... VArgs, typename FRet, typename... FArgs>
struct raster_pair_compatible<VertexShader<VRet(VArgs...)>, FragmentShader<FRet(FArgs...)>>
    : std::bool_constant<(raster_argument_v<VArgs> && ...) && (raster_argument_v<FArgs> && ...) &&
                         std::is_same_v<std::tuple<std::remove_cvref_t<FArgs>...>,
                                        std::conditional_t<std::is_void_v<VRet>, std::tuple<>, std::tuple<VRet>>>>
{
};
}  // namespace detail

template <typename VS, typename FS>
inline constexpr bool is_raster_pair_compatible_v =
    detail::raster_pair_compatible<std::remove_cvref_t<VS>, std::remove_cvref_t<FS>>::value;

template <typename VS, typename FS> class RasterShader
{
private:
    VS vertex_;
    FS fragment_;

public:
    RasterShader(VS vertex, FS fragment)
    requires(is_raster_pair_compatible_v<VS, FS>)
        : vertex_(std::move(vertex)), fragment_(std::move(fragment))
    {
        if (!vertex_.function() || !fragment_.function())
        {
            throw ast::RasterValidationError(
                {{ast::RasterDiagnosticCode::InvalidStage, "Cannot pair a moved-from shader stage"}});
        }
        auto diagnostics = ast::validate_raster_pair(*vertex_.function(), *fragment_.function());
        if (!diagnostics.empty())
        {
            throw ast::RasterValidationError(std::move(diagnostics));
        }
    }

    [[nodiscard]] const VS &vertex() const noexcept
    {
        return vertex_;
    }
    [[nodiscard]] const FS &fragment() const noexcept
    {
        return fragment_;
    }
};

template <typename VS, typename FS>
requires(is_raster_pair_compatible_v<VS, FS>)
RasterShader(VS, FS) -> RasterShader<VS, FS>;
}  // namespace horizon::dsl
