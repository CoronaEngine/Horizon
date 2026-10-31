//
// Created by Zero on 06/06/2022.
//

#pragma once

#include "core/header.h"
#include "core/stl.h"
#include "dsl/api/func.h"
#include "core/util/logging.h"
#include "ast/variable.h"
#include "ast/symbol_name.h"

namespace ocarina {

namespace detail {

struct LiteralPrinter;

}// namespace detail

class OC_GENERATOR_API SourceEmitter {
protected:
    class OC_GENERATOR_API Scratch {
    private:
        ocarina::string buffer_;

    public:
        Scratch() = default;
        explicit Scratch(const string_view &str) noexcept { buffer_ = str; }
        Scratch &operator<<(ocarina::string_view v) noexcept;
        Scratch &operator<<(const ocarina::string &v) noexcept;
        Scratch &operator<<(const char *v) noexcept;
        Scratch &operator<<(int v) noexcept;
        Scratch &operator<<(float v) noexcept;
        Scratch &operator<<(bool v) noexcept;
        Scratch &operator<<(half v) noexcept;
        Scratch &operator<<(uint v) noexcept;
        Scratch &operator<<(size_t v) noexcept;
        Scratch &operator<<(const Scratch &scratch) noexcept;
        void replace(string_view substr, string_view new_str) noexcept;
        void clear() noexcept;
        void pop_back() noexcept;
        [[nodiscard]] const char *c_str() const noexcept;
        [[nodiscard]] ocarina::string_view view() const noexcept;
        [[nodiscard]] bool empty() const noexcept;
        [[nodiscard]] size_t size() const noexcept;
    };

private:
    int indent_{};
    Scratch scratch_;
    ocarina::vector<Scratch *> scratch_stack_;
    ocarina::vector<const Function *> func_stack_;

protected:
    bool obfuscation_{false};

protected:
    void indent_inc() noexcept { indent_ += 1; }
    void indent_dec() noexcept { indent_ -= 1; }
    void push(Scratch &scratch) noexcept { scratch_stack_.push_back(&scratch); }
    void pop(Scratch &scratch) noexcept {
        Scratch *back = scratch_stack_.back();
        if (&scratch != back) [[unlikely]] {
            OC_ERROR("Invalid scratch !");
        }
        scratch_stack_.pop_back();
    }
    Scratch &current_scratch() noexcept { return *scratch_stack_.back(); }
    void push(const Function &function) noexcept { func_stack_.push_back(&function); }
    void pop(const Function &function) noexcept {
        const Function *back = func_stack_.back();
        if (&function != back) [[unlikely]] {
            OC_ERROR("Invalid scratch !");
        }
        func_stack_.pop_back();
    }
    const Function &current_function() noexcept { return *func_stack_.back(); }
    friend struct detail::LiteralPrinter;

    template<typename T>
    class Guard {
    private:
        T &_val;
        SourceEmitter *_codegen{};

    public:
        Guard(SourceEmitter *codegen, T &val)
            : _codegen(codegen), _val(val) {
            _codegen->push(_val);
        }
        ~Guard() {
            _codegen->pop(_val);
        }
    };
#define SCRATCH_GUARD(scratch) Guard<Scratch> __##scratch_guard(this, scratch);
#define FUNCTION_GUARD(function) Guard<const Function> __##function_guard(this, function);

protected:
    virtual void _emit_newline() noexcept;
    virtual void _emit_indent() noexcept;
    virtual void _emit_space() noexcept;

    virtual void _emit_func_name(const Function &f) noexcept;
    virtual void _emit_struct_name(const Type *type) noexcept;
    virtual void _emit_comment(const string &content) noexcept = 0;
    virtual void _emit_member_name(const Type *type, int index) noexcept;

public:
    explicit SourceEmitter(bool obfuscation) : obfuscation_(obfuscation) { push(scratch_); }
    explicit SourceEmitter(Scratch &scratch)
        : scratch_(scratch) {
        push(scratch_);
    }
    virtual void emit(const Function &func) = 0;
    Scratch &scratch() {
        return scratch_;
    }
};

}// namespace ocarina