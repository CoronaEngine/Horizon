//
// Created by Zero on 2022/7/30.
//

#include "variable.h"
#include "symbol_name.h"
#include "function.h"

namespace ocarina {

Variable::Variable(const Function *context,
                   const Type *type,
                   Variable::Tag tag,
                   uint uid, std::string name,
                   std::string suffix) noexcept
    : context_(context), type_(type), uid_(uid) {
    set_tag(tag);
    set_suffix(std::move(suffix));
    set_name(std::move(name));
}

uint64_t Variable::compute_hash() const noexcept {
    auto u0 = static_cast<uint64_t>(uid_);
    auto u1 = static_cast<uint64_t>(tag());
    uint64_t ret = hash64(u0 | (u1 << 32u), type()->hash());
    if (!name().empty()) {
        ret = hash64(ret, name());
    }
    if (!suffix().empty()) {
        ret = hash64(ret, suffix());
    }
    return ret;
}

string Variable::suffix() const noexcept {
    return context_->variable_data(uid_).suffix;
}

string Variable::name() const noexcept {
    return context_->variable_data(uid_).name;
}

void Variable::set_name(string name) noexcept {
    const_cast<Function *>(context_)->variable_data(uid_).name = std::move(name);
}

void Variable::set_suffix(std::string suffix) noexcept {
    const_cast<Function *>(context_)->variable_data(uid_).suffix = std::move(suffix);
}

void Variable::mark_usage(ocarina::Usage usage) const noexcept {
    const_cast<Function *>(context_)->mark_variable_usage(uid_, usage);
}

void Variable::mark_used(bool used) const noexcept {
    const_cast<Function *>(context_)->variable_data(uid_).used = used;
}

bool Variable::used() const noexcept {
    return context_->variable_data(uid_).used;
}

Usage Variable::usage() const noexcept {
    return context_->variable_data(uid_).usage;
}

Variable::Tag Variable::tag() const noexcept {
    return context_->variable_data(uid_).tag;
}

void Variable::set_tag(ocarina::Variable::Tag tag) noexcept {
    const_cast<Function *>(context_)->variable_data(uid_).tag = tag;
}

string Variable::final_name() const noexcept {
    string raw_name = string(detail::variable_prefix(tag())) + detail::to_string(uid_);
    if (!name().empty()) { return name(); }
    if (!suffix().empty()) {
        return raw_name + "_" + suffix();
    }
    return raw_name;
}

}// namespace ocarina