#include "dsl/dsl.h"

#include <iostream>

namespace {

bool has_terminating_bounds_check(const horizon::dsl::Kernel<void()> &kernel) {
    using horizon::ast::IfStmt;
    using horizon::ast::Statement;

    const auto &function = kernel.function();
    if (function == nullptr || !function->body()->check_context(function.get())) {
        return false;
    }

    const IfStmt *bounds_check = nullptr;
    for (const Statement *statement : function->body()->statements()) {
        if (statement->tag() == Statement::Tag::IF) {
            bounds_check = static_cast<const IfStmt *>(statement);
            break;
        }
    }
    if (bounds_check == nullptr) {
        return false;
    }

    for (const Statement *statement : bounds_check->true_branch()->statements()) {
        if (statement->tag() == Statement::Tag::RETURN) {
            return true;
        }
    }
    return false;
}

}// namespace

int main() {
    using horizon::dsl::Kernel;
    using horizon::dsl::Uint;

    Kernel static_size_kernel{[] {
        Uint index = 2u;
        const auto corrected = horizon::dsl::detail::correct_index(
            index, 1u, "static size boundary test", "test traceback");
        (void)corrected;
    }};

    Kernel dynamic_size_kernel{[] {
        Uint index = 2u;
        Uint size = 1u;
        const auto corrected = horizon::dsl::detail::correct_index(
            index, size, "dynamic size boundary test", "test traceback");
        (void)corrected;
    }};

    if (!has_terminating_bounds_check(static_size_kernel) ||
        !has_terminating_bounds_check(dynamic_size_kernel)) {
        std::cerr << "FAIL: correct_index did not emit a valid terminating bounds check\n";
        return 1;
    }
    return 0;
}
