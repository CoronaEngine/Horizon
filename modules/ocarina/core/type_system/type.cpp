//
// Created by Zero on 30/04/2022.
//

#include "core/type.h"
#include "core/type_system/type_registry.h"

#include <cctype>
#include <utility>

#include "core/util/logging.h"
#include "core/util/string_util.h"

namespace ocarina {

namespace {
}// namespace

namespace detail {

struct TypeParser {
    [[nodiscard]] static bool is_letter(char ch) noexcept;
    [[nodiscard]] static bool is_letter_or_num(char ch) noexcept;
    [[nodiscard]] static bool is_num(char ch) noexcept;
    [[nodiscard]] static std::pair<int, int> bracket_matching_far(ocarina::string_view str, char l = '<', char r = '>') noexcept;
    [[nodiscard]] static std::pair<int, int> bracket_matching_near(ocarina::string_view str, char l = '<', char r = '>') noexcept;
    [[nodiscard]] static ocarina::string_view find_identifier(ocarina::string_view &str,
                                                              bool check_start_with_num = false) noexcept;
    [[nodiscard]] static ocarina::vector<ocarina::string_view> find_content(ocarina::string_view &str, char l = '<', char r = '>');
    [[nodiscard]] static const Type *parse_type_locked(ocarina::string_view desc) noexcept;
    static void parse_vector_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_matrix_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_struct_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_bindless_array_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_buffer_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_texture3d_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_texture2d_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_accel_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_byte_buffer_locked(Type *type, ocarina::string_view desc) noexcept;
    static void parse_array_locked(Type *type, ocarina::string_view desc) noexcept;
    [[nodiscard]] static const Type *add_type_locked(ocarina::unique_ptr<Type> type) noexcept;
};

[[nodiscard]] bool TypeParser::is_letter(char ch) noexcept {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

[[nodiscard]] bool TypeParser::is_letter_or_num(char ch) noexcept {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == ':';
}

[[nodiscard]] bool TypeParser::is_num(char ch) noexcept {
    return ch >= '0' && ch <= '9';
}

[[nodiscard]] std::pair<int, int> TypeParser::bracket_matching_far(ocarina::string_view str, char l, char r) noexcept {
    int start = 0;
    int end = 0;
    int pair_count = 0;
    for (int i = 0; i < str.size(); ++i) {
        char ch = str[i];
        if (ch == l) {
            if (pair_count == 0) {
                start = i;
            }
            pair_count += 1;
        } else if (ch == r) {
            pair_count -= 1;
            if (pair_count == 0) {
                end = i;
            }
        }
    }
    return std::make_pair(start, end);
}

[[nodiscard]] std::pair<int, int> TypeParser::bracket_matching_near(ocarina::string_view str, char l, char r) noexcept {
    int start = -1;
    int end = -1;
    int pair_count = 0;
    for (int i = 0; i < str.size(); ++i) {
        char ch = str[i];
        if (ch == l) {
            if (pair_count == 0) {
                start = i;
            }
            pair_count += 1;
        } else if (ch == r) {
            pair_count -= 1;
            if (pair_count == 0) {
                end = i;
            }
        }
        if (pair_count == 0 && start != -1 && end != -1) {
            break;
        }
    }
    return std::make_pair(start, end);
}

[[nodiscard]] ocarina::string_view TypeParser::find_identifier(ocarina::string_view &str,
                                                               bool check_start_with_num) noexcept {
    OC_USING_SV
    uint i = 0u;
    for (; i < str.size() && is_letter_or_num(str[i]); ++i) {
    }
    auto ret = str.substr(0, i);
    if (ret == "vector"sv ||
        ret == "matrix"sv ||
        ret == "struct"sv ||
        ret == "buffer"sv ||
        ret == "texture3d"sv ||
        ret == "array"sv) {
        auto [start, end] = bracket_matching_near(str);
        ret = str.substr(0, end + 1);
        str = str.substr(end + 1);
    } else {
        str = str.substr(i);
    }
    if (!ret.empty() && is_num(ret[0]) && check_start_with_num) [[unlikely]] {
        OC_ERROR_FORMAT("invalid identifier {} !", ret)
    }
    return ret;
}

[[nodiscard]] ocarina::vector<ocarina::string_view> TypeParser::find_content(ocarina::string_view &str, char l, char r) {
    ocarina::vector<ocarina::string_view> ret;
    auto prev_token = str.find(l);
    constexpr auto token = ',';
    str = str.substr(prev_token + 1);
    uint count = 0;
    constexpr uint limit = 10000;
    while (true) {
        auto content = find_identifier(str);
        auto new_cursor = str.find(token) + 1;
        str = str.substr(new_cursor);
        ++count;
        if (count > limit) {
            OC_ERROR("The number of loops has exceeded the upper limit. Please check the code");
        }
        ret.push_back(content);
        if (str[0] == r) {
            break;
        }
    }
    return ret;
}

void TypeParser::parse_vector_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::VECTOR;
    auto [start, end] = bracket_matching_far(desc, '<', '>');
    auto content = desc.substr(start + 1, end - start - 1);
    auto lst = string_split(content, ',');
    OC_ASSERT(lst.size() == 2);
    auto type_str = lst[0];
    auto dimension_str = lst[1];
    auto dimension = std::stoi(string(dimension_str));
    type->dimension_ = dimension;
    type->members_.push_back(parse_type_locked(type_str));
    auto member = type->members_.front();
    if (!member->is_scalar()) [[unlikely]] {
        OC_ERROR("invalid vector element: {}!", member->description());
    }
    type->size_ = member->size() * (dimension == 3 ? 4 : dimension);
    type->alignment_ = type->size();
}

void TypeParser::parse_matrix_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::MATRIX;
    auto [start, end] = bracket_matching_far(desc, '<', '>');
    auto dimension_str = desc.substr(start + 1, end - start - 1);
    auto data = string_split(dimension_str, ',');
    auto type_str = data[0];
    int N = std::stoi(string(data[1]));
    int M = std::stoi(string(data[2]));
    type->dimension_ = M;
    auto tmp_desc = ocarina::format("vector<{},{}>", type_str, N);
    type->members_.push_back(parse_type_locked(tmp_desc));

#define OC_SIZE_ALIGN(TypeName, NN, MM)                       \
    if (#TypeName == type_str && N == (NN) && M == (MM)) {    \
        type->size_ = sizeof(Matrix<TypeName, NN, MM>);       \
        type->alignment_ = alignof(Matrix<TypeName, NN, MM>); \
    } else

#define OC_SIZE_ALIGN_FOR_TYPE(type_name) \
    OC_SIZE_ALIGN(type_name, 2, 2)        \
    OC_SIZE_ALIGN(type_name, 2, 3)        \
    OC_SIZE_ALIGN(type_name, 2, 4)        \
    OC_SIZE_ALIGN(type_name, 3, 2)        \
    OC_SIZE_ALIGN(type_name, 3, 3)        \
    OC_SIZE_ALIGN(type_name, 3, 4)        \
    OC_SIZE_ALIGN(type_name, 4, 2)        \
    OC_SIZE_ALIGN(type_name, 4, 3)        \
    OC_SIZE_ALIGN(type_name, 4, 4)

