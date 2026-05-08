//
// Created by Zero on 30/04/2022.
//

#pragma once

#include "math/basic_types.h"
#include "core/stl.h"
#include "core/header.h"
#include "core/type.h"
#include "core/type_system/precision_policy.h"
#include "expression.h"
#include "statement.h"
#include "ast_node.h"
#include "op.h"

namespace ocarina {

class Statement;
class ScopeStmt;
class RefExpr;
class CallExpr;
class IfStmt;

class CapturedResource : public Hashable {
private:
    const Type *_type;
    const RefExpr *_expr{nullptr};
    MemoryBlock _block;

private:
    [[nodiscard]] uint64_t compute_hash() const noexcept final {
        return hash64(type()->hash(), _expr->hash());
    }

public:
    CapturedResource(const RefExpr *expr, const Type *type, MemoryBlock block)
        : _type(type), _block(block), _expr(expr) {}
    [[nodiscard]] const Type *type() const noexcept { return _type; }
    [[nodiscard]] const void *handle_ptr() const noexcept {
        return _block.address;
    }
    [[nodiscard]] const MemoryBlock &block() const noexcept {
        return _block;
    }
    [[nodiscard]] size_t block_size() const noexcept { return _block.size; }
    [[nodiscard]] size_t block_alignment() const noexcept { return _block.alignment; }
    [[nodiscard]] const RefExpr *expression() const noexcept { return _expr; }
};

class OC_AST_API Function : public concepts::Noncopyable, public Hashable {
public:
    enum Tag : uint8_t {
        KERNEL,
        CALLABLE,
    };

    struct StructureSet {
        ocarina::map<uint64_t, const Type *> struct_map;
        vector<const Type *> struct_lst;
        void add(const Type *type) noexcept;
    };

    using stack_type = stack<SP<Function>>;

private:
    ocarina::vector<ocarina::unique_ptr<Expression>> all_expressions_;
    ocarina::vector<ocarina::unique_ptr<Statement>> all_statements_;

private:
    mutable string description_{};
    const Type *ret_{nullptr};
    vector<const CallExpr *> all_call_expr_;

    ocarina::map<const Expression *, uint> expr_to_argument_index_;

    ocarina::vector<string_view> headers_;

    /// appended argument for output
    ocarina::vector<Variable> appended_arguments_;

    /// key : expression from other function , value : expression belong current function
    /// use for kernel
    ocarina::map<const Expression *, const RefExpr *> outer_to_local_;

    ocarina::vector<Variable> arguments_;

    /// use for splitting parameter structure start
    ocarina::map<string, Variable> argument_map_;
    ocarina::vector<Variable> splitted_arguments_;
    /// use for splitting parameter structure end

    ocarina::vector<CapturedResource> captured_resources_;
    ocarina::vector<Variable> builtin_vars_;
    ocarina::vector<Variable::Data> variable_datas_;
    ocarina::vector<ScopeStmt *> scope_stack_;
    /// use for assignment subscript access
    ocarina::vector<ocarina::pair<std::byte *, size_t>> temp_memory_;
    ScopeStmt body_{true};
    Tag tag_{Tag::CALLABLE};
    ocarina::vector<SP<const Function>> used_custom_func_;
    StructureSet used_struct_;
    bool allow_dsl_capture_{false};
    mutable bool raytracing_{false};
    mutable uint3 block_dim_{make_uint3(0)};
    mutable uint3 grid_dim_{make_uint3(0)};
    StoragePrecisionPolicy storage_policy_{};

    friend class FunctionCorrector;
    friend class Variable;

private:
    static stack_type &_function_stack() noexcept;
    [[nodiscard]] const Type *resolve_ast_type(const Type *type) const noexcept;
    [[nodiscard]] ocarina::list<CallExpr::Template> resolve_ast_templates(ocarina::list<CallExpr::Template> t_args) const noexcept;

    [[nodiscard]] uint _next_variable_uid() noexcept;
    void mark_variable_usage(uint uid, Usage usage) noexcept;
    template<typename Func>
    static shared_ptr<Function> _define(Function::Tag tag, Func &&func) noexcept {
        shared_ptr<Function> ret = ocarina::make_shared<Function>(tag);
        push(ret);
        ret->with(ret->body(), std::forward<Func>(func));
        pop(ret);
        return ret;
    }

