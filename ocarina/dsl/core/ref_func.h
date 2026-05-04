//
// Created by Zero on 2024/9/24.
//

#pragma once

#include "core/stl.h"
#include "core/concepts.h"
#include "math/basic_traits.h"
#include "type_trait.h"
#include "ast/function.h"
#include <utility>
#include "core/runtime/platform.h"

namespace ocarina {

namespace detail {
template<typename Lhs, typename Rhs>
requires(!is_param_struct_v<expr_value_t<Lhs>> && !is_param_struct_v<expr_value_t<Rhs>>)
void assign(Lhs &&lhs, Rhs &&rhs) noexcept;// implement in stmt_builder.h

[[nodiscard]] OC_DSL_API Var<uint> correct_index(Var<uint> index, Var<uint> size, const string &desc,
                                                 const string &tb) noexcept;// implement in env.cpp

[[nodiscard]] OC_DSL_API Var<uint> correct_index(Var<uint> index, uint size, const string &desc,
                                                 const string &tb) noexcept;// implement in env.cpp

[[nodiscard]] OC_DSL_API Var<uint> divide(Var<uint> lhs, Var<uint> rhs) noexcept;// implement in env.cpp

}// namespace detail

template<typename T>
[[nodiscard]] inline Var<expr_value_t<T>> eval(T &&x) noexcept;// implement in stmt_builder.h

template<typename T>
[[nodiscard]] inline Var<expr_value_t<T>> eval(const Expression *expr) noexcept;// implement in stmt_builder.h

template<typename T>
[[nodiscard]] inline Expr<expr_value_t<T>> make_expr(T &&x) noexcept;// implement in stmt_builder.h

template<typename T>
[[nodiscard]] inline Expr<expr_value_t<T>> make_expr(const Expression *expr) noexcept;// implement in stmt_builder.h

class TriangleHit;
class Ray;

template<typename T, typename TBuffer>
struct SOAViewVar;

template<typename T, typename TBuffer>
struct AOSViewVar;

namespace detail {

template<typename T>
[[nodiscard]] decltype(auto) extract_expression(T &&v) noexcept;// implement in dsl/expr.h

#define OC_EXPR(arg) ocarina::detail::extract_expression(OC_FORWARD(arg))

template<typename T,
         typename element_type = std::remove_cvref_t<expr_value_t<decltype(std::declval<expr_value_t<T>>()[0])>>>
struct EnableSubscriptAccess {

    [[nodiscard]] T *self() noexcept { return static_cast<T *>(this); }
    [[nodiscard]] const T *self() const noexcept { return static_cast<const T *>(this); }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    auto operator[](Index &&index) const noexcept {
        auto f = self()->expression()->context();
        const SubscriptExpr *expr = f->subscript(Type::of<element_type>(),
                                                 self()->expression(),
                                                 OC_EXPR(index));
        expr->mark(Usage::READ);
        return eval<element_type>(expr);
    }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    auto at(Index &&index) const noexcept {
        auto f = self()->expression()->context();
        const SubscriptExpr *expr = f->subscript(Type::of<element_type>(),
                                                 self()->expression(),
                                                 OC_EXPR(index));
        expr->mark(Usage::READ);
        return eval<element_type>(expr);
    }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    auto &operator[](Index &&index) noexcept {
        auto f = self()->expression()->context();
        const SubscriptExpr *expr = f->subscript(Type::of<element_type>(),
                                                 self()->expression(),
                                                 OC_EXPR(index));
        expr->mark(Usage::WRITE);
        Var<element_type> *ret = f->template create_temp_obj<Var<element_type>>(expr);
        return *ret;
    }

    template<typename Index>
    requires concepts::integral<expr_value_t<Index>>
    auto &at(Index &&index) noexcept {
        auto f = self()->expression()->context();
        const SubscriptExpr *expr = f->subscript(Type::of<element_type>(),
                                                 self()->expression(),
                                                 OC_EXPR(index));
        expr->mark(Usage::WRITE);
        Var<element_type> *ret = f->template create_temp_obj<Var<element_type>>(expr);
        return *ret;
    }
};

template<typename T>
struct EnableReadAndWrite {

    [[nodiscard]] T *self() noexcept { return static_cast<T *>(this); }
    [[nodiscard]] const T *self() const noexcept { return static_cast<const T *>(this); }