    OC_SIZE_ALIGN_FOR_TYPE(float)
    OC_SIZE_ALIGN_FOR_TYPE(real)
    OC_SIZE_ALIGN_FOR_TYPE(half) {
        OC_ERROR("invalid matrix dimension <{}, {}>!", N, M);
    }

#undef OC_SIZE_ALIGN_FOR_TYPE
#undef OC_SIZE_ALIGN
}

void TypeParser::parse_struct_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::STRUCTURE;
    auto lst = find_content(desc);
    type->cname_ = lst[0];
    auto alignment_str = lst[1];
    bool is_builtin_struct = lst[2] == "true";
    type->builtin_struct_ = is_builtin_struct;
    bool is_param_struct = lst[3] == "true";
    type->param_struct_ = is_param_struct;
    auto alignment = std::stoi(string(alignment_str));
    type->alignment_ = alignment;
    auto size = 0u;
    static constexpr uint member_offset = 4;
    for (int i = member_offset; i < lst.size(); ++i) {
        auto type_str = lst[i];
        type->members_.push_back(parse_type_locked(type_str));
        auto member = type->members_[i - member_offset];
        size = detail::mem_offset(size, member->alignment());
        size += member->size();
    }
    type->size_ = detail::mem_offset(size, type->alignment());
}

