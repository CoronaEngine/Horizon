//
// Created by Z on 2026/4/21.
//

#pragma once

#include "ast/function.h"

namespace ocarina {

class IRFunction {
private:
    const Function *ast_function_{};

public:
    explicit IRFunction(const Function *ast_function) noexcept
        : ast_function_(ast_function) {}

    [[nodiscard]] const Function &ast_function() const noexcept { return *ast_function_; }
    [[nodiscard]] uint64_t hash() const noexcept { return ast_function_->hash(); }
};

class IRModule {
private:
    const Function *entry_function_{};
    ocarina::vector<const Type *> structures_;
    ocarina::vector<IRFunction> functions_;

public:
    explicit IRModule(const Function *entry_function = nullptr) noexcept
        : entry_function_(entry_function) {}

    void set_entry_function(const Function *entry_function) noexcept { entry_function_ = entry_function; }
    [[nodiscard]] const Function &entry_function() const noexcept {
        OC_ASSERT(entry_function_ != nullptr);
        return *entry_function_;
    }
    [[nodiscard]] bool empty() const noexcept { return functions_.empty(); }

    void add_structure(const Type *type) noexcept;
    void add_function(const Function *function) noexcept;

    [[nodiscard]] span<const Type *const> structures() const noexcept { return structures_; }
    [[nodiscard]] span<const IRFunction> functions() const noexcept { return functions_; }
};

}// namespace ocarina