    using element_type = std::remove_cvref_t<decltype(std::declval<expr_value_t<T>>()[0])>;
    template<typename... Index>
    requires concepts::all_integral<expr_value_t<Index>...>
    auto read_multi(Index &&...index) const noexcept {
        const SubscriptExpr *expr = Function::current()->subscript(Type::of<element_type>(),
                                                                   self()->expression(),
                                                                   {OC_EXPR(index)...});
        expr->mark(Usage::READ);
        return eval<element_type>(expr);
    }

    template<typename Index>
    requires concepts::all_integral<expr_value_t<Index>>
    auto read(Index &&index, bool check_boundary = true) const noexcept {
        Var<expr_value_t<Index>> new_index = OC_FORWARD(index);
        if (check_boundary) {
            Var<expr_value_t<Index>> size = static_cast<const T *>(this)->template size<expr_value_t<Index>>();
            new_index = correct_index(new_index, size, typeid(T).name(), traceback_string(1));
        }
        auto f = self()->expression()->context();
        const SubscriptExpr *expr = f->subscript(Type::of<element_type>(),
                                                 self()->expression(),
                                                 {OC_EXPR(new_index)});
        expr->mark(Usage::READ);
        return eval<element_type>(expr);
    }

    template<typename Index, typename Val>
    requires concepts::integral<expr_value_t<Index>> && concepts::static_convertible<element_type, remove_device_t<Val>>
    void write(Index &&index, Val &&elm, bool check_boundary = true) {
        if constexpr (is_swizzle_v<std::remove_cvref_t<Val>>) {
            write(OC_FORWARD(index), elm.decay(), check_boundary);
        } else {
            Var<expr_value_t<Index>> new_index = OC_FORWARD(index);
            if (check_boundary) {
                Var<expr_value_t<Index>> size = static_cast<const T *>(this)->template size<expr_value_t<Index>>();
                new_index = correct_index(new_index, size, typeid(T).name(), traceback_string(1));
            }
            auto f = self()->expression()->context();
            const SubscriptExpr *expr = f->subscript(Type::of<element_type>(),
                                                     self()->expression(),
                                                     OC_EXPR(new_index));
            expr->mark(Usage::WRITE);
            assign(expr, OC_FORWARD(elm));
        }
    }

    template<typename Index, typename Val>
    requires is_integral_expr_v<Index> &&
             ocarina::is_same_v<element_type, expr_value_t<Val>>
    void write_and_check(Index &&index, Val &&elm, uint size, const string &desc) {
        if constexpr (is_integral_v<Index>) {
            OC_ASSERT(index <= size);
            write(OC_FORWARD(index), OC_FORWARD(elm));
        } else {
            index = correct_index(OC_FORWARD(index), size, desc, traceback_string(1));
            write(OC_FORWARD(index), OC_FORWARD(elm));
        }
    }
};

template<typename T>
struct EnableByteLoadAndStore {

    [[nodiscard]] T *self() noexcept { return static_cast<T *>(this); }
    [[nodiscard]] const T *self() const noexcept { return static_cast<const T *>(this); }

    template<uint N = 1, typename Elm = uint, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] auto load(Offset &&offset, bool check_boundary = true) const noexcept {
        Var<expr_value_t<Offset>> new_offset = OC_FORWARD(offset);
        if (check_boundary) {
            new_offset = detail::correct_index(new_offset, self()->template size<expr_value_t<Offset>>(),
                                               typeid(*this).name(), traceback_string());
        }
        self()->expression()->mark(Usage::READ);
        if constexpr (N == 1) {
            const CallExpr *expr = Function::current()->call_builtin(Type::of<Elm>(),
                                                                     CallOp::BYTE_BUFFER_READ,
                                                                     {self()->expression(),
                                                                      OC_EXPR(new_offset)},
                                                                     {N});
            return eval<Elm>(expr);
        } else {
            const CallExpr *expr = Function::current()->call_builtin(Type::of<Vector<Elm, N>>(),
                                                                     CallOp::BYTE_BUFFER_READ,
                                                                     {self()->expression(),
                                                                      OC_EXPR(new_offset)},
                                                                     {N});
            return eval<Vector<Elm, N>>(expr);
        }
    }

