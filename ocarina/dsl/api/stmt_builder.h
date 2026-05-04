//
// Created by Zero on 21/06/2022.
//

#pragma once

#include "../core/func_traits.h"
#include "../core/var.h"
#include "../core/expr.h"
#include "ast/function.h"
#include "operators.h"

namespace ocarina {

inline void comment(const ocarina::string &str) noexcept {
    Function::current()->comment(str);
}

namespace detail {
template<typename Lhs, typename Rhs>
requires(!is_param_struct_v<expr_value_t<Lhs>> && !is_param_struct_v<expr_value_t<Rhs>>)
inline void assign(Lhs &&lhs, Rhs &&rhs) noexcept {
    if constexpr (concepts::assign_able<expr_value_t<Lhs>, expr_value_t<Rhs>>) {
        Function::current()->assign(
            detail::extract_expression(std::forward<Lhs>(lhs)),
            detail::extract_expression(std::forward<Rhs>(rhs)));
    } else if constexpr (std::is_pointer_v<std::remove_cvref_t<Rhs>>) {
        Function::current()->assign(OC_EXPR(lhs), rhs);
    } else if constexpr (std::is_pointer_v<std::remove_cvref_t<Lhs>>) {
        Function::current()->assign(lhs, OC_EXPR(rhs));
    } else {
        static_assert(always_false_v<Lhs, Rhs>, "invalid assignment operator !");
    }
}
}// namespace detail

template<typename T>
[[nodiscard]] inline Var<expr_value_t<T>> eval(T &&x) noexcept {
    return Var(OC_FORWARD(x));
}

template<typename T>
[[nodiscard]] inline Var<expr_value_t<T>> eval(const Expression *expr) noexcept {
    using RawType = expr_value_t<T>;
    return Var<RawType>(Expr<RawType>(expr));
}

template<typename T>
[[nodiscard]] inline Expr<expr_value_t<T>> make_expr(T &&x) noexcept {
    if constexpr (is_expr_v<T>) {
        return make_expr<T>(x.expression());
    } else {
        return Expr<expr_value_t<T>>(std::forward<T>(x));
    }
}

template<typename T>
[[nodiscard]] inline Expr<expr_value_t<T>> make_expr(const Expression *expr) noexcept {
    using RawType = expr_value_t<T>;
    return Expr<RawType>(expr);
}

template<typename T, typename Arg>
[[nodiscard]] inline auto cast(Arg &&arg) {
    if constexpr (is_dsl_v<Arg>) {
        return arg.template cast<T>();
    } else {
        return static_cast<T>(OC_FORWARD(arg));
    }
}

template<typename T, typename Arg>
[[nodiscard]] inline auto as(Arg &&arg) {
    if constexpr (is_dsl_v<Arg>) {
        return arg.template as<T>();
    } else {
        return bit_cast<T>(OC_FORWARD(arg));
    }
}

class Break {
public:
    Break() = default;
    void operator()(const string &str = "", std::source_location = {}) {
        if (!str.empty()) {
            comment(str);
        }
        Function::current()->break_();
    }
};

class Continue {
public:
    Continue() = default;
    void operator()(const string &str = "", std::source_location = {}) {
        if (!str.empty()) {
            comment(str);
        }
        Function::current()->continue_();
    }
};

namespace detail {

class ScopeStmtBuilder {
private:
    string str_{};

public:
    explicit ScopeStmtBuilder(const string &str) : str_(str) {}
    template<typename Body>
    void operator+(Body &&body) noexcept {
        comment("start " + str_);
        auto scope = Function::current()->scope();
        Function::current()->with(scope, OC_FORWARD(body));
        comment("end " + str_);
    }
};

class IfStmtBuilder {
private:
    IfStmt *if_{nullptr};

public:
    IfStmtBuilder() = default;
    explicit IfStmtBuilder(IfStmt *stmt) : if_(stmt) {}

    template<typename Condition>
    requires concepts::bool_able<expr_value_t<Condition>>
    [[nodiscard]] static IfStmtBuilder create(Condition &&condition) noexcept {
        IfStmtBuilder builder(Function::current()->if_(extract_expression(OC_FORWARD(condition))));
        return builder;
    }

    template<typename Condition>
    requires concepts::bool_able<expr_value_t<Condition>>
    [[nodiscard]] static IfStmtBuilder create_with_source_location(const string &str, Condition &&condition) noexcept {
        comment(str);
        return create(OC_FORWARD(condition));
    }

    template<typename TrueBranch>
    IfStmtBuilder &operator/(TrueBranch &&true_branch) noexcept {
        Function::current()->with(if_->true_branch(), std::forward<TrueBranch>(true_branch));
        return *this;
    }

    template<typename Func>
    IfStmtBuilder operator*(Func &&func) noexcept {
        return Function::current()->with(if_->false_branch(), std::forward<Func>(func));
    }

