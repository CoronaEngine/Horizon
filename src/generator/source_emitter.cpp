//
// Created by Zero on 06/06/2022.
//

#include "source_emitter.h"

namespace ocarina {

SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(int v) noexcept {
    return *this << detail::to_string(v);
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(float v) noexcept {
    auto s = detail::to_string(v);
    *this << s;
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) {
        *this << ".f";
    } else if (s.find('.') != std::string::npos || s.find('e') != std::string::npos) {
        *this << "f";
    }
    return *this;
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(bool v) noexcept {
    return *this << detail::to_string(v);
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(ocarina::half v) noexcept {
    return *this << to_str(v);
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(ocarina::string_view v) noexcept {
    buffer_.append(v);
    return *this;
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(const ocarina::string &v) noexcept {
    return *this << string_view{v};
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(const char *v) noexcept {
    return *this << string_view{v};
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(const SourceEmitter::Scratch &scratch) noexcept {
    return *this << scratch.c_str();
}

void SourceEmitter::Scratch::clear() noexcept {
    buffer_.clear();
}
bool SourceEmitter::Scratch::empty() const noexcept {
    return buffer_.empty();
}
const char *SourceEmitter::Scratch::c_str() const noexcept {
    return buffer_.c_str();
}
size_t SourceEmitter::Scratch::size() const noexcept {
    return buffer_.size();
}
ocarina::string_view SourceEmitter::Scratch::view() const noexcept {
    return buffer_;
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(uint v) noexcept {
    return *this << detail::to_string(v) + "u";
}
SourceEmitter::Scratch &SourceEmitter::Scratch::operator<<(size_t v) noexcept {
    return *this << detail::to_string(v) + "ul";
}
void SourceEmitter::Scratch::pop_back() noexcept {
    buffer_.pop_back();
}

void SourceEmitter::Scratch::replace(string_view substr, string_view new_str) noexcept {
    auto begin = buffer_.find(substr);
    auto size = substr.size();
    buffer_.replace(begin, size, new_str);
}

void SourceEmitter::_emit_newline() noexcept {
    if (obfuscation_) {
        return;
    }
    current_scratch() << "\n";
}
void SourceEmitter::_emit_indent() noexcept {
    if (obfuscation_) {
        return;
    }
    static constexpr auto indent_str = "    ";
    for (int i = 0; i < indent_; ++i) {
        current_scratch() << indent_str;
    }
}
void SourceEmitter::_emit_space() noexcept {
    current_scratch() << " ";
}

void SourceEmitter::_emit_func_name(const Function &f) noexcept {
    current_scratch() << f.func_name();
}

void SourceEmitter::_emit_struct_name(const Type *type) noexcept {
    current_scratch() << detail::struct_name(type->hash());
    _emit_comment(ocarina::format("{} : size = {}", type->cname(), type->size()));
}

void SourceEmitter::_emit_member_name(const Type *type, int index) noexcept {
    const auto &member_name = type->member_name();
    if (member_name.empty() || !type->is_builtin_struct()) {
        current_scratch() << detail::member_name(index);
    } else {
        current_scratch() << member_name[index];
    }
}

}// namespace ocarina