void TypeParser::parse_bindless_array_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::BINDLESS_ARRAY;
    type->alignment_ = alignof(BindlessArrayDesc);
}

void TypeParser::parse_buffer_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::BUFFER;
    auto lst = find_content(desc);
    OC_ERROR_IF_NOT(lst.size() == 1u,
                    "multidimensional buffer type is unsupported: ",
                    desc);
    auto type_str = lst[0];
    const Type *element_type = parse_type_locked(type_str);
    type->members_.push_back(element_type);
    type->alignment_ = alignof(BufferDesc<>);
    type->size_ = sizeof(BufferDesc<>);
}

void TypeParser::parse_texture3d_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::TEXTURE3D;
    type->alignment_ = alignof(TextureDesc);
    type->size_ = sizeof(TextureDesc);
}

void TypeParser::parse_texture2d_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::TEXTURE2D;
    type->alignment_ = alignof(TextureDesc);
    type->size_ = sizeof(TextureDesc);
}

void TypeParser::parse_accel_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::ACCEL;
}

void TypeParser::parse_byte_buffer_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::BYTE_BUFFER;
    type->alignment_ = alignof(BufferDesc<>);
}

void TypeParser::parse_array_locked(Type *type, ocarina::string_view desc) noexcept {
    type->tag_ = Type::Tag::ARRAY;
    auto lst = find_content(desc);
    auto type_str = lst[0];
    auto len = std::stoi(string(lst[1]));
    const Type *element_type = parse_type_locked(type_str);
    type->members_.push_back(element_type);
    type->alignment_ = element_type->alignment();
    type->dimension_ = len;
    type->size_ = element_type->size() * len;
}

const Type *TypeParser::add_type_locked(ocarina::unique_ptr<Type> type) noexcept {
    auto &registry = type_registry();
    type->index_ = registry.count();
    const Type *ret = registry.emplace_type(ocarina::move(type));
    notify_type_access(ret);
    return ret;
}

const Type *TypeParser::parse_type_locked(ocarina::string_view desc) noexcept {
    if (desc == "void") {
        return nullptr;
    }
    auto &registry = type_registry();
    uint64_t hash = compute_type_hash(desc);
    if (const auto *type = registry.find_by_hash(hash); type != nullptr) {
        notify_type_access(type);
        return type;
    }

    OC_USING_SV
    auto type = ocarina::make_unique<Type>();

#define OC_PARSE_BASIC_TYPE(T, TAG)    \
    if (desc == #T##sv) {              \
        type->size_ = sizeof(T);       \
        type->tag_ = Type::Tag::TAG;   \
        type->alignment_ = alignof(T); \
        type->dimension_ = 1;          \
    } else

    OC_PARSE_BASIC_TYPE(int, INT)
    OC_PARSE_BASIC_TYPE(uint, UINT)
    OC_PARSE_BASIC_TYPE(bool, BOOL)
    OC_PARSE_BASIC_TYPE(float, FLOAT)
    OC_PARSE_BASIC_TYPE(real, REAL)
    OC_PARSE_BASIC_TYPE(half, HALF)
    OC_PARSE_BASIC_TYPE(uchar, UCHAR)
    OC_PARSE_BASIC_TYPE(char, CHAR)
    OC_PARSE_BASIC_TYPE(ushort, USHORT)
    OC_PARSE_BASIC_TYPE(ulong, ULONG)
    OC_PARSE_BASIC_TYPE(short, SHORT)

#undef OC_PARSE_BASIC_TYPE

    if (desc.starts_with("vector")) {
        parse_vector_locked(type.get(), desc);
    } else if (desc.starts_with("matrix")) {
        parse_matrix_locked(type.get(), desc);
    } else if (desc.starts_with("array")) {
        parse_array_locked(type.get(), desc);
    } else if (desc.starts_with("struct")) {
        parse_struct_locked(type.get(), desc);
    } else if (desc.starts_with("bytebuffer")) {
        parse_byte_buffer_locked(type.get(), desc);
    } else if (desc.starts_with("buffer")) {
        parse_buffer_locked(type.get(), desc);
    } else if (desc.starts_with("texture3d")) {
        parse_texture3d_locked(type.get(), desc);
    } else if (desc.starts_with("texture2d")) {
        parse_texture2d_locked(type.get(), desc);
    } else if (desc.starts_with("accel")) {
        parse_accel_locked(type.get(), desc);
    } else if (desc.starts_with("bindlessArray")) {
        parse_bindless_array_locked(type.get(), desc);
    } else {
        OC_ERROR("invalid data type ", desc);
    }
    type->set_description(desc);
    return add_type_locked(ocarina::move(type));
}

}// namespace detail

