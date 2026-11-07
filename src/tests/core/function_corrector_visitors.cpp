#include "ast/function.h"
#include "core/type_system/type_desc.h"

#include <stdexcept>
#include <string>

using namespace ocarina;

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct CaptureFixture {
    shared_ptr<Function> kernel;
    shared_ptr<Function> callee;
    const CallExpr *call{};

    template<typename Build>
    void build(Build build_body) {
        kernel = Function::define_kernel([&] {
            auto *caller = Function::current();
            callee = build_body(caller);
            callee->set_allow_dsl_capture(true);
            call = caller->call(callee->return_type(), callee, {});
            caller->expr_statement(call);
        });
    }

    void expect_count(size_t count) const {
        require(callee->arguments().empty(), "visitor fixture unexpectedly has explicit arguments");
        require(callee->appended_arguments().size() == count, "visitor fixture has wrong capture count");
        require(call->arguments().size() == count, "visitor fixture has wrong callsite argument count");
    }

    void expect_capture(const Expression *original, const Expression *rewritten, const char *where) const {
        const auto *reference = dynamic_cast<const RefExpr *>(rewritten);
        if (reference && reference->context() == callee.get() && reference->type() == original->type()) {
            for (size_t i = 0; i < call->arguments().size(); ++i) {
                const auto formal = callee->appended_arguments()[i];
                if (call->argument(static_cast<uint>(i)) == original &&
                    formal.uid() == reference->variable().uid() && formal.tag() == Variable::Tag::REFERENCE) {
                    return;
                }
            }
        }
        throw std::runtime_error(std::string(where) + " did not rewrite to the matching capture parameter");
    }
};

void test_expression_children() {
    CaptureFixture fixture;
    const RefExpr *pred{}, *binary_left{}, *binary_right{}, *unary_operand{}, *cast_operand{};
    const RefExpr *array{}, *first_index{}, *second_index{}, *builtin_first{}, *builtin_second{}, *pair{};
    const ConditionalExpr *conditional{};
    const BinaryExpr *binary{};
    const UnaryExpr *unary{};
    const CastExpr *cast{};
    const SubscriptExpr *subscript{};
    const MemberExpr *member{};
    const CallExpr *builtin{};
    const ExprStmt *member_stmt{};
    fixture.build([&](Function *caller) {
        pred = caller->local(Type::of<bool>());
        binary_left = caller->local(Type::of<int>());
        binary_right = caller->local(Type::of<int>());
        unary_operand = caller->local(Type::of<int>());
        cast_operand = caller->local(Type::of<float>());
        array = caller->local(Type::of<int[2][2]>());
        first_index = caller->local(Type::of<int>());
        second_index = caller->local(Type::of<int>());
        builtin_first = caller->local(Type::of<int>());
        builtin_second = caller->local(Type::of<int>());
        pair = caller->local(Type::of<int2>());
        return Function::define_callable([&] {
            auto *function = Function::current();
            const auto *int_type = Type::of<int>();
            binary = function->binary(int_type, BinaryOp::ADD, binary_left, binary_right);
            unary = function->unary(int_type, UnaryOp::NEGATIVE, unary_operand);
            conditional = function->conditional(int_type, pred, binary, unary);
            function->expr_statement(conditional);
            cast = function->cast(int_type, CastOp::STATIC, cast_operand);
            function->expr_statement(cast);
            subscript = function->subscript(int_type, array, ocarina::vector<const Expression *>{first_index, second_index});
            function->expr_statement(subscript);
            builtin = function->call_builtin(int_type, CallOp::MAX, {builtin_first, builtin_second});
            function->expr_statement(builtin);
            member = function->member(int_type, pair, 1);
            member_stmt = function->expr_statement(member);
        });
    });
    fixture.expect_count(11);
    fixture.expect_capture(pred, conditional->pred(), "conditional predicate");
    require(conditional->true_() == binary && conditional->false_() == unary, "conditional lost its branch expressions");
    fixture.expect_capture(binary_left, binary->lhs(), "binary left operand");
    fixture.expect_capture(binary_right, binary->rhs(), "binary right operand");
    fixture.expect_capture(unary_operand, unary->operand(), "unary operand");
    fixture.expect_capture(cast_operand, cast->expression(), "cast operand");
    fixture.expect_capture(array, subscript->range(), "subscript range");
    fixture.expect_capture(first_index, subscript->index(0), "first subscript index");
    fixture.expect_capture(second_index, subscript->index(1), "second subscript index");
    require(!builtin->function() && builtin->call_op() == CallOp::MAX, "builtin lost its call operation");
    fixture.expect_capture(builtin_first, builtin->argument(0), "first builtin argument");
    fixture.expect_capture(builtin_second, builtin->argument(1), "second builtin argument");
    require(member_stmt->expression() == member && member->member_index() == 1, "local member access was replaced");
    fixture.expect_capture(pair, member->parent(), "local member's outer parent");
}

