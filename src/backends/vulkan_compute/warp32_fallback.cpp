#include "ast_to_slang_source.h"

namespace ocarina {
namespace {

bool is_warp(CallOp op) noexcept {
    return op >= CallOp::WARP_ACTIVE_COUNT_BITS && op <= CallOp::WARP_IS_FIRST_ACTIVE_LANE;
}

template<typename F>
void expression_children(const Expression *expr, F &&visit) {
    switch (expr->tag()) {
        case Expression::Tag::UNARY: visit(static_cast<const UnaryExpr *>(expr)->operand()); break;
        case Expression::Tag::BINARY: {
            auto e = static_cast<const BinaryExpr *>(expr);
            visit(e->lhs()); visit(e->rhs());
            break;
        }
        case Expression::Tag::MEMBER: visit(static_cast<const MemberExpr *>(expr)->parent()); break;
        case Expression::Tag::SUBSCRIPT: {
            auto e = static_cast<const SubscriptExpr *>(expr);
            visit(e->range());
            e->for_each_index(visit);
            break;
        }
        case Expression::Tag::CAST: visit(static_cast<const CastExpr *>(expr)->expression()); break;
        case Expression::Tag::CONDITIONAL: {
            auto e = static_cast<const ConditionalExpr *>(expr);
            visit(e->pred()); visit(e->true_()); visit(e->false_());
            break;
        }
        case Expression::Tag::CALL:
            for (auto argument : static_cast<const CallExpr *>(expr)->arguments()) visit(argument);
            break;
        default: break;
    }
}

struct Scan {
    bool warp{}, returns{}, barrier{};
    set<const Function *> functions;

    void expression(const Expression *expr) {
        if (!expr) return;
        if (expr->tag() == Expression::Tag::CALL) {
            auto call = static_cast<const CallExpr *>(expr);
            warp |= is_warp(call->call_op());
            barrier |= call->call_op() == CallOp::SYNCHRONIZE_BLOCK;
            if (auto function = call->function(); function && functions.insert(function).second) {
                // Callable returns are not kernel early returns.
                const bool saved_returns = returns;
                statement(function->body());
                returns = saved_returns;
            }
        }
        expression_children(expr, [this](auto child) { expression(child); });
    }