    void correct() noexcept;
    /// used to capture variable from invoker start
    [[nodiscard]] const RefExpr *mapping_captured_argument(const Expression *outer_expr, bool *contain) noexcept;
    [[nodiscard]] const RefExpr *mapping_local_variable(const Expression *invoked_func_expr, bool *contain) noexcept;
    [[nodiscard]] const RefExpr *outer_to_local(const Expression *invoked_func_expr) noexcept;
    [[nodiscard]] const RefExpr *outer_to_argument(const Expression *invoked_func_expr) noexcept;
    [[nodiscard]] const RefExpr *mapping_output_argument(const Expression *invoked_func_expr, bool *contain) noexcept;
    void append_output_argument(const Expression *expression, bool *contain) noexcept;
    /// used to capture variable from invoker end

    void process_param_struct_member(const Variable &arg, const Type *type, vector<int> &path) noexcept;
    void splitting_param_struct(const Variable &arg, const Type *type, vector<int> &path) noexcept;
    void replace_param_struct_member(const vector<int> &path, const Expression *&expression) noexcept;
    void splitting_arguments() noexcept;

    template<typename Expr, typename Tuple, size_t... i>
    [[nodiscard]] auto _create_expression(Tuple &&tuple, std::index_sequence<i...>) {
        auto expr = ocarina::make_unique<Expr>(std::get<i>(OC_FORWARD(tuple))...);
        auto ret = expr.get();
        expr->set_context(this);
        all_expressions_.push_back(std::move(expr));
        return ret;
    }

    template<typename Expr, typename... Args>
    [[nodiscard]] auto create_expression(Args &&...args) {
        using Tuple = std::tuple<std::remove_reference_t<Args>...>;
        Tuple tuple = Tuple{OC_FORWARD(args)...};
        return _create_expression<Expr>(OC_FORWARD(tuple), std::index_sequence_for<Args...>());
    }
    [[nodiscard]] const RefExpr *_ref(const Variable &variable) noexcept;
    [[nodiscard]] const RefExpr *_builtin(Variable::Tag tag, const Type *type) noexcept;
    const Function *add_used_function(SP<const Function> func) noexcept;

    template<typename Stmt, typename Tuple, size_t... i>
    [[nodiscard]] auto _create_statement(Tuple &&tuple, std::index_sequence<i...>) {
        auto stmt = ocarina::make_unique<Stmt>(std::get<i>(OC_FORWARD(tuple))...);
        auto ret = stmt.get();
        stmt->set_context(this);
        all_statements_.push_back(std::move(stmt));
        current_scope()->add_stmt(ret);
        return ret;
    }

    template<typename Stmt, typename... Args>
    auto create_statement(Args &&...args) {
        using Tuple = std::tuple<std::remove_reference_t<Args>...>;
        Tuple tuple = Tuple{OC_FORWARD(args)...};
        return _create_statement<Stmt>(OC_FORWARD(tuple), std::index_sequence_for<Args...>());
    }
    [[nodiscard]] uint64_t compute_hash() const noexcept override;
    bool check_context() const noexcept {
        return body_.check_context(this);
    }

    class ScopeGuard {
    private:
        ocarina::vector<ScopeStmt *> &scope_stack_;

    public:
        ScopeGuard(ocarina::vector<ScopeStmt *> &stack, ScopeStmt *scope)
            : scope_stack_(stack) {
            scope_stack_.push_back(scope);
        }
        ~ScopeGuard() {
            scope_stack_.pop_back();
        }
    };

public:
    static void push(stack_type::value_type f);
    static void pop(stack_type::value_type f);
    void record_call_expression(const CallExpr *call_expr) noexcept;
    [[nodiscard]] const CallExpr *current_call_expr() const noexcept;
    OC_MAKE_MEMBER_GETTER(all_call_expr, &)
    void set_description(string desc) const noexcept { description_ = ocarina::move(desc); }
    [[nodiscard]] string &description() const noexcept { return description_; }
    void set_allow_dsl_capture(bool v) noexcept { allow_dsl_capture_ = v; }
    [[nodiscard]] bool allow_dsl_capture() const noexcept { return allow_dsl_capture_; }
    template<typename Visitor>
    void for_each_custom_func(Visitor &&visitor) const noexcept {
        for (const auto &f : used_custom_func_) {
            visitor(f.get());
        }
    }

    [[nodiscard]] Variable create_variable(const Type *type, Variable::Tag tag,
                                           string name = "", string suffix = "") noexcept;