void test_control_flow_children() {
    CaptureFixture fixture;
    const RefExpr *if_condition{}, *assignment_lhs{}, *assignment_rhs{}, *else_value{};
    const RefExpr *for_var{}, *for_condition{}, *for_step{}, *for_value{}, *loop_value{};
    const RefExpr *selector{}, *case_value{}, *default_value{}, *return_value{};
    const LiteralExpr *case_label{};
    const IfStmt *if_stmt{};
    const ForStmt *for_stmt{};
    const LoopStmt *loop_stmt{};
    const SwitchStmt *switch_stmt{};
    const SwitchCaseStmt *case_stmt{};
    const SwitchDefaultStmt *default_stmt{};
    fixture.build([&](Function *caller) {
        const auto *int_type = Type::of<int>();
        const auto *bool_type = Type::of<bool>();
        if_condition = caller->local(bool_type);
        assignment_lhs = caller->local(int_type);
        assignment_rhs = caller->local(int_type);
        else_value = caller->local(int_type);
        for_var = caller->local(int_type);
        for_condition = caller->local(bool_type);
        for_step = caller->local(int_type);
        for_value = caller->local(int_type);
        loop_value = caller->local(int_type);
        selector = caller->local(int_type);
        case_value = caller->local(int_type);
        default_value = caller->local(int_type);
        return_value = caller->local(int_type);
        return Function::define_callable([&] {
            auto *function = Function::current();
            auto *branch = function->if_(if_condition);
            if_stmt = branch;
            function->with(branch->true_branch(), [&] { function->assign(assignment_lhs, assignment_rhs); });
            function->with(branch->false_branch(), [&] { function->expr_statement(else_value); });
            auto *iteration = function->for_(for_var, for_condition, for_step);
            for_stmt = iteration;
            function->with(iteration->body(), [&] {
                function->expr_statement(for_value);
                function->continue_();
            });
            auto *loop = function->loop();
            loop_stmt = loop;
            function->with(loop->body(), [&] {
                function->expr_statement(loop_value);
                function->break_();
            });
            auto *selection = function->switch_(selector);
            switch_stmt = selection;
            function->with(selection->body(), [&] {
                case_label = function->literal(int_type, 1);
                auto *label = function->switch_case(case_label);
                case_stmt = label;
                function->with(label->body(), [&] {
                    function->expr_statement(case_value);
                    function->break_();
                });
                auto *fallback = function->switch_default();
                default_stmt = fallback;
                function->with(fallback->body(), [&] { function->expr_statement(default_value); });
            });
            function->return_(return_value);
        });
    });
    fixture.expect_count(13);
    const auto first_expression = [](const ScopeStmt *scope) {
        const auto *stmt = dynamic_cast<const ExprStmt *>(scope->statements().front());
        require(stmt != nullptr, "expected expression statement at the start of the scope");
        return stmt->expression();
    };
    fixture.expect_capture(if_condition, if_stmt->condition(), "if condition");
    const auto *assignment = dynamic_cast<const AssignStmt *>(if_stmt->true_branch()->statements().front());
    require(assignment != nullptr, "if true branch lost its assignment");
    fixture.expect_capture(assignment_lhs, assignment->lhs(), "assignment destination");
    fixture.expect_capture(assignment_rhs, assignment->rhs(), "assignment source");
    require(assignment->lhs()->usage() == Usage::WRITE && assignment_lhs->usage() == Usage::WRITE,
            "assignment did not propagate write usage from the formal to the caller");
    fixture.expect_capture(else_value, first_expression(if_stmt->false_branch()), "if false branch");
    fixture.expect_capture(for_var, for_stmt->var(), "for variable");
    fixture.expect_capture(for_condition, for_stmt->condition(), "for condition");
    fixture.expect_capture(for_step, for_stmt->step(), "for step");
    fixture.expect_capture(for_value, first_expression(for_stmt->body()), "for body");
    require(dynamic_cast<const ContinueStmt *>(for_stmt->body()->statements().back()) != nullptr, "for body lost continue");
    fixture.expect_capture(loop_value, first_expression(loop_stmt->body()), "loop body");
    require(dynamic_cast<const BreakStmt *>(loop_stmt->body()->statements().back()) != nullptr, "loop body lost break");
    fixture.expect_capture(selector, switch_stmt->expression(), "switch selector");
    require(case_stmt->expression() == case_label, "switch case label changed");
    fixture.expect_capture(case_value, first_expression(case_stmt->body()), "switch case body");
    fixture.expect_capture(default_value, first_expression(default_stmt->body()), "switch default body");
    const auto *returned = dynamic_cast<const ReturnStmt *>(fixture.callee->body()->statements().back());
    require(returned != nullptr, "callable lost its return statement");
    fixture.expect_capture(return_value, returned->expression(), "return value");
}