    template<typename Target, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] Var<Target> load_as(Offset &&offset, bool check_boundary = true) const noexcept {
        Var<expr_value_t<Offset>> new_offset = OC_FORWARD(offset);
        if (check_boundary) {
            new_offset = detail::correct_index(new_offset, self()->template size<expr_value_t<Offset>>(),
                                               typeid(*this).name(), traceback_string());
        }
        const Type *ret_type = Type::of<Target>();
        self()->expression()->mark(Usage::READ);
        const CallExpr *expr = Function::current()->call_builtin(ret_type,
                                                                 CallOp::BYTE_BUFFER_READ,
                                                                 {self()->expression(),
                                                                  OC_EXPR(new_offset)},
                                                                 {ret_type});
        return eval<Target>(expr);
    }

    template<typename Target, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] Var<Target> &load_as(Offset &&offset, bool check_boundary = true) noexcept {
        Var<expr_value_t<Offset>> new_offset = OC_FORWARD(offset);
        if (check_boundary) {
            new_offset = detail::correct_index(new_offset, self()->template size<expr_value_t<Offset>>(),
                                               typeid(*this).name(), traceback_string());
        }
        const Type *ret_type = Type::of<Target>();
        self()->expression()->mark(Usage::WRITE);
        const CallExpr *expr = Function::current()->call_builtin(ret_type,
                                                                 CallOp::BYTE_BUFFER_READ,
                                                                 {self()->expression(),
                                                                  OC_EXPR(new_offset)},
                                                                 {ret_type});
        Var<Target> *ret = Function::current()->template create_temp_obj<Var<Target>>(expr);
        return *ret;
    }

    template<typename Elm = uint, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] auto load2(Offset &&offset, bool check_boundary = true) const noexcept {
        return load<2, Elm>(OC_FORWARD(offset), check_boundary);
    }

    template<typename Elm = uint, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] auto load3(Offset &&offset, bool check_boundary = true) const noexcept {
        return load<3, Elm>(OC_FORWARD(offset), check_boundary);
    }

    template<typename Elm = uint, typename Offset>
    requires is_integral_expr_v<Offset>
    [[nodiscard]] auto load4(Offset &&offset, bool check_boundary = true) const noexcept {
        return load<4, Elm>(OC_FORWARD(offset), check_boundary);
    }

    template<typename Elm, typename Offset>
    requires is_integral_expr_v<Offset>
    void store(Offset &&offset, const Elm &val, bool check_boundary = true) noexcept {
        Var<expr_value_t<Offset>> new_offset = OC_FORWARD(offset);
        if (check_boundary) {
            new_offset = detail::correct_index(new_offset, self()->template size<expr_value_t<Offset>>(),
                                               typeid(*this).name(), traceback_string());
        }
        const CallExpr *expr = Function::current()->call_builtin(nullptr, CallOp::BYTE_BUFFER_WRITE,
                                                                 {self()->expression(),
                                                                  OC_EXPR(new_offset), OC_EXPR(val)});
        self()->expression()->mark(Usage::WRITE);
        Function::current()->expr_statement(expr);
    }
};

template<typename T, size_t Dim>
struct EnableTextureSample {
    static_assert(Dim == 2 || Dim == 3, "The dimension of texture must be 2 or 3!");
    static constexpr CallOp call_op = Dim == 2 ? CallOp::TEX2D_SAMPLE : CallOp::TEX3D_SAMPLE;
    template<typename U, typename V>
    requires(is_all_floating_point_expr_v<U, V>)
    OC_NODISCARD DynamicArray<float> sample(uint channel_num, const U &u, const V &v)
        const noexcept;// implement in dsl/dynamic_array.h

    template<typename U, typename V, typename W>
    requires(is_all_floating_point_expr_v<U, V, W>)
    OC_NODISCARD DynamicArray<float> sample(uint channel_num, const U &u, const V &v, const W &w)
        const noexcept;// implement in dsl/dynamic_array.h

    template<typename UVW>
    requires(is_general_float_vector3_v<remove_device_t<UVW>>)
    OC_NODISCARD DynamicArray<float> sample(uint channel_num, const UVW &uvw)
        const noexcept;// implement in dsl/dynamic_array.h

    template<typename UV>
    requires(is_general_float_vector2_v<remove_device_t<UV>>)
    OC_NODISCARD DynamicArray<float> sample(uint channel_num, const UV &uv)
        const noexcept;// implement in dsl/dynamic_array.h
};