    template<typename FalseBranch>
    void operator%(FalseBranch &&false_branch) noexcept {
        Function::current()->with(if_->false_branch(), std::forward<FalseBranch>(false_branch));
    }

    template<typename Condition, typename TrueBranch>
    IfStmtBuilder elif_(Condition &&condition, TrueBranch &&true_branch) noexcept {
        return (*this) * [&] {
            return detail::IfStmtBuilder::create(std::forward<Condition>(condition));
        } / std::forward<TrueBranch>(true_branch);
    }

    template<typename Condition, typename TrueBranch>
    IfStmtBuilder elif_with_source_location(const string &str, Condition &&condition, TrueBranch &&true_branch) noexcept {
        return (*this) * [&] {
            return detail::IfStmtBuilder::create_with_source_location(str, std::forward<Condition>(condition));
        } / std::forward<TrueBranch>(true_branch);
    }

    template<typename FalseBranch>
    void else_(FalseBranch &&false_branch) noexcept {
        (*this) % std::forward<FalseBranch>(false_branch);
    }

    template<typename FalseBranch>
    void else_with_source_location(const string &str, FalseBranch &&false_branch) noexcept {
        comment(str);
        else_(OC_FORWARD(false_branch));
    }
};
}// namespace detail

template<typename Condition, typename TrueBranch>
detail::IfStmtBuilder if_(Condition &&condition,
                          TrueBranch &&true_branch) noexcept {
    return detail::IfStmtBuilder::create(std::forward<Condition>(condition)) / std::forward<TrueBranch>(true_branch);
}

namespace detail {

class CaseStmtBuilder {
private:
    SwitchCaseStmt *case_stmt_{nullptr};

public:
    explicit CaseStmtBuilder(SwitchCaseStmt *stmt)
        : case_stmt_(stmt) {}

    template<typename CaseExpr>
    requires concepts::integral<CaseExpr>
    [[nodiscard]] static CaseStmtBuilder create(CaseExpr &&case_expr) noexcept {
        CaseStmtBuilder builder(Function::current()->switch_case(extract_expression(std::forward<CaseExpr>(case_expr))));
        return builder;
    }

    template<typename CaseExpr>
    requires concepts::integral<CaseExpr>
    [[nodiscard]] static CaseStmtBuilder create_with_source_location(const string &str, CaseExpr &&case_expr) noexcept {
        comment(str);
        return create(OC_FORWARD(case_expr));
    }

    template<typename Body>
    void operator*(Body &&body) noexcept {
        Function::current()->with(case_stmt_->body(), [&] {
            body(Break{});
        });
    }
};

}// namespace detail

class Case {
public:
    Case() = default;
    template<typename CaseExpr>
    requires concepts::integral<CaseExpr>
    [[nodiscard]] detail::CaseStmtBuilder operator()(const string &str, CaseExpr &&case_expr) {
        return detail::CaseStmtBuilder::create_with_source_location(str, OC_FORWARD(case_expr));
    }

    template<typename CaseExpr, typename Body>
    requires concepts::integral<CaseExpr>
    void operator()(CaseExpr &&t, Body &&body) noexcept {
        detail::CaseStmtBuilder::create(std::forward<CaseExpr>(t)) * std::forward<Body>(body);
    }
};

namespace detail {
class DefaultStmtBuilder {
private:
    SwitchDefaultStmt *default_stmt_{};

public:
    DefaultStmtBuilder() noexcept
        : default_stmt_(Function::current()->switch_default()) {}

    DefaultStmtBuilder(const string &str) noexcept
        : default_stmt_(Function::current()->switch_default()) {
        comment(str);
    }

    template<typename Body>
    void operator*(Body &&body) noexcept {
        Function::current()->with(default_stmt_->body(), [&] {
            body(Break{});
        });
    }
};

}// namespace detail

class Default {
public:
    Default() = default;
    [[nodiscard]] detail::DefaultStmtBuilder operator()(const string &str) noexcept {
        return detail::DefaultStmtBuilder(str);
    }

    template<typename Body>
    requires std::is_invocable_v<Body, Break>
    void operator()(Body &&body) noexcept {
        detail::DefaultStmtBuilder() * std::forward<Body>(body);
    }
};

namespace detail {

class SwitchStmtBuilder {
private:
    SwitchStmt *switch_stmt_{nullptr};

public:
    explicit SwitchStmtBuilder(SwitchStmt *stmt)
        : switch_stmt_(stmt) {}

    template<typename T>
    requires concepts::switch_able<expr_value_t<T>>
    [[nodiscard]] static SwitchStmtBuilder create(T &&t) noexcept {
        SwitchStmtBuilder builder(Function::current()->switch_(t.expression()));
        return builder;
    }

