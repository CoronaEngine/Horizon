//
// Created by Zero on 08/06/2022.
//

#include "ast_to_cpp_source.h"
#include "core/type_system/type_desc.h"
#include "core/util/util.h"

namespace ocarina {

namespace detail {
struct LiteralPrinter {
    using Scratch = SourceEmitter::Scratch;
    Scratch &scratch;
    const Type *type_{nullptr};
    explicit LiteralPrinter(Scratch &scratch, const Type *type = nullptr) : scratch(scratch), type_(type) {}

    template<typename T>
    requires(is_scalar_v<T> || is_vector_v<T>)
    void operator()(T v, const Type *type = nullptr) {
        if constexpr (ocarina::is_scalar_v<T>) {
            if constexpr (ocarina::is_floating_point_v<T>) {
                if (ocarina::isnan(v)) [[unlikely]] {
                    OC_ERROR("nan error!");
                } else if (ocarina::isinf(v)) {
                    if constexpr (ocarina::is_real_v<T>) {
                        scratch << (static_cast<float>(v) < 0.f ? "(1.f/-0.f)" : "1.f/+0.f");
                    } else {
                        scratch << (v < 0.f ? "(1.f/-0.f)" : "1.f/+0.f");
                    }
                } else {
                    if constexpr (ocarina::is_real_v<T>) {
                        scratch << static_cast<float>(v);
                    } else {
                        scratch << v;
                    }
                }
            } else {
                scratch << v;
            }
        } else {
            static constexpr auto dim = ocarina::vector_dimension_v<T>;
            const Type *vector_type = type_ == nullptr ? Type::of<T>() : type_;
            auto printer = LiteralPrinter{scratch, vector_type->element()};
            scratch << TYPE_PREFIX << vector_type->name() << "(";
            for (int i = 0; i < dim; ++i) {
                const char *token = i == dim - 1 ? ")" : ",";
                printer(v[i]);
                scratch << token;
            }
        }
    }
    template<typename T, size_t N, size_t M>
    void operator()(Matrix<T, N, M> m) {
        const Type *matrix_type = type_ == nullptr ? Type::of<Matrix<T, N, M>>() : type_;
        auto printer = LiteralPrinter{scratch, matrix_type->element()};
        scratch << TYPE_PREFIX << matrix_type->name() << "(";
        for (int i = 0; i < M; ++i) {
            const char *token = i == M - 1 ? ")" : ",";
            printer(m[i]);
            scratch << token;
        }
    }
};
}// namespace detail

void AstToCppSource::visit(const BreakStmt *stmt) noexcept {
    current_scratch() << "break";
}
void AstToCppSource::visit(const ContinueStmt *stmt) noexcept {
    current_scratch() << "continue";
}
void AstToCppSource::visit(const ReturnStmt *stmt) noexcept {
    current_scratch() << "return ";
    if (stmt->expression()) {
        stmt->expression()->accept(*this);
    }
}
void AstToCppSource::visit(const ScopeStmt *stmt) noexcept {
    current_scratch() << "{";
    _emit_newline();
    indent_inc();
    if (stmt->is_func_body()) {
        _emit_builtin_vars_define(current_function());
    }
    _emit_local_var_define(stmt);
    _emit_statements(stmt->statements());
    indent_dec();
    _emit_indent();
    current_scratch() << "}";
}
void AstToCppSource::visit(const IfStmt *stmt) noexcept {
    current_scratch() << "if (";
    stmt->condition()->accept(*this);
    current_scratch() << ") ";
    stmt->true_branch()->accept(*this);
    auto false_branch = stmt->false_branch();
    if (false_branch->empty()) {
        return;
    }
    current_scratch() << " else ";
    if (false_branch->size() == 1 && false_branch->statements()[0]->tag() == Statement::Tag::IF) {
        false_branch->statements()[0]->accept(*this);
    } else {
        false_branch->accept(*this);
    }
}

void AstToCppSource::visit(const CommentStmt *stmt) noexcept {
    _emit_comment(stmt->string());
}

void AstToCppSource::visit(const LoopStmt *stmt) noexcept {
    current_scratch() << "while (1) ";
    stmt->body()->accept(*this);
}
void AstToCppSource::visit(const ExprStmt *stmt) noexcept {
    stmt->expression()->accept(*this);
}
void AstToCppSource::visit(const SwitchStmt *stmt) noexcept {
    current_scratch() << "switch (";
    stmt->expression()->accept(*this);
    current_scratch() << ") ";
    stmt->body()->accept(*this);
}
void AstToCppSource::visit(const SwitchCaseStmt *stmt) noexcept {
    current_scratch() << "case ";
    stmt->expression()->accept(*this);
    current_scratch() << ":";
    stmt->body()->accept(*this);
}
void AstToCppSource::visit(const SwitchDefaultStmt *stmt) noexcept {
    current_scratch() << "default:";
    stmt->body()->accept(*this);
}
void AstToCppSource::visit(const AssignStmt *stmt) noexcept {
    stmt->lhs()->accept(*this);
    current_scratch() << " = ";
    stmt->rhs()->accept(*this);
}

void AstToCppSource::visit(const ForStmt *stmt) noexcept {
    current_scratch() << "for (;";
    stmt->condition()->accept(*this);
    current_scratch() << "; ";
    stmt->var()->accept(*this);
    current_scratch() << " += ";
    stmt->step()->accept(*this);
    current_scratch() << ")";
    stmt->body()->accept(*this);
}

void AstToCppSource::visit(const PrintStmt *stmt) noexcept {
    span<const Expression *const> args = stmt->args();
    current_scratch() << "printf(";
    Scratch format_scratch("\"");
    format_scratch << stmt->fmt() << "\\n\"";
    Scratch args_scratch;

    for (const Expression *expr : args) {
        switch (expr->type()->tag()) {
            case Type::Tag::USHORT:
            case Type::Tag::UINT: {
                format_scratch.replace("{}", "%u");
                break;
            }
            case Type::Tag::BOOL: {
                format_scratch.replace("{}", "%d");
                break;
            }
            case Type::Tag::FLOAT: {
                format_scratch.replace("{}", "%f");
                break;
            }
            case Type::Tag::SHORT:
            case Type::Tag::INT: {
                format_scratch.replace("{}", "%d");
                break;
            }
            case Type::Tag::UCHAR: {
                format_scratch.replace("{}", "%u");
                break;
            }
            case Type::Tag::CHAR: {
                format_scratch.replace("{}", "%d");
                break;
            }
            default: break;
        }
        SCRATCH_GUARD(args_scratch);
        current_scratch() << ",";
        expr->accept(*this);
    }
    current_scratch() << format_scratch
                      << args_scratch
                      << ")";
}

void AstToCppSource::visit(const UnaryExpr *expr) noexcept {
    switch (expr->op()) {
        case UnaryOp::POSITIVE: current_scratch() << "+"; break;
        case UnaryOp::NEGATIVE: current_scratch() << "-"; break;
        case UnaryOp::NOT: current_scratch() << "!"; break;
        case UnaryOp::BIT_NOT: current_scratch() << "~"; break;
    }
    current_scratch() << "(";
    expr->operand()->accept(*this);
    current_scratch() << ")";
}
void AstToCppSource::visit(const BinaryExpr *expr) noexcept {
    current_scratch() << "(";
    expr->lhs()->accept(*this);
    switch (expr->op()) {
        case BinaryOp::ADD: current_scratch() << " + "; break;
        case BinaryOp::SUB: current_scratch() << " - "; break;
        case BinaryOp::MUL: current_scratch() << " * "; break;
        case BinaryOp::DIV: current_scratch() << " / "; break;
        case BinaryOp::MOD: current_scratch() << " % "; break;
        case BinaryOp::BIT_AND: current_scratch() << " & "; break;
        case BinaryOp::BIT_OR: current_scratch() << " | "; break;
        case BinaryOp::BIT_XOR: current_scratch() << " ^ "; break;
        case BinaryOp::SHL: current_scratch() << " << "; break;
        case BinaryOp::SHR: current_scratch() << " >> "; break;
        case BinaryOp::AND: current_scratch() << " && "; break;
        case BinaryOp::OR: current_scratch() << " || "; break;
        case BinaryOp::LESS: current_scratch() << " < "; break;
        case BinaryOp::GREATER: current_scratch() << " > "; break;
        case BinaryOp::LESS_EQUAL: current_scratch() << " <= "; break;
        case BinaryOp::GREATER_EQUAL: current_scratch() << " >= "; break;
        case BinaryOp::EQUAL: current_scratch() << " == "; break;
        case BinaryOp::NOT_EQUAL: current_scratch() << " != "; break;
    }
    expr->rhs()->accept(*this);
    current_scratch() << ")";
}

void AstToCppSource::visit(const ocarina::ConditionalExpr *expr) {
    current_scratch() << "(";
    expr->pred()->accept(*this);
    current_scratch() << " ? ";
    expr->true_()->accept(*this);
    current_scratch() << " : ";
    expr->false_()->accept(*this);
    current_scratch() << ")";
}

void AstToCppSource::visit(const MemberExpr *expr) noexcept {
    expr->parent()->accept(*this);
    if (expr->is_swizzle()) {
        static constexpr std::string_view xyzw[] = {"x", "y", "z", "w"};
        current_scratch() << ".";
        for (int i = 0; i < expr->swizzle_size(); ++i) {
            current_scratch() << xyzw[expr->swizzle_index(i)];
        }
    } else {
        current_scratch() << ".";
        _emit_member_name(expr->parent()->type(), expr->member_index());
    }
}

void AstToCppSource::visit(const SubscriptExpr *expr) noexcept {
    expr->range()->accept(*this);
    expr->for_each_index([&](const Expression *index) {
        current_scratch() << "[";
        index->accept(*this);
        current_scratch() << "]";
    });
}

void AstToCppSource::visit(const LiteralExpr *expr) noexcept {
    auto printer = detail::LiteralPrinter(current_scratch(), expr->type());
    ocarina::visit(printer, expr->value());
}

void AstToCppSource::visit(const RefExpr *expr) noexcept {
    _emit_variable_name(expr->variable());
}

void AstToCppSource::visit(const CallExpr *expr) noexcept {
    switch (expr->call_op()) {
        case CallOp::CUSTOM: {
            _emit_func_name(*expr->function());
            current_scratch() << "(";
            for (const auto &arg : expr->arguments()) {
                arg->accept(*this);
                current_scratch() << ",";
            }
            current_scratch() << "d_dim, d_idx";
            current_scratch() << ")";
            break;
        }
        default: break;
    }
}

void AstToCppSource::visit(const CastExpr *expr) noexcept {
    switch (expr->cast_op()) {
        case CastOp::STATIC: current_scratch() << "static_cast<"; break;
        case CastOp::BITWISE: current_scratch() << "bit_cast<"; break;
    }
    _emit_type_name(expr->type());
    current_scratch() << ">(";
    expr->expression()->accept(*this);
    current_scratch() << ")";
}

void AstToCppSource::visit(const Type *type) noexcept {
    if (!type->is_structure() ||
        has_generated(type) ||
        type->is_builtin_struct()) {
        return;
    }
    current_scratch() << "struct ";
    current_scratch() << "alignas(";
    current_scratch() << type->alignment();
    current_scratch() << ") ";
    _emit_struct_name(type);
    current_scratch() << " {";
    _emit_newline();
    indent_inc();
    for (int i = 0; i < type->members().size(); ++i) {
        const Type *member = type->members()[i];
        _emit_indent();
        _emit_type_name(member);
        _emit_space();
        _emit_member_name(type, i);
        current_scratch() << "{};";
        _emit_newline();
    }
    indent_dec();
    current_scratch() << "};";
    _emit_newline();

    add_generated(type);
}

void AstToCppSource::_emit_types_define() noexcept {
    Type::for_each(this);
}

bool AstToCppSource::has_generated(const Type *type) const noexcept {
    return generated_struct_.contains(type);
}

void AstToCppSource::add_generated(const Type *type) noexcept {
    generated_struct_.emplace(type);
}

bool AstToCppSource::has_generated(const Function *func) const noexcept {
    return generated_func_.contains(func->hash());
}

void AstToCppSource::add_generated(const Function *func) noexcept {
    generated_func_.emplace(func->hash());
}

void AstToCppSource::_emit_variable_define(const Variable &v) noexcept {
    if (v.type()->is_buffer()) {
        _emit_type_name(v.type());
        _emit_space();
        _emit_variable_name(v);
    } else {
        _emit_type_name(v.type());
        _emit_space();
        switch (v.tag()) {
            case Variable::Tag::REFERENCE: current_scratch() << "&"; break;
            default: break;
        }
        _emit_variable_name(v);
    }
}

void AstToCppSource::_emit_local_var_define(const ScopeStmt *scope) noexcept {
    for (const auto &var : scope->local_vars()) {
        _emit_indent();
        _emit_variable_define(var);
        current_scratch() << "{};";
        _emit_newline();
    }
}

void AstToCppSource::_emit_builtin_vars_define(const Function &f) noexcept {
    for (const Variable &var : f.builtin_vars()) {
        _emit_indent();
        _emit_builtin_var(var);
        current_scratch() << ";";
        _emit_newline();
    }
}

void AstToCppSource::_emit_type_name(const Type *type) noexcept {
    if (type == nullptr) {
        current_scratch() << "void";
    } else {
        switch (type->tag()) {
            case Type::Tag::BOOL: current_scratch() << "bool"; break;
            case Type::Tag::FLOAT: current_scratch() << "float"; break;
            case Type::Tag::INT: current_scratch() << "int"; break;
            case Type::Tag::UINT: current_scratch() << "uint"; break;
            case Type::Tag::UCHAR: current_scratch() << "uchar"; break;
            case Type::Tag::CHAR: current_scratch() << "char"; break;
            case Type::Tag::HALF: current_scratch() << "half"; break;
            case Type::Tag::ULONG: current_scratch() << "ulong"; break;
            case Type::Tag::VECTOR:
                _emit_type_name(type->element());
                current_scratch() << type->dimension();
                break;
            case Type::Tag::ARRAY:
                _emit_type_name(type->element());
                current_scratch() << "[";
                current_scratch() << type->dimension();
                current_scratch() << "]";
                break;
            case Type::Tag::MATRIX: {
                auto d = type->dimension();
                current_scratch() << "float" << d << "x" << d;
                break;
            }
            case Type::Tag::STRUCTURE:
                _emit_struct_name(type);
                break;
            case Type::Tag::BUFFER:
                _emit_type_name(type->element());
                current_scratch() << "*";
                break;
            case Type::Tag::TEXTURE3D: break;
            case Type::Tag::BINDLESS_ARRAY: break;
            case Type::Tag::ACCEL: break;
            case Type::Tag::NONE: break;
            default: break;
        }
    }
}

void AstToCppSource::_emit_function(const Function &f) noexcept {
    if (has_generated(&f)) {
        return;
    }
    _emit_type_name(f.return_type());
    _emit_space();
    _emit_func_name(f);
    _emit_arguments(f);
    _emit_body(f);
    add_generated(&f);
    _emit_newline();
}

void AstToCppSource::_emit_variable_name(Variable v) noexcept {
    current_scratch() << v.final_name();
}

void AstToCppSource::_emit_statements(ocarina::span<const Statement *const> stmts) noexcept {
    for (const Statement *stmt : stmts) {
        _emit_indent();
        stmt->accept(*this);
        current_scratch() << ";";
        _emit_newline();
    }
}

void AstToCppSource::_emit_body(const Function &f) noexcept {
    f.body()->accept(*this);
}

void AstToCppSource::_emit_comment(const std::string &content) noexcept {
    if (obfuscation_) {
        return;
    }
    current_scratch() << "/* " << content << " */";
}

void AstToCppSource::_emit_argument(const ocarina::Variable &v) noexcept {
    _emit_variable_define(v);
    current_scratch() << ",";
    _emit_newline();
}

void AstToCppSource::_emit_arguments(const Function &f) noexcept {
    current_scratch() << "(";
    for (const auto &v : f.arguments()) {
        _emit_argument(v);
    }
    for (const auto &var : f.captured_resources()) {
        _emit_argument(var.expression()->variable());
    }
    if (f.is_kernel() && !f.is_raytracing()) {
        Variable dispatch_dim = (const_cast<Function &>(f)).create_variable(Type::of<uint3>(), Variable::Tag::LOCAL, "d_dim");
        _emit_variable_define(dispatch_dim);
    } else {
        if (f.arguments().size() + f.captured_resources().size() > 0) {
            current_scratch().pop_back();
        }
    }
    current_scratch() << ")";
}

void AstToCppSource::emit(const Function &func) noexcept {
    FUNCTION_GUARD(func)
    if (func.is_kernel()) {
        _emit_comment(func.description());
        _emit_newline();
        func.for_each_header([&](string_view fn) {
            current_scratch() << "#include \"" << fn << "\"\n";
        });
    }

    TIMER_TAG(codegen, "function " + func.func_name() + " " + func.description() + " generated");
    func.for_each_structure([&](const Type *type) {
        visit(type);
    });

    func.for_each_custom_func([&](const Function *f) {
        emit(*f);
    });
    if (func.is_raytracing_kernel()) {
        _emit_raytracing_param(func);
    }
    if (!has_generated(&func) && !func.description().empty()) {
        _emit_comment(func.description());
        _emit_newline();
    }
    _emit_function(func);
}
}// namespace ocarina