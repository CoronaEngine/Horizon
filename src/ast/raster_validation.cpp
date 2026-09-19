#include "raster_validation.h"
#include "function.h"

#include <unordered_map>
#include <unordered_set>

namespace horizon::ast
{
namespace
{

string describe(const Function &function)
{
    return string(function.is_vertex()     ? "vertex "
                  : function.is_fragment() ? "fragment "
                  : function.is_kernel()   ? "kernel "
                                           : "callable ") +
           function.description();
}

class RasterWalker : public StmtVisitor, public ExprVisitor
{
protected:
    void expression(const Expression *expr)
    {
        if (expr)
        {
            on_expression(expr);
            expr->accept(*this);
        }
    }
    virtual void on_call(const CallExpr *) {}
    virtual void on_write(const Expression *) {}
    virtual void on_return(const ReturnStmt *) {}
    virtual void on_discard() {}
    virtual void on_expression(const Expression *) {}
    virtual void on_statement(const Statement *) {}

public:
    void visit(const ScopeStmt *stmt) override
    {
        on_statement(stmt);
        for (auto *child : stmt->statements())
        {
            on_statement(child);
            child->accept(*this);
        }
    }
    void visit(const ReturnStmt *stmt) override
    {
        on_return(stmt);
        expression(stmt->expression());
    }
    void visit(const IfStmt *stmt) override
    {
        expression(stmt->condition());
        visit(stmt->true_branch());
        visit(stmt->false_branch());
    }
    void visit(const LoopStmt *stmt) override
    {
        visit(stmt->body());
    }
    void visit(const ForStmt *stmt) override
    {
        on_write(stmt->var());
        expression(stmt->var());
        expression(stmt->condition());
        expression(stmt->step());
        visit(stmt->body());
    }
    void visit(const SwitchStmt *stmt) override
    {
        expression(stmt->expression());
        visit(stmt->body());
    }
    void visit(const SwitchCaseStmt *stmt) override
    {
        expression(stmt->expression());
        visit(stmt->body());
    }
    void visit(const SwitchDefaultStmt *stmt) override
    {
        visit(stmt->body());
    }
    void visit(const ExprStmt *stmt) override
    {
        expression(stmt->expression());
    }
    void visit(const AssignStmt *stmt) override
    {
        on_write(stmt->lhs());
        expression(stmt->lhs());
        expression(stmt->rhs());
    }
    void visit(const BreakStmt *) override {}
    void visit(const ContinueStmt *) override {}
    void visit(const CommentStmt *) override {}
    void visit(const DiscardStmt *) override
    {
        on_discard();
    }
    void visit(const UnaryExpr *expr) override
    {
        expression(expr->operand());
    }
    void visit(const BinaryExpr *expr) override
    {
        expression(expr->lhs());
        expression(expr->rhs());
    }
    void visit(const CastExpr *expr) override
    {
        expression(expr->expression());
    }
    void visit(const ConditionalExpr *expr) override
    {
        expression(expr->pred());
        expression(expr->true_());
        expression(expr->false_());
    }
    void visit(const MemberExpr *expr) override
    {
        expression(expr->parent());
    }
    void visit(const SubscriptExpr *expr) override
    {
        expression(expr->range());
        expr->for_each_index(
            [&](const Expression *index)
            {
                expression(index);
            });
    }
    void visit(const LiteralExpr *) override {}
    void visit(const RefExpr *) override {}
    void visit(const CallExpr *expr) override
    {
        for (auto *arg : expr->arguments())
        {
            expression(arg);
        }
        on_call(expr);
    }
};

class HashResetter final : public RasterWalker
{
private:
    std::unordered_set<const Function *> visited_;
    void on_expression(const Expression *expr) override
    {
        expr->reset_hash();
    }
    void on_statement(const Statement *stmt) override
    {
        stmt->reset_hash();
    }
    void on_call(const CallExpr *call) override
    {
        if (const auto *callee = call->function())
        {
            reset(*callee);
        }
    }

public:
    void reset(const Function &function)
    {
        if (!visited_.insert(&function).second)
        {
            return;
        }
        function.reset_hash();
        visit(function.body());
    }
};

class GraphValidator final : public RasterWalker
{
private:
    vector<RasterDiagnostic> &diagnostics_;
    std::unordered_set<const Function *> visiting_;
    std::unordered_set<const Function *> visited_;

