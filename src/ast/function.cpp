//
// Created by Zero on 30/04/2022.
//

#include "function.h"
#include "core/type_system/type_desc.h"
#include "core/util/util.h"
#include "generator/source_emitter.h"
#include "function_corrector.h"

namespace ocarina {

void Function::StructureSet::add(const ocarina::Type *type) noexcept {
    for (const Type *m : type->members()) {
        add(m);
    }
    if (struct_map.contains(type->hash()) ||
        !type->is_structure() ||
        type->is_param_struct()) {
        return;
    }
    struct_map.insert(make_pair(type->hash(), type));
    struct_lst.push_back(type);
}

Function::stack_type &Function::_function_stack() noexcept {
    static stack_type ret;
    return ret;
}

const Type *Function::resolve_ast_type(const Type *type) const noexcept {
    if (type == nullptr) {
        return nullptr;
    }
    return Type::resolve(type, storage_policy_);
}

ocarina::list<CallExpr::Template> Function::resolve_ast_templates(ocarina::list<CallExpr::Template> t_args) const noexcept {
    for (auto &t_arg : t_args) {
        if (std::holds_alternative<const Type *>(t_arg)) {
            t_arg = resolve_ast_type(std::get<const Type *>(t_arg));
        }
    }
    return t_args;
}

void Function::correct() noexcept {
    TIMER(FunctionCorrect)
    FunctionCorrector().apply(this);
}

void Function::mark_variable_usage(ocarina::uint uid, ocarina::Usage usage) noexcept {
    OC_ASSERT(uid < variable_datas_.size());
    auto old_usage = to_underlying(variable_datas_[uid].usage);
    auto new_usage = to_underlying(usage);
    auto final_usage = old_usage | new_usage;
    if (final_usage != old_usage) {
        variable_datas_[uid].usage = static_cast<Usage>(final_usage);
    }
}

const RefExpr *Function::mapping_captured_argument(const Expression *outer_expr, bool *contain) noexcept {
    OC_ERROR_IF(!allow_dsl_capture_,
                "Callable '{}' must not capture any variables. "
                "If you need captures, use Lambda instead.",
                description_);
    *contain = expr_to_argument_index_.contains(outer_expr);
    if (!*contain) {
        uint arg_index = static_cast<uint>(appended_arguments_.size());
        expr_to_argument_index_.insert(make_pair(outer_expr, arg_index));
        Variable variable = create_variable(outer_expr->type(), Variable::Tag::REFERENCE, "", "outer");
        appended_arguments_.push_back(variable);
    }
    uint arg_index = expr_to_argument_index_.at(outer_expr);
    const RefExpr *ret = _ref(appended_arguments_.at(arg_index));
    return ret;
}

const RefExpr *Function::mapping_local_variable(const Expression *invoked_func_expr, bool *contain) noexcept {
    if (contain) {
        *contain = outer_to_local_.contains(invoked_func_expr);
    }
    if (!outer_to_local_.contains(invoked_func_expr)) {
        const RefExpr *ref_expr = local(invoked_func_expr->type());
        outer_to_local_.insert(make_pair(invoked_func_expr, ref_expr));
    }
    const RefExpr *ret = outer_to_local_.at(invoked_func_expr);
    return ret;
}

const RefExpr *Function::outer_to_local(const Expression *invoked_func_expr) noexcept {
    if (!outer_to_local_.contains(invoked_func_expr)) {
        return nullptr;
    }
    const RefExpr *ret = outer_to_local_.at(invoked_func_expr);
    return ret;
}

const RefExpr *Function::outer_to_argument(const Expression *invoked_func_expr) noexcept {
    OC_ASSERT(expr_to_argument_index_.contains(invoked_func_expr));
    uint arg_index = expr_to_argument_index_.at(invoked_func_expr);
    const RefExpr *ret = _ref(appended_arguments_.at(arg_index));
    return ret;
}

const RefExpr *Function::mapping_output_argument(const Expression *invoked_func_expr, bool *contain) noexcept {
    if (contain) {
        *contain = expr_to_argument_index_.contains(invoked_func_expr);
    }
    if (!expr_to_argument_index_.contains(invoked_func_expr)) {
        uint arg_index = static_cast<uint>(appended_arguments_.size());
        Variable variable = create_variable(invoked_func_expr->type(), Variable::Tag::REFERENCE, "", "pass");
        expr_to_argument_index_.insert(make_pair(invoked_func_expr, arg_index));
        appended_arguments_.push_back(variable);
    }
    return outer_to_argument(invoked_func_expr);
}

void Function::append_output_argument(const Expression *expression, bool *contain) noexcept {
    if (contain) {
        *contain = expr_to_argument_index_.contains(expression);
    }
    if (expr_to_argument_index_.contains(expression)) {
        return;
    }
    expr_to_argument_index_.insert(make_pair(expression, static_cast<uint>(appended_arguments_.size())));
    Variable variable = create_variable(expression->type(), Variable::Tag::REFERENCE, "", "output");
    appended_arguments_.push_back(variable);
    const RefExpr *ref_expr = _ref(variable);
    OC_ERROR_IF(expression->type()->is_resource());
    with(body(), [&] {
        assign(ref_expr, expression);
    });
}

namespace detail {

[[nodiscard]] string path_key(const vector<int> &path) noexcept {
    string ret = ocarina::format("{}", path[0]);
    for (int i = 1; i < path.size(); ++i) {
        ret += "_" + to_string(path[i]);
    }
    return ret;
}
}// namespace detail

void Function::replace_param_struct_member(const vector<int> &path, const Expression *&expression) noexcept {
    string key = detail::path_key(path);
    if (argument_map_.contains(key)) {
        const RefExpr *ref_expr = _ref(argument_map_[key]);
        ref_expr->mark(expression->usage());
        expression = ref_expr;
    }
}

void Function::process_param_struct_member(const Variable &arg, const Type *type,
                                           vector<int> &path) noexcept {
    if (type->is_param_struct()) {
        splitting_param_struct(arg, type, path);
    } else {
        string key = detail::path_key(path);
        Variable v = create_variable(type, Variable::Tag::LOCAL, "", key);
        argument_map_.insert(make_pair(key, v));
        splitted_arguments_.push_back(v);
    }
}

void Function::splitting_param_struct(const ocarina::Variable &arg, const Type *type,
                                      vector<int> &path) noexcept {
    for (int i = 0; i < type->members().size(); ++i) {
        path.push_back(i);
        process_param_struct_member(arg, type->members()[i], path);
        path.pop_back();
    }
}

void Function::splitting_arguments() noexcept {
    for (const Variable &arg : arguments_) {
        if (arg.type()->is_param_struct()) {
            vector<int> path;
            path.push_back(arg.uid());
            splitting_param_struct(arg, arg.type(), path);
        } else {
            splitted_arguments_.push_back(arg);
        }
    }
    std::swap(arguments_, splitted_arguments_);
}

Function::~Function() noexcept {
    for (auto &mem : temp_memory_) {
        delete_with_allocator(mem.first);
    }
}

void Function::correct_used_structures() noexcept {
    for (const Variable &arg : arguments_) {
        add_used_structure(arg.type());
    }
    for (const Variable &arg : appended_arguments_) {
        add_used_structure(arg.type());
    }
    for (const CapturedResource &resource : captured_resources_) {
        add_used_structure(resource.type());
    }
}

void Function::push(stack_type::value_type f) {
    _function_stack().push(f);
}

void Function::pop(stack_type::value_type f) {
    _function_stack().pop();
}

void Function::record_call_expression(const CallExpr *call_expr) noexcept {
    all_call_expr_.push_back(call_expr);
}

const CallExpr *Function::current_call_expr() const noexcept {
    return all_call_expr_.back();
}

const RefExpr *Function::_ref(const ocarina::Variable &variable) noexcept {
    return create_expression<RefExpr>(variable);
}

uint Function::_next_variable_uid() noexcept {
    auto ret = variable_datas_.size();
    variable_datas_.push_back(Variable::Data(Usage::NONE));
    return ret;
}

Variable Function::create_variable(const Type *type, Variable::Tag tag, std::string name, std::string suffix) noexcept {
    Variable ret{this, resolve_ast_type(type), tag, _next_variable_uid(), ocarina::move(name), ocarina::move(suffix)};
    return ret;
}

void Function::add_used_structure(const ocarina::Type *type) noexcept {
    used_struct_.add(type);
}

const Usage &Function::variable_usage(uint uid) const noexcept {
    OC_ASSERT(uid < variable_datas_.size());
    return variable_datas_[uid].usage;
}

Usage &Function::variable_usage(uint uid) noexcept {
    OC_ASSERT(uid < variable_datas_.size());
    return variable_datas_[uid].usage;
}

Variable::Data &Function::variable_data(ocarina::uint uid) noexcept {
    OC_ASSERT(uid < variable_datas_.size());
    return variable_datas_[uid];
}

const Variable::Data &Function::variable_data(ocarina::uint uid) const noexcept {
    OC_ASSERT(uid < variable_datas_.size());
    return variable_datas_[uid];
}

void Function::return_(const Expression *expression) noexcept {
    if (expression) {
        ret_ = resolve_ast_type(expression->type());
    }
    create_statement<ReturnStmt>(expression);
}

Function::Function(Function::Tag tag) noexcept
    : tag_(tag),
      storage_policy_(global_storage_policy()) {}

const ScopeStmt *Function::body() const noexcept {
    return &body_;
}

const ScopeStmt *Function::current_scope() const noexcept {
    return scope_stack_.empty() ? body() : scope_stack_.back();
}

ScopeStmt *Function::current_scope() noexcept {
    return scope_stack_.empty() ? body() : scope_stack_.back();
}

ScopeStmt *Function::body() noexcept {
    return &body_;
}

const RefExpr *Function::_builtin(Variable::Tag tag, const Type *type) noexcept {
    Variable variable = create_variable(type, tag);
    if (auto iter = std::find_if(builtin_vars_.begin(),
                                 builtin_vars_.end(),
                                 [&](auto v) {
                                     return v.tag() == tag;
                                 });
        iter != builtin_vars_.end()) {
        return _ref(*iter);
    }
    builtin_vars_.push_back(variable);
    return _ref(variable);
}

const Function *Function::add_used_function(SP<const Function> func) noexcept {
    used_custom_func_.push_back(func);
    return func.get();
}

const RefExpr *Function::block_idx() noexcept {
    return _builtin(Variable::Tag::BLOCK_IDX, Type::of<uint3>());
}

const RefExpr *Function::thread_idx() noexcept {
    return _builtin(Variable::Tag::THREAD_IDX, Type::of<uint3>());
}

const RefExpr *Function::dispatch_dim() noexcept {
    return _builtin(Variable::Tag::DISPATCH_DIM, Type::of<uint3>());
}

const RefExpr *Function::dispatch_id() noexcept {
    return _builtin(Variable::Tag::DISPATCH_ID, Type::of<uint>());
}

const RefExpr *Function::thread_id() noexcept {
    return _builtin(Variable::Tag::THREAD_ID, Type::of<uint>());
}

const RefExpr *Function::dispatch_idx() noexcept {
    return _builtin(Variable::Tag::DISPATCH_IDX, Type::of<uint3>());
}

const RefExpr *Function::argument(const Type *type) noexcept {
    Variable::Tag tag;
    switch (type->tag()) {
        case Type::Tag::BUFFER:
            tag = Variable::Tag::BUFFER;
            break;
        case Type::Tag::BYTE_BUFFER:
            tag = Variable::Tag::BYTE_BUFFER;
            break;
        case Type::Tag::TEXTURE3D:
            tag = Variable::Tag::TEXTURE3D;
            break;
        case Type::Tag::TEXTURE2D:
            tag = Variable::Tag::TEXTURE2D;
            break;
        case Type::Tag::ACCEL:
            tag = Variable::Tag::ACCEL;
            break;
        default:
            tag = Variable::Tag::LOCAL;
            break;
    }
    Variable variable = create_variable(type, tag);
    arguments_.push_back(variable);
    return _ref(variable);
}

const RefExpr *Function::reference_argument(const Type *type) noexcept {
    Variable variable = create_variable(type, Variable::Tag::REFERENCE, "", "ref");
    arguments_.push_back(variable);
    return _ref(variable);
}

const RefExpr *Function::local(const Type *type, std::source_location location) noexcept {
    auto ret = create_expression<RefExpr>(create_variable(type, Variable::Tag::LOCAL));
    ret->variable().set_src_location(location);
    current_scope()->add_var(ret->variable());
    return ret;
}

const LiteralExpr *Function::literal(const Type *type, LiteralExpr::value_type value) noexcept {
    return create_expression<LiteralExpr>(resolve_ast_type(type), value);
}

const UnaryExpr *Function::unary(const Type *type, UnaryOp op,
                                 const Expression *expression) noexcept {
    return create_expression<UnaryExpr>(resolve_ast_type(type), op, expression);
}

const BinaryExpr *Function::binary(const Type *type, BinaryOp op, const Expression *lhs,
                                   const Expression *rhs) noexcept {
    return create_expression<BinaryExpr>(resolve_ast_type(type), op, lhs, rhs);
}

const ConditionalExpr *Function::conditional(const Type *type, const Expression *pred,
                                             const Expression *t,
                                             const Expression *f) noexcept {
    return create_expression<ConditionalExpr>(resolve_ast_type(type), pred, t, f);
}

const CastExpr *Function::cast(const Type *type, CastOp op, const Expression *expression) noexcept {
    return create_expression<CastExpr>(resolve_ast_type(type), op, expression);
}

const SubscriptExpr *Function::subscript(const Type *type, const Expression *range,
                                         const Expression *index) noexcept {
    return create_expression<SubscriptExpr>(resolve_ast_type(type), range, index);
}

const SubscriptExpr *Function::subscript(const Type *type, const Expression *range,
                                         vector<const Expression *> indexes) noexcept {
    return create_expression<SubscriptExpr>(resolve_ast_type(type), range, indexes);
}

const MemberExpr *Function::swizzle(const Type *type, const Expression *obj, uint16_t mask,
                                    uint16_t swizzle_size) noexcept {
    const Type *resolved = resolve_ast_type(type);
    return create_expression<MemberExpr>(resolved, obj, mask, swizzle_size, create_variable(resolved, Variable::Tag::MEMBER));
}

const MemberExpr *Function::member(const Type *type, const Expression *obj, int index) noexcept {
    const Type *resolved = resolve_ast_type(type);
    return create_expression<MemberExpr>(resolved, obj, index, 0, create_variable(resolved, Variable::Tag::MEMBER));
}

const CallExpr *Function::call(const Type *type, SP<const Function> func,
                               ocarina::list<const Expression *> args) noexcept {
    const Function *ptr = add_used_function(func);
    const Type *resolved = ptr->return_type() != nullptr ? ptr->return_type() : resolve_ast_type(type);
    return create_expression<CallExpr>(resolved, ptr, std::move(args));
}

const CallExpr *Function::call(const ocarina::Type *type, string_view func_name,
                               ocarina::list<const Expression *> args) noexcept {
    return create_expression<CallExpr>(resolve_ast_type(type), func_name, ocarina::move(args));
}

const CallExpr *Function::call_builtin(const Type *type, CallOp op,
                                       ocarina::list<const Expression *> args,
                                       ocarina::list<CallExpr::Template> t_args) noexcept {
    if (op == CallOp::TRACE_CLOSEST || op == CallOp::TRACE_OCCLUSION) {
        set_raytracing(true);
    }
    return create_expression<CallExpr>(resolve_ast_type(type), op, std::move(args), resolve_ast_templates(std::move(t_args)));
}

const CapturedResource &Function::get_captured_resource(const Type *type, Variable::Tag tag, MemoryBlock block) noexcept {
    if (auto iter = std::find_if(captured_resources_.begin(),
                                 captured_resources_.end(),
                                 [&](auto v) {
                                     return v.handle_ptr() == block.address;
                                 });
        iter != captured_resources_.end()) {
        return *iter;
    }
    const Type *resolved = resolve_ast_type(type);
    const RefExpr *expr = _ref(create_variable(resolved, tag));
    captured_resources_.emplace_back(expr, resolved, block);
    return captured_resources_.back();
}

const CapturedResource &Function::add_captured_resource(const Type *type, Variable::Tag tag, MemoryBlock block) noexcept {
    const Type *resolved = resolve_ast_type(type);
    const RefExpr *expr = _ref(create_variable(resolved, tag, "", "cap_res"));
    captured_resources_.emplace_back(expr, resolved, block);
    return captured_resources_.back();
}

bool Function::has_captured_resource(const void *handle) const noexcept {
    bool ret = std::find_if(captured_resources_.begin(),
                            captured_resources_.end(),
                            [&](auto v) {
                                return v.handle_ptr() == handle;
                            }) != captured_resources_.end();
    return ret;
}

void Function::update_captured_resources(const Function *func) noexcept {
    func->for_each_captured_resource([&](const CapturedResource &var) {
        if (!has_captured_resource(var.handle_ptr())) {
            add_captured_resource(var.type(), var.expression()->variable().tag(), var.block());
        }
    });
}

vector<Variable> Function::all_arguments() const noexcept {
    vector<Variable> ret;
    for (const auto &v : arguments()) {
        ret.push_back(v);
    }
    for (const auto &res : captured_resources()) {
        ret.push_back(res.expression()->variable());
    }
    for (const auto &v : appended_arguments()) {
        ret.push_back(v);
    }
    return ret;
}

const CapturedResource *Function::get_captured_resource_by_handle(const void *handle) const noexcept {
    const CapturedResource *var = nullptr;
    for (const CapturedResource &v : captured_resources_) {
        if (v.handle_ptr() == handle) {
            var = &v;
            break;
        }
    }
    return var;
}

Function *Function::current() noexcept {
    if (_function_stack().empty()) {
        return nullptr;
    }
    return _function_stack().top().get();
}

ScopeStmt *Function::scope() noexcept {
    return create_statement<ScopeStmt>(false);
}

void Function::add_header(std::string_view fn) noexcept {
    headers_.push_back(fn);
}

IfStmt *Function::if_(const Expression *expr) noexcept {
    return create_statement<IfStmt>(expr);
}

SwitchStmt *Function::switch_(const Expression *expr) noexcept {
    return create_statement<SwitchStmt>(expr);
}

SwitchCaseStmt *Function::switch_case(const Expression *expr) noexcept {
    return create_statement<SwitchCaseStmt>(expr);
}

const ExprStmt *Function::expr_statement(const Expression *expr) noexcept {
    return create_statement<ExprStmt>(expr);
}

SwitchDefaultStmt *Function::switch_default() noexcept {
    return create_statement<SwitchDefaultStmt>();
}

LoopStmt *Function::loop() noexcept {
    return create_statement<LoopStmt>();
}

ForStmt *Function::for_(const Expression *init, const Expression *cond, const Expression *step) noexcept {
    return create_statement<ForStmt>(init, cond, step);
}

void Function::continue_() noexcept {
    create_statement<ContinueStmt>();
}

void Function::break_() noexcept {
    create_statement<BreakStmt>();
}

void Function::comment(const ocarina::string &string) noexcept {
    create_statement<CommentStmt>(string);
}

void Function::print(string fmt, const vector<const Expression *> &args) noexcept {
    create_statement<PrintStmt>(fmt, args);
}

ocarina::span<const Variable> Function::arguments() const noexcept {
    return arguments_;
}

ocarina::span<const Variable> Function::appended_arguments() const noexcept {
    return appended_arguments_;
}

ocarina::span<const Variable> Function::builtin_vars() const noexcept {
    return builtin_vars_;
}

ocarina::string Function::func_name(uint64_t ext_hash, string ext_name) const noexcept {
    uint64_t final_hash = ext_hash == 0 ? hash() : hash64(hash(), ext_hash);
    if (is_kernel()) {
        if (is_raytracing()) {
            return detail::raygen_name(final_hash, ocarina::move(ext_name));
        } else {
            return detail::kernel_name(final_hash, ocarina::move(ext_name));
        }
    } else {
        return detail::func_name(final_hash);
    }
}

namespace detail {
void assign_verify(const Expression *lhs, const Expression *rhs) noexcept {
    if (lhs->type()->is_array()) {
        OC_ASSERT(lhs->type()->dimension() == rhs->type()->dimension() || rhs->type()->dimension() == 1);
    }
}
}// namespace detail

void Function::assign(const Expression *lhs, const Expression *rhs) noexcept {
    detail::assign_verify(lhs, rhs);
    create_statement<AssignStmt>(lhs, rhs);
}

uint64_t Function::compute_hash() const noexcept {
    auto ret = ret_ ? ret_->hash() : 0;
    for_each_header([&](string_view fn) {
        ret = hash64(ret, fn);
    });
    ret = hash64(ret,
                 static_cast<uint>(storage_policy_.policy),
                 static_cast<uint>(storage_policy_.allow_real_in_storage));
    ret = hash64(tag(), ret);
    for (const Variable &v : arguments_) {
        ret = hash64(ret, v.hash());
    }
    for (const Variable &v : appended_arguments_) {
        ret = hash64(ret, v.hash());
    }
    for (const Variable &v : builtin_vars_) {
        ret = hash64(ret, v.hash());
    }
    for (const CapturedResource &v : captured_resources_) {
        ret = hash64(ret, v.hash());
    }
    ret = hash64(ret, body_.hash());
    return ret;
}

}// namespace ocarina