    template<typename Visitor>
    void for_each_structure(Visitor &&visitor) const noexcept {
        for (const auto &type : used_struct_.struct_lst) {
            visitor(type);
        }
    }

    template<typename Func>
    void for_each_header(Func &&func) const noexcept {
        std::for_each(headers_.begin(), headers_.end(), OC_FORWARD(func));
    }
    void add_used_structure(const Type *type) noexcept;
    [[nodiscard]] const Usage &variable_usage(uint uid) const noexcept;
    [[nodiscard]] Usage &variable_usage(uint uid) noexcept;
    [[nodiscard]] const Variable::Data &variable_data(uint uid) const noexcept;
    [[nodiscard]] Variable::Data &variable_data(uint uid) noexcept;
    const CapturedResource &get_captured_resource(const Type *type, Variable::Tag tag, MemoryBlock block) noexcept;
    const CapturedResource &add_captured_resource(const Type *type, Variable::Tag tag, MemoryBlock block) noexcept;
    [[nodiscard]] bool has_captured_resource(const void *handle) const noexcept;
    [[nodiscard]] auto &captured_resources() const noexcept { return captured_resources_; }
    [[nodiscard]] const CapturedResource *get_captured_resource_by_handle(const void *handle) const noexcept;
    template<typename Visitor>
    void for_each_captured_resource(Visitor &&visitor) const noexcept {
        for (const CapturedResource &var : captured_resources_) {
            visitor(var);
        }
    }
    [[nodiscard]] vector<Variable> all_arguments() const noexcept;
    void update_captured_resources(const Function *func) noexcept;
    [[nodiscard]] static Function *current() noexcept;
    template<typename Func>
    static auto define_callable(Func &&func) noexcept {
        return _define(Tag::CALLABLE, std::forward<Func>(func));
    }
    [[nodiscard]] const ScopeStmt *current_scope() const noexcept;
    [[nodiscard]] ScopeStmt *current_scope() noexcept;
    template<typename Func>
    decltype(auto) with(ScopeStmt *scope, Func &&func) noexcept {
        ScopeGuard guard(scope_stack_, scope);
        return func();
    }
    template<typename Func>
    static shared_ptr<Function> define_kernel(Func &&func) noexcept {
        shared_ptr<Function> function = _define(Tag::KERNEL, std::forward<Func>(func));
        function->correct();
        return function;
    }
    explicit Function(Tag tag) noexcept;
    ~Function() noexcept;