    void on_call(const CallExpr *call) override
    {
        if (auto *callee = call->function())
        {
            if (!callee->is_callable())
            {
                diagnostics_.push_back({RasterDiagnosticCode::InvalidStage,
                                        "Shader entry cannot be called as a callable: " + describe(*callee)});
            }
            check(*callee);
        }
    }

public:
    explicit GraphValidator(vector<RasterDiagnostic> &diagnostics) : diagnostics_(diagnostics) {}
    void check(const Function &function)
    {
        if (visiting_.contains(&function))
        {
            diagnostics_.push_back(
                {RasterDiagnosticCode::RecursiveCall, "Recursive shader call: " + describe(function)});
            return;
        }
        if (visited_.contains(&function))
        {
            return;
        }
        visiting_.insert(&function);
        visit(function.body());
        visiting_.erase(&function);
        visited_.insert(&function);
    }
};

bool vertex_builtin(Variable::Tag tag)
{
    return tag == Variable::Tag::VertexPosition || tag == Variable::Tag::VertexIndex ||
           tag == Variable::Tag::InstanceIndex || tag == Variable::Tag::DrawIndex;
}

bool fragment_builtin(Variable::Tag tag)
{
    return tag == Variable::Tag::FragmentCoord || tag == Variable::Tag::FrontFacing;
}

bool readonly_root(Variable::Tag tag)
{
    return tag == Variable::Tag::StageInput || tag == Variable::Tag::VertexIndex ||
           tag == Variable::Tag::InstanceIndex || tag == Variable::Tag::DrawIndex || fragment_builtin(tag);
}

class FunctionValidator final : public RasterWalker
{
private:
    struct Frame
    {
        const Function *function;
        std::unordered_map<uint32_t, const RefExpr *> references;
        bool has_value_return{false};
        bool has_discard{false};
    };
    const Function &entry_;
    vector<RasterDiagnostic> &diagnostics_;
    bool kernel_only_;
    bool position_written_{false};
    vector<Frame> frames_;
    std::unordered_set<const Function *> active_;

    const Function &current() const
    {
        return *frames_.back().function;
    }
    void diagnostic(RasterDiagnosticCode code, string message)
    {
        diagnostics_.push_back({code, describe(entry_) + " / " + describe(current()) + ": " + std::move(message)});
    }

    const RefExpr *root(const Expression *expr) const
    {
        if (!expr)
        {
            return nullptr;
        }
        if (expr->tag() == Expression::Tag::Member)
        {
            return root(static_cast<const MemberExpr *>(expr)->parent());
        }
        if (expr->tag() == Expression::Tag::Subscript)
        {
            return root(static_cast<const SubscriptExpr *>(expr)->range());
        }
        if (expr->tag() != Expression::Tag::Ref)
        {
            return nullptr;
        }
        auto *ref = static_cast<const RefExpr *>(expr);
        for (auto iter = frames_.rbegin(); iter != frames_.rend(); ++iter)
        {
            if (iter->function == ref->context())
            {
                if (auto mapped = iter->references.find(ref->variable().uid()); mapped != iter->references.end())
                {
                    return mapped->second;
                }
                break;
            }
        }
        return ref;
    }

    void on_write(const Expression *expression) override
    {
        if (kernel_only_)
        {
            return;
        }
        if (auto *ref = root(expression))
        {
            auto tag = ref->variable().tag();
            if (readonly_root(tag))
            {
                diagnostic(RasterDiagnosticCode::ReadOnlyWrite,
                           "write to readonly root variable " + std::to_string(ref->variable().uid()));
            }
            if (tag == Variable::Tag::VertexPosition)
            {
                position_written_ = true;
            }
        }
    }

