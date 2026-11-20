#include "function_requirements.h"
#include "function.h"

namespace ocarina {
namespace {

// Walk the final statement tree. Function's allocation arena can also contain
// discarded expressions, and all_call_expr() records callers, not callees.
class RequirementVisitor final : private StmtVisitor, private ExprVisitor {
private:
    FunctionRequirements result_;
    unordered_set<const Expression *> expressions_;
    const Function *current_{};
    set<std::pair<const Function *, string>> recorded_;

    void record(bool &flag, string_view reason) {
        flag = true;
        if (recorded_.emplace(current_, reason).second) {
            const auto &name = current_->description();
            const auto kind = current_->is_kernel() ? "kernel" : "callable";
            result_.diagnostic_sources.push_back(
                name.empty() ? ocarina::format("{}: {}", kind, reason) :
                               ocarina::format("{} '{}': {}", kind, name, reason));
        }
    }

    void function(const Function &f) {
        if (!result_.reachable_functions.insert(&f).second) return;
        auto *caller = std::exchange(current_, &f);
        if (f.requires_ray_query_compute_workgroups()) record(result_.requires_compute_workgroups, "explicit compute workgroups");
        if (!f.shared_vars().empty()) record(result_.requires_compute_workgroups, "shared memory");
        for (const auto &var : f.builtin_vars()) {
            switch (var.tag()) {
                case Variable::Tag::BLOCK_IDX: record(result_.requires_compute_workgroups, "block_idx"); break;
                case Variable::Tag::THREAD_IDX: record(result_.requires_compute_workgroups, "thread_idx"); break;
                case Variable::Tag::THREAD_ID: record(result_.requires_compute_workgroups, "thread_id"); break;
                default: break;
            }
        }
        f.for_each_header([&](string_view header) {
            record(result_.has_native_source, ocarina::format("custom header '{}'", header));
        });
        f.body()->accept(*this);
        current_ = caller;
    }

    void expression(const Expression *e) {
        if (e && expressions_.insert(e).second) e->accept(*this);
    }
    void visit(const CallExpr *e) override {
        const auto op = e->call_op();
        if (op == CallOp::TRACE_CLOSEST || op == CallOp::TRACE_OCCLUSION ||
            op == CallOp::TRACE_CLOSEST_WITH_DISTANCE) result_.uses_trace = true;
        if (op == CallOp::SYNCHRONIZE_BLOCK) record(result_.requires_compute_workgroups, "synchronize_block");
        if (op >= CallOp::WARP_ACTIVE_COUNT_BITS && op <= CallOp::WARP_IS_FIRST_ACTIVE_LANE)
            record(result_.uses_warp, "warp operation");
        if (op >= CallOp::SUBGROUP_ACTIVE_SUM && op <= CallOp::SUBGROUP_ACTIVE_COUNT_BITS) {
            record(result_.uses_subgroup, "subgroup operation");
            result_.uses_subgroup_arithmetic |= op == CallOp::SUBGROUP_ACTIVE_SUM;
            result_.uses_subgroup_ballot |= op == CallOp::SUBGROUP_ACTIVE_COUNT_BITS;
        }
        if (!e->function_name().empty())
            record(result_.has_native_source, ocarina::format("native call '{}'", e->function_name()));
        if (e->function()) function(*e->function());
        for (const auto *arg : e->arguments()) expression(arg);
    }
    void visit(const UnaryExpr *e) override { expression(e->operand()); }
    void visit(const BinaryExpr *e) override {
        expression(e->lhs());
        expression(e->rhs());
    }
    void visit(const MemberExpr *e) override { expression(e->parent()); }
    void visit(const SubscriptExpr *e) override {
        expression(e->range());
        e->for_each_index([&](const Expression *index) { expression(index); });
    }
    void visit(const ConditionalExpr *e) override {
        expression(e->pred());
        expression(e->true_());
        expression(e->false_());
    }
    void visit(const CastExpr *e) override { expression(e->expression()); }
    void visit(const LiteralExpr *) override {}
    void visit(const RefExpr *) override {}

    void visit(const ScopeStmt *s) override {
        for (auto *stmt : s->statements()) stmt->accept(*this);
    }
    void visit(const ExprStmt *s) override { expression(s->expression()); }
    void visit(const ReturnStmt *s) override { expression(s->expression()); }
    void visit(const AssignStmt *s) override {
        expression(s->lhs());
        expression(s->rhs());
    }
    void visit(const IfStmt *s) override {
        expression(s->condition());
        s->true_branch()->accept(*this);
        s->false_branch()->accept(*this);
    }
    void visit(const LoopStmt *s) override { s->body()->accept(*this); }
    void visit(const ForStmt *s) override {
        expression(s->var());
        expression(s->condition());
        expression(s->step());
        s->body()->accept(*this);
    }
    void visit(const SwitchStmt *s) override {
        expression(s->expression());
        s->body()->accept(*this);
    }
    void visit(const SwitchCaseStmt *s) override {
        expression(s->expression());
        s->body()->accept(*this);
    }
    void visit(const SwitchDefaultStmt *s) override { s->body()->accept(*this); }
    void visit(const BreakStmt *) override {}
    void visit(const ContinueStmt *) override {}
    void visit(const CommentStmt *) override {}

public:
    FunctionRequirements analyze(const Function &entry) {
        function(entry);
        return std::move(result_);
    }
};

}// namespace

FunctionRequirements analyze_function_requirements(const Function &function) noexcept {
    return RequirementVisitor{}.analyze(function);
}

}// namespace ocarina