    template<typename T>
    requires concepts::switch_able<expr_value_t<T>>
    [[nodiscard]] static SwitchStmtBuilder create_with_source_location(const string &str, T &&t) noexcept {
        comment(str);
        return create(OC_FORWARD(t));
    }

    template<typename CaseExpr, typename Body>
    SwitchStmtBuilder &case_(CaseExpr &&case_expr, Body &&body) noexcept {
        Function::current()->with(switch_stmt_->body(), [&] {
            CaseStmtBuilder::create(std::forward<CaseExpr>(case_expr)) * std::forward<Body>(body);
        });
        return *this;
    }

    template<typename Body>
    void default_(Body &&body) noexcept {
        Function::current()->with(switch_stmt_->body(), [&] {
            DefaultStmtBuilder() * std::forward<Body>(body);
        });
    }

    template<typename Body>
    SwitchStmtBuilder &operator*(Body &&func) && noexcept {
        Function::current()->with(switch_stmt_->body(), [&] {
            func(Case{}, Default{});
        });
        return *this;
    }
};

}// namespace detail

template<typename T, typename Body>
void switch_(T &&t, Body &&body) noexcept {
    detail::SwitchStmtBuilder::create(std::forward<T>(t)) * std::forward<Body>(body);
}

template<typename T>
decltype(auto) switch_(T &&t) noexcept {
    return detail::SwitchStmtBuilder::create(std::forward<T>(t));
}

template<typename T, typename Body>
void case_(T &&t, Body &&body) noexcept {
    detail::CaseStmtBuilder::create(std::forward<T>(t)) * std::forward<Body>(body);
}

template<typename Body>
void default_(Body &&body) noexcept {
    detail::DefaultStmtBuilder() * std::forward<Body>(body);
}

namespace syntax {
inline void break_(const string &str = "") noexcept {
    if (!str.empty()) {
        comment(str);
    }
    Function::current()->break_();
}

inline void continue_(const string &str = "") noexcept {
    if (!str.empty()) {
        comment(str);
    }
    Function::current()->continue_();
}
}// namespace syntax

namespace detail {
class LoopStmtBuilder {
private:
    LoopStmt *loop_{};

public:
    explicit LoopStmtBuilder(LoopStmt *loop) noexcept : loop_(loop) {}

    static auto create() noexcept {
        return LoopStmtBuilder(Function::current()->loop());
    }

    static auto create_with_source_location(const string &str) noexcept {
        comment(str);
        return LoopStmtBuilder(Function::current()->loop());
    }

    template<typename Func>
    LoopStmtBuilder &operator/(Func &&func) noexcept {
        Function::current()->with(loop_->body(), std::forward<Func>(func));
        return *this;
    }

    template<typename Body>
    void operator*(Body &&body) noexcept {
        Function::current()->with(loop_->body(), [&] {
            body(Continue{}, Break());
        });
    }
};
}// namespace detail

template<typename Body>
void loop(Body &&body) noexcept {
    detail::LoopStmtBuilder::create() * std::forward<Body>(body);
}

template<typename Condition, typename Body>
void while_(Condition &&cond, Body &&body) noexcept {
    detail::LoopStmtBuilder::create() * [&]() noexcept {
        if constexpr (std::is_invocable_v<Condition>) {
            if_(!cond(), [&] {
                syntax::break_();
            });
        } else {
            if_(!cond, [&] {
                syntax::break_();
            });
        }
        body();
    };
}

namespace detail {
template<typename T = int, typename U = T, typename V = U>
class ForStmtBuilder {
private:
    Var<T> var_;
    Var<U> end_;
    Var<V> step_;
    ForStmt *for_stmt_;

public:
    ForStmtBuilder(const Var<T> &begin, const Var<U> &end, const Var<V> &step)
        : var_(begin),
          end_(end),
          step_(step) {
        const BinaryExpr *negative_step = Function::current()->binary(Type::of<bool>(), BinaryOp::LESS,
                                                                      step_.expression(), OC_EXPR(T(0)));
        const BinaryExpr *condition = Function::current()->binary(Type::of<bool>(), BinaryOp::LESS,
                                                                  var_.expression(), end_.expression());
        const BinaryExpr *reduce_cond = Function::current()->binary(Type::of<bool>(), BinaryOp::GREATER,
                                                                    var_.expression(), end_.expression());
        const Expression *final_condition = Function::current()->conditional(Type::of<bool>(), negative_step,
                                                                             reduce_cond, condition);
        for_stmt_ = Function::current()->for_(var_.expression(),
                                              final_condition,
                                              step_.expression());
    }

    auto &set_var_symbol(std::string str) noexcept {
        str = string_replace(str, "&", "_");
        var_.set_symbol(str);
        return *this;
    }