    void on_return(const ReturnStmt *stmt) override
    {
        if (kernel_only_)
        {
            return;
        }
        const Type *actual = stmt->expression() ? stmt->expression()->type() : nullptr;
        frames_.back().has_value_return |= actual != nullptr;
        if (actual != current().return_type())
        {
            diagnostic(RasterDiagnosticCode::InvalidReturn, "return type differs from the declared type");
        }
    }

    void on_discard() override
    {
        // Discard terminates the fragment invocation, including its callers.
        for (auto &frame : frames_)
        {
            frame.has_discard = true;
        }
        if (!entry_.is_fragment())
        {
            diagnostic(RasterDiagnosticCode::InvalidBuiltinStage, "discard requires a fragment entry");
        }
    }

    void on_call(const CallExpr *call) override
    {
        if (const auto *callee = call->function())
        {
            Frame frame{callee, {}};
            auto parameters = callee->all_arguments();
            auto arguments = call->arguments();
            auto actual = arguments.begin();
            for (const auto &parameter : parameters)
            {
                if (actual == arguments.end())
                {
                    break;
                }
                if (parameter.tag() == Variable::Tag::Reference)
                {
                    frame.references.emplace(parameter.uid(), root(*actual));
                }
                ++actual;
            }
            check(std::move(frame));
            return;
        }
        if (kernel_only_)
        {
            return;
        }
        switch (call->call_op())
        {
            case CallOp::SynchronizeBlock:
            case CallOp::MakeRay:
            case CallOp::RayOffsetOrigin:
            case CallOp::TraceClosest:
            case CallOp::TraceOcclusion:
                diagnostic(RasterDiagnosticCode::InvalidBuiltinStage,
                           "compute synchronization or ray tracing operation in raster entry");
                break;
            case CallOp::AtomicExch:
            case CallOp::AtomicAdd:
            case CallOp::AtomicSub:
            case CallOp::AtomicCas:
                if (!call->arguments().empty())
                {
                    on_write(call->argument(0));
                }
                break;
            default:
                break;
        }
    }