void test_local_and_null_children() {
    CaptureFixture fixture;
    const RefExpr *local{};
    const LiteralExpr *literal{};
    const ExprStmt *expression{};
    fixture.build([&](Function *) {
        return Function::define_callable([&] {
            auto *function = Function::current();
            local = function->local(Type::of<int>());
            literal = function->literal(Type::of<int>(), 7);
            function->assign(local, literal);
            function->comment("local value");
            expression = function->expr_statement(nullptr);
            function->return_(nullptr);
        });
    });
    fixture.expect_count(0);
    const auto statements = fixture.callee->body()->statements();
    require(statements.size() == 4, "local fixture statements changed");
    const auto *assignment = dynamic_cast<const AssignStmt *>(statements[0]);
    require(assignment && assignment->lhs() == local && assignment->rhs() == literal, "local or literal identity changed");
    const auto *comment = dynamic_cast<const CommentStmt *>(statements[1]);
    require(comment && comment->string() == "local value", "comment was lost");
    require(expression == statements[2] && expression->expression() == nullptr, "null expression statement changed");
    const auto *returned = dynamic_cast<const ReturnStmt *>(statements[3]);
    require(returned && returned->expression() == nullptr, "void return changed");
}

}// namespace

void test_function_corrector_visitors() {
    test_expression_children();
    test_control_flow_children();
    test_local_and_null_children();
}

void probe_function_corrector_shared_capture() {
    CaptureFixture fixture;
    fixture.build([](Function *caller) {
        const auto *shared = caller->shared(Type::of<int[4]>());
        return Function::define_callable([&] { Function::current()->expr_statement(shared); });
    });
}

void probe_function_corrector_forbidden_capture() {
    Function::define_kernel([] {
        auto *caller = Function::current();
        const auto *outer = caller->local(Type::of<int>());
        auto callee = Function::define_callable([&] { Function::current()->expr_statement(outer); });
        caller->expr_statement(caller->call(nullptr, callee, {}));
    });
}