    void correct_used_structures() noexcept;
    template<typename T, typename... Args>
    T *create_temp_obj(Args &&...args) noexcept {
        T *ptr = new_with_allocator<T>(std::forward<Args>(args)...);
        temp_memory_.emplace_back(reinterpret_cast<std::byte *>(ptr), sizeof(T));
        return ptr;
    }
    void set_block_dim(uint x, uint y = 1, uint z = 1) const noexcept { block_dim_ = make_uint3(x, y, z); }
    void set_block_dim(uint2 size) const noexcept { block_dim_ = make_uint3(size, 1); }
    void set_block_dim(uint3 size) const noexcept { block_dim_ = size; }
    [[nodiscard]] uint3 block_dim() const noexcept { return block_dim_; }
    void set_grid_dim(uint x, uint y = 1, uint z = 1) const noexcept { grid_dim_ = make_uint3(x, y, z); }
    void set_grid_dim(uint2 size) const noexcept { grid_dim_ = make_uint3(size, 1); }
    void set_grid_dim(uint3 size) const noexcept { grid_dim_ = size; }
    [[nodiscard]] uint3 grid_dim() const noexcept { return grid_dim_; }
    void configure(uint3 grid_dim, uint3 block_dim) const noexcept {
        grid_dim_ = grid_dim;
        block_dim_ = block_dim;
    }
    [[nodiscard]] bool has_configure() const noexcept { return all(block_dim() != 0u) || all(grid_dim() != 0u); }
    void set_storage_policy(StoragePrecisionPolicy policy) noexcept { storage_policy_ = policy; }
    [[nodiscard]] StoragePrecisionPolicy storage_policy() const noexcept { return storage_policy_; }
    [[nodiscard]] ocarina::string func_name(uint64_t ext_hash = 0u, string ext_name = "") const noexcept;
    void assign(const Expression *lhs, const Expression *rhs) noexcept;
    void return_(const Expression *expression) noexcept;
    [[nodiscard]] const RefExpr *block_idx() noexcept;
    [[nodiscard]] const RefExpr *thread_idx() noexcept;
    [[nodiscard]] const RefExpr *thread_id() noexcept;
    [[nodiscard]] const RefExpr *dispatch_idx() noexcept;
    [[nodiscard]] const RefExpr *dispatch_id() noexcept;
    [[nodiscard]] const RefExpr *dispatch_dim() noexcept;
    [[nodiscard]] const RefExpr *argument(const Type *type) noexcept;
    [[nodiscard]] const RefExpr *reference_argument(const Type *type) noexcept;
    [[nodiscard]] const RefExpr *local(const Type *type, OC_APPEND_SRC_LOCATION) noexcept;
    [[nodiscard]] const LiteralExpr *literal(const Type *type, basic_literal_t value) noexcept;
    [[nodiscard]] const UnaryExpr *unary(const Type *type, UnaryOp op, const Expression *expression) noexcept;
    [[nodiscard]] const BinaryExpr *binary(const Type *type, BinaryOp op, const Expression *lhs, const Expression *rhs) noexcept;
    [[nodiscard]] const ConditionalExpr *conditional(const Type *type, const Expression *pred, const Expression *t, const Expression *f) noexcept;
    [[nodiscard]] const CastExpr *cast(const Type *type, CastOp op, const Expression *expression) noexcept;
    [[nodiscard]] const SubscriptExpr *subscript(const Type *type, const Expression *range, const Expression *index) noexcept;
    [[nodiscard]] const SubscriptExpr *subscript(const Type *type, const Expression *range,
                                                 vector<const Expression *> indexes) noexcept;
    [[nodiscard]] const MemberExpr *swizzle(const Type *type, const Expression *obj, uint16_t mask, uint16_t swizzle_size) noexcept;
    [[nodiscard]] const MemberExpr *member(const Type *type, const Expression *obj, int index) noexcept;
    const CallExpr *call(const Type *type, SP<const Function> func, ocarina::list<const Expression *> args) noexcept;
    const CallExpr *call(const Type *type, string_view func_name, ocarina::list<const Expression *> args) noexcept;
    const CallExpr *call_builtin(const Type *type, CallOp op, ocarina::list<const Expression *> args,
                                 ocarina::list<CallExpr::Template> t_args = {}) noexcept;
    void add_header(string_view fn) noexcept;
    [[nodiscard]] ScopeStmt *scope() noexcept;
    [[nodiscard]] IfStmt *if_(const Expression *expr) noexcept;
    [[nodiscard]] SwitchStmt *switch_(const Expression *expr) noexcept;
    [[nodiscard]] SwitchCaseStmt *switch_case(const Expression *expr) noexcept;
    const ExprStmt *expr_statement(const Expression *expr) noexcept;
    void break_() noexcept;
    [[nodiscard]] SwitchDefaultStmt *switch_default() noexcept;
    [[nodiscard]] LoopStmt *loop() noexcept;
    [[nodiscard]] ForStmt *for_(const Expression *init, const Expression *cond, const Expression *step) noexcept;
    void continue_() noexcept;
    void comment(const ocarina::string &string) noexcept;
    void print(string fmt, const vector<const Expression *> &args) noexcept;
    [[nodiscard]] const ScopeStmt *body() const noexcept;
    [[nodiscard]] ScopeStmt *body() noexcept;
    [[nodiscard]] ocarina::span<const Variable> arguments() const noexcept;
    [[nodiscard]] ocarina::span<const Variable> appended_arguments() const noexcept;
    [[nodiscard]] ocarina::span<const Variable> builtin_vars() const noexcept;
    [[nodiscard]] constexpr Tag tag() const noexcept { return tag_; }
    [[nodiscard]] constexpr bool is_callable() const noexcept { return tag_ == Tag::CALLABLE; }
    [[nodiscard]] constexpr bool is_kernel() const noexcept { return tag_ == Tag::KERNEL; }
    [[nodiscard]] constexpr bool is_raytracing_kernel() const noexcept { return is_raytracing() && is_kernel(); }
    [[nodiscard]] constexpr bool is_general_kernel() const noexcept { return !is_raytracing() && is_kernel(); }
    [[nodiscard]] constexpr bool is_raytracing() const noexcept { return raytracing_; }
    [[nodiscard]] constexpr const Type *return_type() const noexcept { return ret_; }
    constexpr void set_raytracing(bool val) const noexcept { raytracing_ = val; }
};
}// namespace ocarina