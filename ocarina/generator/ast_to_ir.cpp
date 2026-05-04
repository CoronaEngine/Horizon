//
// Created by Z on 2026/4/21.
//

#include "ast_to_ir.h"

namespace ocarina {

void IRModule::add_structure(const Type *type) noexcept {
    if (type == nullptr) {
        return;
    }
    if (std::find(structures_.begin(), structures_.end(), type) != structures_.end()) {
        return;
    }
    structures_.push_back(type);
}

void IRModule::add_function(const Function *function) noexcept {
    if (function == nullptr) {
        return;
    }
    auto iter = std::find_if(functions_.begin(), functions_.end(), [&](const IRFunction &item) {
        return item.hash() == function->hash();
    });
    if (iter != functions_.end()) {
        return;
    }
    functions_.emplace_back(function);
}

void AstToIR::_lower_function(const Function &function, IRModule &module) const noexcept {
    function.for_each_custom_func([&](const Function *callee) {
        _lower_function(*callee, module);
    });
    function.for_each_structure([&](const Type *type) {
        module.add_structure(type);
    });
    module.add_function(&function);
}

IRModule AstToIR::lower(const Function &function) const noexcept {
    IRModule module{&function};
    _lower_function(function, module);
    return module;
}

}// namespace ocarina