namespace detail {

const Type *parse_type_locked(ocarina::string_view desc) noexcept {
    return detail::TypeParser::parse_type_locked(desc);
}

}// namespace detail

size_t Type::count() noexcept {
    return type_registry().count();
}

const Type *Type::from(std::string_view description) noexcept {
    return type_registry().parse_type(description);
}

const Type *Type::resolve(const Type *type,
                          StoragePrecisionPolicy policy) noexcept {
    return type_registry().resolve_type(type, policy);
}

const Type *Type::at(uint32_t uid) noexcept {
    return type_registry().at(uid);
}

bool Type::is_dynamic() const noexcept {
    switch (tag_) {
        case Tag::ARRAY:
        case Tag::VECTOR:
        case Tag::MATRIX:
        case Tag::BUFFER:
        case Tag::STRUCTURE: {
            return std::ranges::any_of(members_, [](const Type *member) {
                return member->is_dynamic();
            });
        }
        case Tag::REAL:
            return true;
        default:
            return false;
    }
}

bool Type::exists(ocarina::string_view description) noexcept {
    return exists(compute_type_hash(description));
}

bool Type::exists(uint64_t hash) noexcept {
    return type_registry().exists(hash);
}

ocarina::span<const Type *const> Type::members() const noexcept {
    return {members_};
}

const Type *Type::element() const noexcept {
    return members_.front();
}

void Type::set_cname(std::string s) const noexcept {
    cname_ = ocarina::move(s);
}

ocarina::string Type::simple_cname() const noexcept {
    return cname_.substr(cname_.find_last_of("::") + 1);
}

bool Type::is_valid() const noexcept {
    switch (tag_) {
        case Tag::STRUCTURE: {
            bool ret = true;
            for (auto member : members_) {
                ret = ret && member->is_valid();
            }
            return ret;
        }
        case Tag::ARRAY: return dimension() > 0;
        default: return true;
    }
}

const Type *Type::get_member(ocarina::string_view name) const noexcept {
    for (int i = 0; i < member_name_.size(); ++i) {
        if (member_name_[i] == name) {
            return members_[i];
        }
    }
    return nullptr;
}

size_t Type::max_member_size() const noexcept {
    switch (tag_) {
        case Tag::BOOL:
        case Tag::FLOAT:
        case Tag::INT:
        case Tag::UINT:
        case Tag::UCHAR:
        case Tag::CHAR: return size();
        case Tag::VECTOR:
        case Tag::MATRIX:
        case Tag::ARRAY: return element()->max_member_size();
        case Tag::STRUCTURE: {
            size_t size = 0;
            for (const Type *member : members_) {
                if (member->max_member_size() > size) {
                    size = member->max_member_size();
                }
            }
            return size;
        }
        default:
            return 0;
    }
}

void Type::for_each(TypeVisitor *visitor) {
    auto snapshot = type_registry().snapshot();
    for (const Type *type : snapshot) {
        visitor->visit(type);
    }
}

uint64_t Type::compute_hash() const noexcept {
    return hash64(description_);
}

void Type::set_description(ocarina::string_view desc) noexcept {
    description_ = desc;
    update_name(desc);
}

void Type::update_member_name(const string_view *names, int num) const noexcept {
    member_name_.clear();
    for (int i = 0; i < num; ++i) {
        member_name_.push_back(names[i]);
    }
}

void Type::update_name(ocarina::string_view desc) noexcept {
    switch (tag_) {
        case Tag::NONE:
            OC_ASSERT(0);
            break;
        case Tag::VECTOR:
            name_ = ocarina::format("{}{}", element()->name(), dimension());
            break;
        case Tag::MATRIX:
            name_ = ocarina::format("{}{}x{}", element()->element()->name(),
                                    element()->dimension(), dimension());
            break;
        default:
            name_ = desc;
            break;
    }
}

}// namespace ocarina