    void statement(const Statement *stmt) {
        if (!stmt) return;
        switch (stmt->tag()) {
            case Statement::Tag::SCOPE:
                for (auto child : static_cast<const ScopeStmt *>(stmt)->statements()) statement(child);
                break;
            case Statement::Tag::ASSIGN: {
                auto s = static_cast<const AssignStmt *>(stmt);
                expression(s->lhs()); expression(s->rhs()); break;
            }
            case Statement::Tag::EXPR: expression(static_cast<const ExprStmt *>(stmt)->expression()); break;
            case Statement::Tag::RETURN:
                returns = true;
                expression(static_cast<const ReturnStmt *>(stmt)->expression()); break;
            case Statement::Tag::IF: {
                auto s = static_cast<const IfStmt *>(stmt);
                expression(s->condition()); statement(s->true_branch()); statement(s->false_branch()); break;
            }
            case Statement::Tag::LOOP: statement(static_cast<const LoopStmt *>(stmt)->body()); break;
            case Statement::Tag::FOR: {
                auto s = static_cast<const ForStmt *>(stmt);
                expression(s->var()); expression(s->condition()); expression(s->step()); statement(s->body()); break;
            }
            case Statement::Tag::SWITCH: {
                auto s = static_cast<const SwitchStmt *>(stmt);
                expression(s->expression()); statement(s->body()); break;
            }
            case Statement::Tag::SWITCH_CASE: statement(static_cast<const SwitchCaseStmt *>(stmt)->body()); break;
            case Statement::Tag::SWITCH_DEFAULT: statement(static_cast<const SwitchDefaultStmt *>(stmt)->body()); break;
            default: break;
        }
    }
};

// Hoisting a collective must not reorder a sibling atomic/write/call with
// unknown effects. Ordinary buffer reads are evaluated under the active mask.
void validate_operands(const Expression *expr, bool root = true) {
    if (!expr) return;
    if (!root && expr->tag() == Expression::Tag::CALL) {
        const auto op = static_cast<const CallExpr *>(expr)->call_op();
        const bool effect = op == CallOp::CUSTOM || op == CallOp::TEX2D_WRITE || op == CallOp::TEX3D_WRITE ||
                            op == CallOp::BYTE_BUFFER_WRITE || op == CallOp::BINDLESS_ARRAY_BUFFER_WRITE ||
                            op == CallOp::BINDLESS_ARRAY_BYTE_BUFFER_WRITE ||
                            (op >= CallOp::ATOMIC_EXCH && op <= CallOp::ATOMIC_CAS);
        OC_ERROR_IF(effect, "warp32 fallback does not support side-effecting operands around collective expressions");
    }
    expression_children(expr, [](auto child) { validate_operands(child, false); });
}

}// namespace

bool AstToSlangSource::_contains_warp32(const Expression *expr) noexcept {
    Scan scan;
    scan.expression(expr);
    return scan.warp;
}

bool AstToSlangSource::_contains_warp32(const Statement *stmt) noexcept {
    Scan scan;
    scan.statement(stmt);
    return scan.warp;
}

bool AstToSlangSource::_in_warp32_fallback() noexcept {
    return warp32_fallback_ && current_function().is_kernel();
}

string AstToSlangSource::_warp32_participation() const noexcept {
    return "(oc_warp32_live && " + warp32_mask_ + ")";
}

void AstToSlangSource::_prepare_warp32_expression(const Expression *expr) noexcept {
    if (!expr) return;
    if (expr->tag() == Expression::Tag::CONDITIONAL)
        OC_ERROR_IF(_contains_warp32(expr), "warp32 fallback does not support collectives in conditional expressions; use a structured if");
    if (expr->tag() == Expression::Tag::BINARY) {
        const auto op = static_cast<const BinaryExpr *>(expr)->op();
        OC_ERROR_IF((op == BinaryOp::AND || op == BinaryOp::OR) && _contains_warp32(expr),
                    "warp32 fallback does not support collectives in short-circuit expressions; use a structured if");
    }
    expression_children(expr, [this](auto child) { _prepare_warp32_expression(child); });
    if (expr->tag() != Expression::Tag::CALL) return;
    auto call = static_cast<const CallExpr *>(expr);
    const auto op = call->call_op();
    if (!is_warp(op) || warp32_values_.contains(call)) return;
    const string name = "oc_warp32_value_" + std::to_string(warp32_temporary_++);
    _emit_indent();
    _emit_type_name(expr->type());
    current_scratch() << " " << name << " = ";
    _emit_default_value(expr->type());
    current_scratch() << ";";
    _emit_newline();
    if (op == CallOp::WARP_SIZE || op == CallOp::WARP_LANE_ID) {
        _emit_indent();
        current_scratch() << name << " = " << (op == CallOp::WARP_SIZE ? "32u" : "oc_warp32_index % 32u") << ";";
        _emit_newline();
    } else {
        const string predicate = name + "_predicate", bits = name + "_bits";
        _emit_indent();
        current_scratch() << "bool " << predicate << " = false;";
        _emit_newline();
        _emit_indent();
        current_scratch() << "if " << _warp32_participation() << " { " << predicate << " = ";
        if (call->arguments().empty()) current_scratch() << "true";
        else _emit_converted_value(Type::of<bool>(), call->argument(0u));
        current_scratch() << "; }";
        _emit_newline();
        _emit_indent();
        current_scratch() << "uint " << bits << " = oc_warp32_ballot(" << predicate << ", oc_warp32_index);";
        _emit_newline();
        _emit_indent();
        current_scratch() << name << " = ";
        switch (op) {
            case CallOp::WARP_ACTIVE_BIT_MASK: current_scratch() << "uint4(" << bits << ", 0u, 0u, 0u)"; break;
            case CallOp::WARP_ACTIVE_COUNT_BITS: current_scratch() << "countbits(" << bits << ")"; break;
            case CallOp::WARP_PREFIX_COUNT_BITS:
                current_scratch() << "countbits(" << bits << " & ((1u << (oc_warp32_index % 32u)) - 1u))"; break;
            case CallOp::WARP_FIRST_ACTIVE_LANE: current_scratch() << "uint(firstbitlow(" << bits << "))"; break;
            case CallOp::WARP_IS_FIRST_ACTIVE_LANE:
                current_scratch() << "(oc_warp32_index % 32u == uint(firstbitlow(" << bits << ")))"; break;
            default: OC_ERROR("unhandled warp32 fallback operation"); break;
        }
        current_scratch() << ";";
        _emit_newline();
    }
    warp32_values_.emplace(call, name);
}

void AstToSlangSource::_emit_warp32_if(const IfStmt *stmt) noexcept {
    warp32_values_.clear();
    if (_contains_warp32(stmt->condition())) validate_operands(stmt->condition());
    _prepare_warp32_expression(stmt->condition());
    const string condition = "oc_warp32_condition_" + std::to_string(warp32_temporary_++);
    current_scratch() << "{ bool " << condition << " = false;";
    _emit_newline();
    indent_inc();
    _emit_indent();
    current_scratch() << "if " << _warp32_participation() << " { " << condition << " = ";
    _emit_converted_value(Type::of<bool>(), stmt->condition());
    current_scratch() << "; }";
    _emit_newline();
    warp32_values_.clear();
    const string outer = warp32_mask_;
    warp32_mask_ = "(" + outer + " && " + condition + ")";
    _emit_indent();
    stmt->true_branch()->accept(*this);
    _emit_newline();
    warp32_mask_ = "(" + outer + " && !" + condition + ")";
    _emit_indent();
    stmt->false_branch()->accept(*this);
    _emit_newline();
    warp32_mask_ = outer;
    indent_dec();
    _emit_indent();
    current_scratch() << "}";
}

void AstToSlangSource::_emit_warp32_statements(span<const Statement *const> statements) noexcept {
    for (const Statement *stmt : statements) {
        if (!_should_emit_statement(stmt)) continue;
        Scan scan;
        scan.statement(stmt);
        const auto tag = stmt->tag();
        // A loop without collectives can still wait on a different logical
        // warp that is parked at a workgroup-wide fallback barrier. Until a
        // progress-preserving loop lowering exists, reject all user AST loops.
        OC_ERROR_IF(tag == Statement::Tag::LOOP || tag == Statement::Tag::FOR,
                    "warp32 fallback does not support loop control flow, including callable loops");
        if (tag == Statement::Tag::SWITCH)
            OC_ERROR_IF(scan.warp || scan.returns || scan.barrier,
                        "warp32 fallback does not support switches containing collectives, barriers or kernel returns");
        OC_ERROR_IF(tag == Statement::Tag::IF && scan.barrier,
                    "warp32 fallback does not support explicit block barriers inside conditionals");
        // A collective-bearing scope or branch must be visited by every physical
        // invocation. Its individual user statements carry the participation mask.
        if (tag == Statement::Tag::SCOPE || (tag == Statement::Tag::IF && scan.warp)) {
            _emit_indent();
            stmt->accept(*this);
            _emit_newline();
            continue;
        }
        warp32_values_.clear();
        auto prepare = [this](const Expression *expr) {
            if (_contains_warp32(expr)) validate_operands(expr);
            _prepare_warp32_expression(expr);
        };
        if (tag == Statement::Tag::ASSIGN) {
            auto assign = static_cast<const AssignStmt *>(stmt);
            prepare(assign->lhs()); prepare(assign->rhs());
        } else if (tag == Statement::Tag::EXPR) {
            const Expression *expr = static_cast<const ExprStmt *>(stmt)->expression();
            if (expr->tag() == Expression::Tag::CALL &&
                static_cast<const CallExpr *>(expr)->call_op() == CallOp::SYNCHRONIZE_BLOCK) {
                // Tail and returned invocations remain at this unconditional
                // synchronization point, just as for the fallback's barriers.
                _emit_regular_statements({&stmt, 1u});
                continue;
            }
            prepare(expr);
        }
        _emit_indent();
        current_scratch() << "if " << _warp32_participation() << " {";
        _emit_newline();
        indent_inc();
        // Retain the existing callable-alias diagnostic suppression.
        _emit_regular_statements({&stmt, 1u});
        indent_dec();
        _emit_indent();
        current_scratch() << "}";
        _emit_newline();
        warp32_values_.clear();
    }
}

}// namespace ocarina