template<typename T, size_t Dim>
struct EnableTextureReadAndWrite {
    static_assert(Dim == 2 || Dim == 3, "The dimension of texture must be 2 or 3!");
    static constexpr CallOp Read = Dim == 2 ? CallOp::TEX2D_READ : CallOp::TEX3D_READ;
    static constexpr CallOp Write = Dim == 2 ? CallOp::TEX2D_WRITE : CallOp::TEX3D_WRITE;
    [[nodiscard]] T *self() noexcept { return static_cast<T *>(this); }
    [[nodiscard]] const T *self() const noexcept { return static_cast<const T *>(this); }

    template<typename Output, typename X, typename Y>
    requires(is_all_integral_expr_v<X, Y>)
    OC_NODISCARD auto read(const X &x, const Y &y) const noexcept {
        const CallExpr *expr = Function::current()->call_builtin(Type::of<Output>(), Read,
                                                                 {self()->expression(), OC_EXPR(x), OC_EXPR(y)},
                                                                 {Type::of<Output>()});
        self()->expression()->mark(Usage::READ);
        return eval<Output>(expr);
    }

    template<typename Output, typename X, typename Y, typename Z>
    requires(is_all_integral_expr_v<X, Y, Z>)
    OC_NODISCARD auto read(const X &x, const Y &y, const Z &z) const noexcept {
        static_assert(Dim == 3,"This method only available on 3D tex");
        const CallExpr *expr = Function::current()->call_builtin(Type::of<Output>(), Read,
                                                                 {self()->expression(), OC_EXPR(x), OC_EXPR(y), OC_EXPR(z)},
                                                                 {Type::of<Output>()});
        self()->expression()->mark(Usage::READ);
        return eval<Output>(expr);
    }

    template<typename Target, typename XY>
    requires((is_general_integer_vector2_v<remove_device_t<XY>>) &&
             (is_uchar_element_expr_v<Target> || is_float_element_expr_v<Target>))
    OC_NODISCARD auto read(const XY &xy) const noexcept {
        return [&]<typename Arg>(const Arg &arg) {
            return read<Target>(arg.x, arg.y);
        }(decay_swizzle(xy));
    }

    template<typename Target, typename XYZ>
    requires((is_general_int_vector3_v<remove_device_t<XYZ>> || is_general_uint_vector3_v<remove_device_t<XYZ>>) &&
             (is_uchar_element_expr_v<Target> || is_float_element_expr_v<Target>))
    OC_NODISCARD auto read(const XYZ &xyz) const noexcept {
        return [&]<typename Arg>(const Arg &arg) {
            return read<Target>(arg.x, arg.y, arg.z);
        }(decay_swizzle(xyz));
    }

    template<typename X, typename Y, typename Val>
    requires(is_all_integral_expr_v<X, Y> &&
             (is_uchar_element_expr_v<Val> ||
              is_float_element_expr_v<Val>))
    void write(const Val &elm, const X &x, const Y &y) noexcept {
        const T *texture = static_cast<const T *>(this);
        const CallExpr *expr = Function::current()->call_builtin(nullptr, Write,
                                                                 {self()->expression(),
                                                                  OC_EXPR(elm), OC_EXPR(x), OC_EXPR(y)});
        self()->expression()->mark(Usage::WRITE);
        Function::current()->expr_statement(expr);
    }

    template<typename X, typename Y, typename Z, typename Val>
    requires(is_all_integral_expr_v<X, Y, Z> &&
             (is_uchar_element_expr_v<Val> ||
              is_float_element_expr_v<Val>))
    void write(const Val &elm, const X &x, const Y &y, const Z &z) noexcept {
        static_assert(Dim == 3,"This method only available on 3D tex");
        const CallExpr *expr = Function::current()->call_builtin(nullptr, Write,
                                                                 {self()->expression(),
                                                                  OC_EXPR(elm), OC_EXPR(x), OC_EXPR(y), OC_EXPR(z)});
        self()->expression()->mark(Usage::WRITE);
        Function::current()->expr_statement(expr);
    }

    template<typename XYZ, typename Val>
    requires(is_general_integer_vector3_v<remove_device_t<XYZ>>)
    void write(const Val &elm, const XYZ &xyz) noexcept {
        [&]<typename Arg>(const Arg &arg) {
            write(elm, arg.x, arg.y, arg.z);
        }(decay_swizzle(xyz));
    }

