//
// Created by Z on 05/08/2026.
//

#pragma once

#include "core/concepts.h"
#include "core/stl.h"
#include "core/type.h"
#include "core/util/logging.h"
#include "op.h"
#include "variable.h"

namespace horizon
{
class Function;

class ASTNode
{
public:
    virtual ~ASTNode() = default;

    [[nodiscard]] virtual Function *context() noexcept = 0;
    [[nodiscard]] virtual const Function *context() const noexcept = 0;

    virtual bool check_context(const Function *ctx) const noexcept
    {
        return context() == ctx;
    }
};

namespace detail
{
template <typename T> bool check_context(const T &t, const Function *ctx)
{
    if constexpr (concepts::iterable<T>)
    {
        bool ret = true;
        for (const auto &item : t)
        {
#ifndef NDEBUG
            ret = check_context(item, ctx) && ret;
#else
            ret = ret && check_context(item, ctx);
#endif
        }
        return ret;
    }
    else if constexpr (requires() { t.check_context(ctx); })
    {
        return t.check_context(ctx);
    }
    else if constexpr (requires() { t->check_context(ctx); })
    {
        if (t == nullptr)
        {
            return true;
        }
        return t->check_context(ctx);
    }
    else
    {
        static_assert(always_false_v<T>);
    }
}
} // namespace detail

#define OC_MAKE_CHECK_CONTEXT_ELEMENT(name) &&detail::check_context((name), ctx)

#define OC_MAKE_CHECK_CONTEXT(Super, ...)                                                                              \
    bool check_context(const Function *ctx) const noexcept override                                                    \
    {                                                                                                                  \
        return Super::check_context(ctx) MAP(OC_MAKE_CHECK_CONTEXT_ELEMENT, __VA_ARGS__);                              \
    }

} // namespace horizon