    void check(Frame frame)
    {
        const auto &function = *frame.function;
        if (!active_.insert(&function).second)
        {
            return;
        }
        frames_.push_back(std::move(frame));
        if (!kernel_only_ && !function.captured_resources().empty())
        {
            diagnostic(RasterDiagnosticCode::PhysicalResourceCapture, "physical GPU resource capture is not supported");
        }
        if (!kernel_only_ && !function.body()->check_context(&function))
        {
            diagnostic(RasterDiagnosticCode::InterfaceMismatch, "AST nodes belong to a different function context");
        }
        if (!kernel_only_ && function.is_raster())
        {
            for (const auto &argument : function.arguments())
            {
                if (argument.tag() != Variable::Tag::StageInput)
                {
                    diagnostic(RasterDiagnosticCode::InvalidInterfaceType,
                               "raster parameters must be value stage inputs, not references");
                }
            }
        }
        for (const auto &builtin : function.builtin_vars())
        {
            auto tag = builtin.tag();
            bool vertex = vertex_builtin(tag);
            bool fragment = fragment_builtin(tag);
            if (kernel_only_ ? (vertex || fragment)
                             : !((vertex && entry_.is_vertex()) || (fragment && entry_.is_fragment())))
            {
                diagnostic(RasterDiagnosticCode::InvalidBuiltinStage, "builtin is not available in this entry stage");
            }
        }
        visit(function.body());
        if (!kernel_only_ && function.return_type() && !frames_.back().has_value_return && !frames_.back().has_discard)
        {
            diagnostic(RasterDiagnosticCode::InvalidReturn, "nonvoid function has no value return or fragment discard");
        }
        frames_.pop_back();
        active_.erase(&function);
    }

public:
    FunctionValidator(const Function &entry, vector<RasterDiagnostic> &diagnostics, bool kernel_only = false)
        : entry_(entry), diagnostics_(diagnostics), kernel_only_(kernel_only)
    {
    }
    void run()
    {
        check({&entry_, {}});
        if (!kernel_only_ && entry_.is_vertex() && !position_written_)
        {
            diagnostics_.push_back({RasterDiagnosticCode::MissingVertexPosition,
                                    describe(entry_) + ": no reachable write to vertex position"});
        }
    }
};

}  // namespace

RasterValidationError::RasterValidationError(horizon::core::vector<RasterDiagnostic> diagnostics)
    : std::invalid_argument(diagnostics.empty() ? "Invalid raster shader" : diagnostics.front().message),
      diagnostics_(std::move(diagnostics))
{
}

namespace detail
{
void reset_raster_hashes(const Function &function)
{
    HashResetter().reset(function);
}

vector<RasterDiagnostic> validate_raster_call_graph(const Function &function)
{
    vector<RasterDiagnostic> diagnostics;
    GraphValidator(diagnostics).check(function);
    return diagnostics;
}

vector<RasterDiagnostic> validate_kernel_raster_usage(const Function &function)
{
    vector<RasterDiagnostic> diagnostics;
    FunctionValidator(function, diagnostics, true).run();
    return diagnostics;
}
}  // namespace detail

horizon::core::vector<RasterDiagnostic> validate_raster_function(const Function &function)
{
    if (!function.is_raster())
    {
        return {{RasterDiagnosticCode::InvalidStage, "Expected a vertex or fragment entry: " + function.description()}};
    }
    auto diagnostics = detail::validate_raster_call_graph(function);
    if (!diagnostics.empty())
    {
        return diagnostics;
    }
    (void)detail::build_shader_interface(function, diagnostics);
    FunctionValidator(function, diagnostics).run();
    return diagnostics;
}

horizon::core::vector<RasterDiagnostic> validate_raster_pair(const Function &vertex, const Function &fragment)
{
    if (!vertex.is_vertex() || !fragment.is_fragment())
    {
        return {{RasterDiagnosticCode::InvalidStage, "Raster pair requires vertex then fragment entries"}};
    }
    auto diagnostics = validate_raster_function(vertex);
    auto fragment_diagnostics = validate_raster_function(fragment);
    diagnostics.insert(diagnostics.end(), fragment_diagnostics.begin(), fragment_diagnostics.end());
    const auto &outputs = vertex.shader_interface().outputs;
    const auto &inputs = fragment.shader_interface().inputs;
    auto mismatch = [&](string message)
    {
        diagnostics.push_back({RasterDiagnosticCode::InterfaceMismatch,
                               describe(vertex) + " -> " + describe(fragment) + ": " + std::move(message)});
    };
    auto arguments = fragment.arguments();
    if (vertex.return_type() ? (arguments.size() != 1 || arguments[0].type() != vertex.return_type())
                             : !arguments.empty())
    {
        mismatch("root 0: vertex return type must match the single fragment parameter (void requires no parameters)");
    }
    if (outputs.size() != inputs.size())
    {
        mismatch("root 0: varying leaf counts differ");
    }
    for (size_t i = 0; i < std::min(outputs.size(), inputs.size()); ++i)
    {
        const auto &output = outputs[i];
        const auto &input = inputs[i];
        if (output.type != input.type || output.root_index != 0 || input.root_index != 0 ||
            output.member_path != input.member_path || output.location != input.location ||
            output.interpolation != input.interpolation)
        {
            string path = "root 0";
            for (auto member : output.member_path)
            {
                path += "." + std::to_string(member);
            }
            mismatch(path + " location " + std::to_string(output.location) + ": varying leaf metadata differs");
        }
    }
    return diagnostics;
}

}  // namespace horizon::ast