    template<typename XY, typename Val>
    requires(is_general_integer_vector2_v<remove_device_t<XY>>)
    void write(const Val &elm, const XY &xy) noexcept {
        [&]<typename Arg>(const Arg &arg) {
            write(elm, arg.x, arg.y);
        }(decay_swizzle(xy));
    }
};

template<typename T>
struct AtomicRef {
private:
    const Expression *range_{};
    const Expression *index_{};

public:
    explicit AtomicRef(const Expression *range,
                       const Expression *index)
        : range_(range), index_(index) {}
    AtomicRef(AtomicRef &&) noexcept = delete;
    AtomicRef(const AtomicRef &) noexcept = delete;
    AtomicRef &operator=(AtomicRef &&) noexcept = delete;
    AtomicRef &operator=(const AtomicRef &) noexcept = delete;

    Var<T> exchange(Var<T> value) noexcept {
        const Expression *expr = Function::current()->call_builtin(Type::of<T>(),
                                                                   CallOp::ATOMIC_EXCH,
                                                                   {range_, index_, OC_EXPR(value)});
        range_->mark(Usage::READ_WRITE);
        return eval<T>(expr);
    }

    Var<T> fetch_add(Var<T> value) noexcept {
        const Expression *expr = Function::current()->call_builtin(Type::of<T>(),
                                                                   CallOp::ATOMIC_ADD,
                                                                   {range_, index_, OC_EXPR(value)});
        range_->mark(Usage::READ_WRITE);
        return eval<T>(expr);
    }

    Var<T> fetch_sub(Var<T> value) noexcept {
        const Expression *expr = Function::current()->call_builtin(Type::of<T>(),
                                                                   CallOp::ATOMIC_SUB,
                                                                   {range_, index_, OC_EXPR(value)});
        range_->mark(Usage::READ_WRITE);
        return eval<T>(expr);
    }
};

template<typename TBuffer, typename Elm>
struct BufferAsAtomicAddress {
    template<typename Target = Elm, typename Index>
    requires is_integral_expr_v<Index>
    [[nodiscard]] AtomicRef<Target> atomic(Index &&index) noexcept {
        static_assert(is_scalar_expr_v<Elm>);
        return AtomicRef<Target>(static_cast<TBuffer *>(this)->expression(), OC_EXPR(index));
    }
};

template<typename T>
struct EnableGetMemberByIndex {

    [[nodiscard]] T *self() noexcept { return static_cast<T *>(this); }
    [[nodiscard]] const T *self() const noexcept { return static_cast<const T *>(this); }

    using element_type = std::remove_cvref_t<decltype(std::declval<expr_value_t<T>>()[0])>;
    template<int i>
    [[nodiscard]] auto get() const noexcept {
        return eval<element_type>(Function::current()->subscript(Type::of<element_type>(),
                                                                 OC_EXPR(*self()),
                                                                 OC_EXPR(i)));
    }
    template<int i>
    auto &get() noexcept {
        auto f = Function::current();
        const SubscriptExpr *expr = f->subscript(Type::of<element_type>(),
                                                 OC_EXPR(*self()),
                                                 OC_EXPR(int(i)));
        Var<element_type> *ret = f->template create_temp_obj<Var<element_type>>(expr);
        return *ret;
    }
};

template<typename T>
struct EnableStaticCast {
    template<class Dest>
    requires concepts::static_convertible<Dest, expr_value_t<T>>
    OC_NODISCARD Var<Dest>
    cast()
        const noexcept {
        const CastExpr *expr = Function::current()->cast(Type::of<Dest>(), CastOp::STATIC,
                                                         static_cast<const T *>(this)->expression());
        return eval<Dest>(expr);
    }
};

template<typename T>
struct EnableBitwiseCast {
    template<class Dest>
    requires concepts::bitwise_convertible<Dest, expr_value_t<T>>
    OC_NODISCARD Var<Dest>
    as()
        const noexcept {
        const CastExpr *expr = Function::current()->cast(Type::of<Dest>(), CastOp::BITWISE,
                                                         static_cast<const T *>(this)->expression());
        return eval<Dest>(expr);
    }
};
}// namespace detail
}// namespace ocarina