    template<typename Body>
    void operator/(Body &&body) noexcept {
        Function::current()->with(for_stmt_->body(), [&]() noexcept {
            body(var_, Continue{}, Break());
        });
    }
};
}// namespace detail

template<typename... Args, EPort p = port_v<Args...>>
void print(ocarina::string f, Args &&...args) {
    if constexpr (p == D) {
        size_t num = sizeof...(Args);
        OC_ASSERT(num == substr_count(f, "{}"));
        Function::current()->print(f, vector<const Expression *>{OC_EXPR(args)...});
    } else {
        f += "\n";
        cout << format(f.c_str(), OC_FORWARD(args)...);
    }
}

namespace detail {
template<typename T>
[[nodiscard]] auto to_tuple(const T &t) {
    if constexpr (is_vector_expr_v<T>) {
        using elm_ty = decltype(T::x);
        static constexpr auto dim = vector_expr_dimension_v<T>;
        if constexpr (dim == 2) {
            return std::tuple<elm_ty, elm_ty>(t.x, t.y);
        } else if constexpr (dim == 3) {
            return std::tuple<elm_ty, elm_ty, elm_ty>(t.x, t.y, t.z);
        } else {
            return std::tuple<elm_ty, elm_ty, elm_ty, elm_ty>(t.x, t.y, t.z, t.w);
        }
    } else if constexpr (is_scalar_expr_v<T>) {
        return std::tuple<T>(t);
    }
}
}// namespace detail

template<typename... Args>
void prints(ocarina::string f, Args &&...args) {
    auto all_tuple = std::tuple_cat(detail::to_tuple(args)...);
    auto func = [&]<typename... A, size_t... i>(std::tuple<A...> &&tp, std::index_sequence<i...>) {
        print(f, std::get<i>(OC_FORWARD(tp))...);
    };
    func(ocarina::move(all_tuple), std::make_index_sequence<std::tuple_size_v<decltype(all_tuple)>>());
}

namespace detail {
template<typename Count>
requires ocarina::is_all_integral_expr_v<Count>
auto range(Count &&count) noexcept {
    return detail::ForStmtBuilder<expr_value_t<Count>>(0, make_expr(std::forward<Count>(count)), 1);
}

template<typename Begin, typename End>
requires ocarina::is_all_integral_expr_v<Begin, End>
auto range(Begin &&begin, End &&end) noexcept {
    return detail::ForStmtBuilder<expr_value_t<Begin>,
                                  expr_value_t<End>>(make_expr(std::forward<Begin>(begin)),
                                                     make_expr(std::forward<End>(end)),
                                                     1);
}

template<typename Begin, typename End, typename Step>
requires ocarina::is_all_integral_expr_v<Begin, End, Step>
auto range(Begin &&begin, End &&end, Step &&step) noexcept {
    return detail::ForStmtBuilder<expr_value_t<Begin>, expr_value_t<End>,
                                  expr_value_t<Step>>(make_expr(std::forward<Begin>(begin)),
                                                      make_expr(std::forward<End>(end)),
                                                      make_expr(std::forward<Step>(step)));
}

template<typename... Args>
auto range_with_source_location(const string &str, Args &&...args) noexcept {
    comment(str);
    return range(OC_FORWARD(args)...);
}

}// namespace detail

template<typename Count, typename Body>
requires concepts::integral<expr_value_t<Count>>
void for_range(Count &&count, Body &&body) noexcept {
    detail::range(std::forward<Count>(count)) / std::forward<Body>(body);
}

template<typename Begin, typename End, typename Body>
requires ocarina::is_all_integral_expr_v<Begin, End>
void for_range(Begin &&begin, End &&end, Body &&body) noexcept {
    detail::range(std::forward<Begin>(begin),
                  std::forward<End>(end)) /
        std::forward<Body>(body);
}

template<typename Begin, typename End, typename Step, typename Body>
requires ocarina::is_all_integral_expr_v<Begin, End, Step>
void for_range(Begin &&begin, End &&end, Step &&step, Body &&body) noexcept {
    detail::range(std::forward<Begin>(begin),
                  std::forward<End>(end),
                  std::forward<Step>(step)) /
        std::forward<Body>(body);
}

inline void include(string_view fn) noexcept {
    Function::current()->add_header(fn);
}

template<typename... Args>
void configure_block(Args &&...args) noexcept {
    Function::current()->set_block_dim(OC_FORWARD(args)...);
}

template<typename... Args>
void configure_grid(Args &&...args) noexcept {
    Function::current()->set_grid_dim(OC_FORWARD(args)...);
}

template<typename... Args>
void configure(Args &&...args) noexcept {
    Function::current()->configure(OC_FORWARD(args)...);
}

template<typename T>
void return_(T &&ret) noexcept {
    Function::current()->return_(OC_EXPR(ret));
}

inline void return_() noexcept {
    Function::current()->return_(nullptr);
}

}// namespace ocarina