//
// Created during CUDA→Slang+Vulkan migration.
// Replaces ast_to_cuda_source.cpp — same virtual hook structure, Slang/HLSL targets.
//

#include "ast_to_slang_source.h"
#include "ast/expression.h"
#include "core/util/util.h"
#include "core/type_system/precision_policy.h"
#include "core/type_system/type_desc.h"
#include "vk_compute_device.h"
#include <cmath>

namespace ocarina {

namespace {

[[nodiscard]] string_view precision_policy_name(PrecisionPolicy policy) noexcept {
    switch (policy) {
        case PrecisionPolicy::force_f16: return "force_f16";
        case PrecisionPolicy::force_f32: return "force_f32";
    }
    return "unknown";
}

struct SlangScalarName {
    string_view ocarina;
    string_view slang;
};

// Scalar prefixes of ocarina type names ("half3", "uchar2", "float4x4", ...).
// No entry is a prefix of another, so lookup order does not matter.
constexpr std::array<SlangScalarName, 12> slang_scalar_names{{
    {"bool", "bool"},
    {"float", "float"},
    {"real", "float"},
    {"half", "float16_t"},
    {"int", "int"},
    {"uint", "uint"},
    {"uchar", "uint8_t"},
    {"char", "int8_t"},
    {"ushort", "uint16_t"},
    {"short", "int16_t"},
    {"ulong", "uint64_t"},
    {"long", "int64_t"},
}};

// Map an ocarina scalar/vector/matrix type name to its Slang spelling.
// bool/int/uint/float keep the HLSL shorthand (float3, float4x4); every other
// scalar uses vector<T, N> / matrix<T, R, C> so no non-standard alias is needed.
[[nodiscard]] string slang_type_name(string_view name) noexcept {
    for (const auto &entry : slang_scalar_names) {
        if (!name.starts_with(entry.ocarina)) continue;
        string_view shape = name.substr(entry.ocarina.size());
        if (shape.empty()) return string(entry.slang);
        if (entry.slang == "bool" || entry.slang == "int" ||
            entry.slang == "uint" || entry.slang == "float") {
            return string(entry.slang) + string(shape);
        }
        auto x = shape.find('x');
        if (x == string_view::npos) {
            return ocarina::format("vector<{}, {}>", entry.slang, shape);
        }
        return ocarina::format("matrix<{}, {}, {}>", entry.slang,
                               shape.substr(0, x), shape.substr(x + 1));
    }
    return string(name);
}

// Host packs bool/8-bit/16-bit kernel arguments at byte granularity, which a
// typed physical-storage pointer cannot address portably (Slang bool is 4 bytes,
// 8/16-bit storage is optional). Such scalars are extracted from the enclosing
// 32-bit word instead. Returns an empty string for every other type.
[[nodiscard]] string narrow_scalar_load(const Type *type, size_t offset) noexcept {
    size_t width = 0;
    switch (type->tag()) {
        case Type::Tag::BOOL:
        case Type::Tag::UCHAR:
        case Type::Tag::CHAR: width = 8; break;
        case Type::Tag::USHORT:
        case Type::Tag::SHORT:
        case Type::Tag::HALF: width = 16; break;
        default: return {};
    }
    string bits = ocarina::format("oc_param_bits({}u, {}u)", offset / 4 * 4, offset % 4 * 8);
    uint mask = width == 8 ? 0xffu : 0xffffu;
    switch (type->tag()) {
        case Type::Tag::BOOL:
            return ocarina::format("(({} & 0x{:x}u) != 0u)", bits, mask);
        case Type::Tag::UCHAR:
            return ocarina::format("uint8_t({} & 0x{:x}u)", bits, mask);
        case Type::Tag::USHORT:
            return ocarina::format("uint16_t({} & 0x{:x}u)", bits, mask);
        case Type::Tag::CHAR:
            return ocarina::format("int8_t((int({}) << {}) >> {})", bits, 32 - width, 32 - width);
        case Type::Tag::SHORT:
            return ocarina::format("int16_t((int({}) << {}) >> {})", bits, 32 - width, 32 - width);
        case Type::Tag::HALF:
            return ocarina::format("float16_t(f16tof32({} & 0x{:x}u))", bits, mask);
        default: return {};
    }
}

}// namespace

// ── visit(CallExpr) ──────────────────────────────────────────────────────────

void AstToSlangSource::visit(const CallExpr *expr) noexcept {
    auto emit_args = [this](const CallExpr *e) {
        current_scratch() << "(";
        for (const auto &arg : e->arguments()) {
            arg->accept(*this);
            current_scratch() << ",";
        }
        if (!e->arguments().empty()) current_scratch().pop_back();
        current_scratch() << ")";
    };

    string_view func_name = expr->function_name();
    if (!func_name.empty()) {
        current_scratch() << func_name;
        emit_args(expr);
        return;
    }

// Slang native builtins keep their name; oc_ wrappers live in slang_device_math.slang.
// Constructors are emitted from the resolved result type, so MAKE_REAL* follows the
// storage precision policy (float16_t under force_f16) instead of a fixed spelling.
#define OC_SLANG_FUNC(name) current_scratch() << #name
#define OC_SLANG_WRAP(name) current_scratch() << "oc_" #name
#define OC_SLANG_CTOR       _emit_type_name(expr->type())
    switch (expr->call_op()) {
        case CallOp::CUSTOM: AstToCppSource::visit(expr); return;
        case CallOp::ALL:              OC_SLANG_FUNC(all);             break;
        case CallOp::ANY:              OC_SLANG_FUNC(any);             break;
        case CallOp::NONE:             OC_SLANG_WRAP(none);            break;
        case CallOp::SELECT:           OC_SLANG_FUNC(select);          break;
        case CallOp::CLAMP:            OC_SLANG_FUNC(clamp);           break;
        // ocarina lerp is pbrt-style (t, a, b); Slang lerp is (a, b, t).
        case CallOp::LERP:             OC_SLANG_WRAP(lerp);            break;
        case CallOp::INVERSE_LERP:     OC_SLANG_WRAP(inverse_lerp);    break;
        case CallOp::ABS:              OC_SLANG_FUNC(abs);             break;
        case CallOp::MIN:              OC_SLANG_FUNC(min);             break;
        case CallOp::MAX:              OC_SLANG_FUNC(max);             break;
        case CallOp::IS_INF:           OC_SLANG_FUNC(isinf);           break;
        case CallOp::IS_NAN:           OC_SLANG_FUNC(isnan);           break;
        case CallOp::ACOS:             OC_SLANG_FUNC(acos);            break;
        case CallOp::ASIN:             OC_SLANG_FUNC(asin);            break;
        case CallOp::ATAN:             OC_SLANG_FUNC(atan);            break;
        case CallOp::ACOSH:            OC_SLANG_FUNC(acosh);           break;
        case CallOp::ASINH:            OC_SLANG_FUNC(asinh);           break;
        case CallOp::ATANH:            OC_SLANG_FUNC(atanh);           break;
        case CallOp::ATAN2:            OC_SLANG_FUNC(atan2);           break;
        case CallOp::COPYSIGN:         OC_SLANG_WRAP(copysign);        break;
        case CallOp::COS:              OC_SLANG_FUNC(cos);             break;
        case CallOp::SIN:              OC_SLANG_FUNC(sin);             break;
        case CallOp::TAN:              OC_SLANG_FUNC(tan);             break;
        case CallOp::SINH:             OC_SLANG_FUNC(sinh);            break;
        case CallOp::COSH:             OC_SLANG_FUNC(cosh);            break;
        case CallOp::TANH:             OC_SLANG_FUNC(tanh);            break;
        case CallOp::EXP:              OC_SLANG_FUNC(exp);             break;
        case CallOp::EXP2:             OC_SLANG_FUNC(exp2);            break;
        case CallOp::EXP10:            OC_SLANG_WRAP(exp10);           break;
        case CallOp::LOG:              OC_SLANG_FUNC(log);             break;
        case CallOp::LOG2:             OC_SLANG_FUNC(log2);            break;
        case CallOp::LOG10:            OC_SLANG_FUNC(log10);           break;
        case CallOp::POW:              OC_SLANG_FUNC(pow);             break;
        case CallOp::SQRT:             OC_SLANG_FUNC(sqrt);            break;
        case CallOp::RSQRT:            OC_SLANG_FUNC(rsqrt);           break;
        case CallOp::CEIL:             OC_SLANG_FUNC(ceil);            break;
        case CallOp::FLOOR:            OC_SLANG_FUNC(floor);           break;
        case CallOp::ROUND:            OC_SLANG_FUNC(round);           break;
        case CallOp::FMA:              OC_SLANG_FUNC(mad);             break;
        case CallOp::CROSS:            OC_SLANG_FUNC(cross);           break;
        case CallOp::DOT:              OC_SLANG_FUNC(dot);             break;
        case CallOp::LENGTH:           OC_SLANG_FUNC(length);          break;
        case CallOp::LENGTH_SQUARED:   OC_SLANG_WRAP(length_squared);  break;
        case CallOp::DISTANCE:         OC_SLANG_FUNC(distance);        break;
        case CallOp::DISTANCE_SQUARED: OC_SLANG_WRAP(distance_squared); break;
        case CallOp::NORMALIZE:        OC_SLANG_FUNC(normalize);       break;
        // ocarina face_forward differs from HLSL faceforward in arity and sign.
        case CallOp::FACE_FORWARD:     OC_SLANG_WRAP(face_forward);    break;
        case CallOp::COORDINATE_SYSTEM:   OC_SLANG_WRAP(coordinate_system);   break;
        case CallOp::MAKE_NORMAL_TANGENT: OC_SLANG_WRAP(make_normal_tangent); break;
        case CallOp::DETERMINANT:      OC_SLANG_FUNC(determinant);     break;
        case CallOp::TRANSPOSE:        OC_SLANG_FUNC(transpose);       break;
        case CallOp::INVERSE:          OC_SLANG_WRAP(inverse);         break;
        case CallOp::SQR:              OC_SLANG_WRAP(sqr);             break;
        case CallOp::RCP:              OC_SLANG_FUNC(rcp);             break;
        case CallOp::SIGN:             OC_SLANG_FUNC(sign);            break;
        case CallOp::FRACT:            OC_SLANG_FUNC(frac);            break;
        case CallOp::DEGREES:          OC_SLANG_FUNC(degrees);         break;
        case CallOp::RADIANS:          OC_SLANG_FUNC(radians);         break;
        case CallOp::SATURATE:         OC_SLANG_FUNC(saturate);        break;

        case CallOp::SYNCHRONIZE_BLOCK:         OC_SLANG_WRAP(synchronize_block);         break;
        case CallOp::WARP_ACTIVE_BIT_MASK:      OC_SLANG_WRAP(warp_active_bit_mask);      break;
        case CallOp::WARP_ACTIVE_COUNT_BITS:    OC_SLANG_WRAP(warp_active_count_bits);    break;
        case CallOp::WARP_PREFIX_COUNT_BITS:    OC_SLANG_WRAP(warp_prefix_count_bits);    break;
        case CallOp::WARP_LANE_ID:              OC_SLANG_WRAP(warp_lane_id);              break;
        case CallOp::WARP_SIZE:                 OC_SLANG_WRAP(warp_size);                 break;
        case CallOp::WARP_FIRST_ACTIVE_LANE:    OC_SLANG_WRAP(warp_first_active_lane);    break;
        case CallOp::WARP_IS_FIRST_ACTIVE_LANE: OC_SLANG_WRAP(warp_is_first_active_lane); break;

        case CallOp::MAKE_BOOL2:
        case CallOp::MAKE_BOOL3:
        case CallOp::MAKE_BOOL4:
        case CallOp::MAKE_INT2:
        case CallOp::MAKE_INT3:
        case CallOp::MAKE_INT4:
        case CallOp::MAKE_UINT2:
        case CallOp::MAKE_UINT3:
        case CallOp::MAKE_UINT4:
        case CallOp::MAKE_UCHAR2:
        case CallOp::MAKE_UCHAR3:
        case CallOp::MAKE_UCHAR4:
        case CallOp::MAKE_FLOAT2:
        case CallOp::MAKE_FLOAT3:
        case CallOp::MAKE_FLOAT4:
        case CallOp::MAKE_REAL2:
        case CallOp::MAKE_REAL3:
        case CallOp::MAKE_REAL4:
        case CallOp::MAKE_HALF2:
        case CallOp::MAKE_HALF3:
        case CallOp::MAKE_HALF4:
        case CallOp::MAKE_HALF2X2:
        case CallOp::MAKE_HALF2X3:
        case CallOp::MAKE_HALF2X4:
        case CallOp::MAKE_HALF3X2:
        case CallOp::MAKE_HALF3X3:
        case CallOp::MAKE_HALF3X4:
        case CallOp::MAKE_HALF4X2:
        case CallOp::MAKE_HALF4X3:
        case CallOp::MAKE_HALF4X4:
        case CallOp::MAKE_REAL2X2:
        case CallOp::MAKE_REAL2X3:
        case CallOp::MAKE_REAL2X4:
        case CallOp::MAKE_REAL3X2:
        case CallOp::MAKE_REAL3X3:
        case CallOp::MAKE_REAL3X4:
        case CallOp::MAKE_REAL4X2:
        case CallOp::MAKE_REAL4X3:
        case CallOp::MAKE_REAL4X4:
        case CallOp::MAKE_FLOAT2X2:
        case CallOp::MAKE_FLOAT2X3:
        case CallOp::MAKE_FLOAT2X4:
        case CallOp::MAKE_FLOAT3X2:
        case CallOp::MAKE_FLOAT3X3:
        case CallOp::MAKE_FLOAT3X4:
        case CallOp::MAKE_FLOAT4X2:
        case CallOp::MAKE_FLOAT4X3:
        case CallOp::MAKE_FLOAT4X4: OC_SLANG_CTOR; break;

        case CallOp::FLOAT2HALF: OC_SLANG_WRAP(float2half); break;
        case CallOp::HALF2FLOAT: OC_SLANG_WRAP(half2float); break;

        case CallOp::GEMM: {
            uint id = std::get<uint>(expr->template_arg(0));
            current_scratch() << "oc_tensor_gemm<" << id << ">";
            break;
        }

        case CallOp::ATOMIC_EXCH: OC_SLANG_WRAP(atomicExch); break;
        case CallOp::ATOMIC_ADD:  OC_SLANG_WRAP(atomicAdd);  break;
        case CallOp::ATOMIC_SUB:  OC_SLANG_WRAP(atomicSub);  break;
        case CallOp::ATOMIC_CAS:  OC_SLANG_WRAP(atomicCAS);  break;
        case CallOp::BINDLESS_ARRAY_BUFFER_WRITE:      OC_SLANG_WRAP(bindless_array_buffer_write);      break;
        case CallOp::BINDLESS_ARRAY_BYTE_BUFFER_WRITE: OC_SLANG_WRAP(bindless_array_byte_buffer_write); break;
        case CallOp::BINDLESS_ARRAY_BUFFER_SIZE:       OC_SLANG_WRAP(bindless_array_buffer_size);       break;
        case CallOp::BINDLESS_ARRAY_BYTE_BUFFER_READ: {
            current_scratch() << "oc_bindless_array_byte_buffer_read<";
            _emit_type_name(expr->type());
            current_scratch() << ">";
            break;
        }
        case CallOp::BINDLESS_ARRAY_BUFFER_READ: {
            current_scratch() << "oc_bindless_array_buffer_read<";
            _emit_type_name(expr->type());
            current_scratch() << ">";
            break;
        }
        case CallOp::BYTE_BUFFER_WRITE: OC_SLANG_WRAP(byte_buffer_write); break;
        case CallOp::BYTE_BUFFER_READ: {
            // The integral form reads N packed uints (CUDA: oc_byte_buffer_read<N>).
            ocarina::visit(
                [&]<typename T>(T &&t) {
                    if constexpr (is_integral_v<T>) {
                        current_scratch() << "oc_byte_buffer_read<"
                                          << (int(t) == 1 ? string("uint") : ocarina::format("uint{}", int(t)))
                                          << ">";
                    } else {
                        current_scratch() << "oc_byte_buffer_read<";
                        _emit_type_name(t);
                        current_scratch() << ">";
                    }
                },
                expr->template_arg(0));
            break;
        }

        case CallOp::UNREACHABLE:       OC_SLANG_WRAP(unreachable);       break;
        case CallOp::MAKE_RAY:          OC_SLANG_WRAP(make_ray);          break;
        case CallOp::TRACE_OCCLUSION:   OC_SLANG_WRAP(trace_occlusion);   break;
        case CallOp::RAY_OFFSET_ORIGIN: OC_SLANG_WRAP(offset_ray_origin); break;
        case CallOp::TRACE_CLOSEST:     OC_SLANG_WRAP(trace_closest);     break;
        case CallOp::IS_NULL_BUFFER:    OC_SLANG_WRAP(is_null_buffer);    break;
        case CallOp::IS_NULL_TEXTURE:   OC_SLANG_WRAP(is_null_texture);   break;
        case CallOp::BUFFER_SIZE:       OC_SLANG_WRAP(buffer_size);       break;
        case CallOp::BYTE_BUFFER_SIZE:  OC_SLANG_WRAP(buffer_size);       break;

        // Texture ops keep the CUDA builtin names and template arguments so the
        // Slang builtins can mirror cuda_device_resource.h signatures one-to-one.
        case CallOp::BINDLESS_ARRAY_TEX3D_SAMPLE: {
            uint N = std::get<uint>(expr->template_arg(0));
            current_scratch() << "oc_bindless_array_tex3d_sample<" << N << ">";
            break;
        }
        case CallOp::TEX3D_SAMPLE: {
            uint N = std::get<uint>(expr->template_arg(0));
            current_scratch() << "oc_tex3d_sample_float<" << N << ">";
            break;
        }
        case CallOp::TEX3D_READ: {
            current_scratch() << "oc_tex3d_read<";
            _emit_type_name(std::get<const Type *>(expr->template_arg(0)));
            current_scratch() << ">";
            break;
        }
        case CallOp::TEX3D_WRITE: OC_SLANG_WRAP(tex3d_write); break;

        case CallOp::BINDLESS_ARRAY_TEX2D_SAMPLE: {
            uint N = std::get<uint>(expr->template_arg(0));
            current_scratch() << "oc_bindless_array_tex2d_sample<" << N << ">";
            break;
        }
        case CallOp::TEX2D_SAMPLE: {
            uint N = std::get<uint>(expr->template_arg(0));
            current_scratch() << "oc_tex2d_sample_float<" << N << ">";
            break;
        }
        case CallOp::TEX2D_READ: {
            current_scratch() << "oc_tex2d_read<";
            _emit_type_name(std::get<const Type *>(expr->template_arg(0)));
            current_scratch() << ">";
            break;
        }
        case CallOp::TEX2D_WRITE: OC_SLANG_WRAP(tex2d_write); break;
        case CallOp::COUNT: break;
        default: OC_ASSERT(0); break;
    }
#undef OC_SLANG_CTOR
#undef OC_SLANG_WRAP
#undef OC_SLANG_FUNC
    emit_args(expr);
}

// ── Expressions ──────────────────────────────────────────────────────────────

void AstToSlangSource::visit(const BinaryExpr *expr) noexcept {
    // Slang `*` on matrices is component-wise. ocarina matrices are column-major
    // (m[i] is column i) while Slang indexes rows, so the emitted matrix is the
    // transpose of the ocarina one and every matrix product swaps its operands.
    const Type *lhs = expr->lhs()->type();
    const Type *rhs = expr->rhs()->type();
    bool matrix_product = expr->op() == BinaryOp::MUL &&
                          (lhs->is_matrix() || rhs->is_matrix()) &&
                          !lhs->is_scalar() && !rhs->is_scalar();
    if (!matrix_product) {
        AstToCppSource::visit(expr);
        return;
    }
    current_scratch() << "mul(";
    expr->rhs()->accept(*this);
    current_scratch() << ", ";
    expr->lhs()->accept(*this);
    current_scratch() << ")";
}

void AstToSlangSource::visit(const CastExpr *expr) noexcept {
    switch (expr->cast_op()) {
        case CastOp::STATIC:
            _emit_type_name(expr->type());
            current_scratch() << "(";
            break;
        case CastOp::BITWISE:
            current_scratch() << "bit_cast<";
            _emit_type_name(expr->type());
            current_scratch() << " >(";
            break;
    }
    expr->expression()->accept(*this);
    current_scratch() << ")";
}

void AstToSlangSource::visit(const LiteralExpr *expr) noexcept {
    // Every non-bool literal is wrapped in its Slang type, so half, 8/16/64-bit
    // and unsigned literals never depend on suffix inference.
    auto print_scalar = [this]<typename T>(T v, string_view name) {
        if constexpr (std::is_same_v<T, bool>) {
            current_scratch() << (v ? "true" : "false");
            return;
        }
        current_scratch() << name << "(";
        if constexpr (ocarina::is_floating_point_v<T> || std::is_same_v<T, half>) {
            float f = static_cast<float>(v);
            if (std::isnan(f)) [[unlikely]] {
                OC_ERROR("nan error!");
            } else if (std::isinf(f)) {
                current_scratch() << (f < 0.f ? "asfloat(0xff800000u)" : "asfloat(0x7f800000u)");
            } else {
                current_scratch() << f;
            }
        } else if constexpr (sizeof(T) == 8) {
            current_scratch() << ocarina::format("{}{}", v, std::is_signed_v<T> ? "ll" : "ull");
        } else if constexpr (std::is_signed_v<T>) {
            current_scratch() << static_cast<int>(v);
        } else {
            current_scratch() << static_cast<uint>(v);
        }
        current_scratch() << ")";
    };
    // By value, like detail::LiteralPrinter: Vector/Matrix indexing may be non-const.
    auto print_vector = [&]<typename V>(V v, const Type *type) {
        constexpr auto dim = ocarina::vector_dimension_v<V>;
        string element = slang_type_name(type->element()->name());
        current_scratch() << slang_type_name(type->name()) << "(";
        for (size_t i = 0; i < dim; ++i) {
            print_scalar(v[i], element);
            if (i + 1 < dim) current_scratch() << ",";
        }
        current_scratch() << ")";
    };
    // Columns are passed in ocarina order, consistent with the transposed
    // matrix convention described in visit(BinaryExpr).
    auto print_matrix = [&]<typename T, size_t N, size_t M>(Matrix<T, N, M> m, const Type *type) {
        current_scratch() << slang_type_name(type->name()) << "(";
        for (size_t i = 0; i < M; ++i) {
            print_vector(m[i], type->element());
            if (i + 1 < M) current_scratch() << ",";
        }
        current_scratch() << ")";
    };
    const Type *type = expr->type();
    ocarina::visit(
        [&]<typename T>(const T &v) {
            const Type *t = type != nullptr ? type : Type::of<T>();
            if constexpr (ocarina::is_scalar_v<T>) {
                print_scalar(v, slang_type_name(t->name()));
            } else if constexpr (ocarina::is_vector_v<T>) {
                print_vector(v, t);
            } else {
                print_matrix(v, t);
            }
        },
        expr->value());
}

// ── Types and declarations ───────────────────────────────────────────────────

void AstToSlangSource::visit(const Type *type) noexcept {
    // Slang has neither alignas nor brace member initializers.
    if (!type->is_structure() ||
        has_generated(type) ||
        type->is_builtin_struct()) {
        return;
    }
    current_scratch() << "struct ";
    _emit_struct_name(type);
    current_scratch() << " {";
    _emit_newline();
    indent_inc();
    for (int i = 0; i < static_cast<int>(type->members().size()); ++i) {
        _emit_indent();
        _emit_type_name(type->members()[i]);
        _emit_space();
        _emit_member_name(type, i);
        current_scratch() << ";";
        _emit_newline();
    }
    indent_dec();
    current_scratch() << "};";
    _emit_newline();
    add_generated(type);
}

void AstToSlangSource::_emit_variable_define(const Variable &v) noexcept {
    // Slang has no references; by-reference callable parameters become inout.
    if (v.tag() == Variable::Tag::REFERENCE && !v.type()->is_buffer()) {
        current_scratch() << "inout ";
    }
    _emit_type_name(v.type());
    _emit_space();
    _emit_variable_name(v);
}

void AstToSlangSource::_emit_local_var_define(const ScopeStmt *scope) noexcept {
    for (const auto &var : scope->local_vars()) {
        _emit_indent();
        _emit_variable_define(var);
        // `= {}` zero-initializes like C++ `T v{}`; opaque handles cannot be.
        current_scratch() << (var.type()->tag() == Type::Tag::ACCEL ? ";" : " = {};");
        _emit_newline();
    }
}

void AstToSlangSource::_emit_type_name(const Type *type) noexcept {
    if (type == nullptr) {
        current_scratch() << "void";
        return;
    }
    switch (type->tag()) {
        case Type::Tag::BOOL:
        case Type::Tag::FLOAT:
        case Type::Tag::REAL:
        case Type::Tag::HALF:
        case Type::Tag::INT:
        case Type::Tag::UINT:
        case Type::Tag::UCHAR:
        case Type::Tag::CHAR:
        case Type::Tag::SHORT:
        case Type::Tag::USHORT:
        case Type::Tag::ULONG:
        case Type::Tag::VECTOR:
        case Type::Tag::MATRIX:
            current_scratch() << slang_type_name(type->name());
            break;
        case Type::Tag::ARRAY:
            // Type-first array syntax works for parameters, locals, members and returns.
            _emit_type_name(type->element());
            current_scratch() << "[" << type->dimension() << "]";
            break;
        case Type::Tag::STRUCTURE:
            _emit_struct_name(type);
            break;
        case Type::Tag::BUFFER:
            // The space keeps nested generics from lexing as `>>`.
            current_scratch() << "OCBuffer<";
            _emit_type_name(type->element());
            current_scratch() << " >";
            break;
        case Type::Tag::BYTE_BUFFER:
            current_scratch() << "OCBuffer<uint8_t>";
            break;
        case Type::Tag::TEXTURE3D:
        case Type::Tag::TEXTURE2D:
            current_scratch() << "OCTextureDesc";
            break;
        case Type::Tag::BINDLESS_ARRAY:
            current_scratch() << "OCBindlessArrayDesc";
            break;
        case Type::Tag::ACCEL:
            current_scratch() << "RaytracingAccelerationStructure";
            break;
        case Type::Tag::NONE:
            break;
    }
}

void AstToSlangSource::_emit_struct_name(const Type *type) noexcept {
    OC_ERROR_IF(type->cname().empty());
    if (type->is_builtin_struct()) {
        current_scratch() << type->simple_cname();
    } else {
        SourceEmitter::_emit_struct_name(type);
    }
}

// ── Kernel interface ─────────────────────────────────────────────────────────
// Push constants (VkShaderBase::c_pc_*): 8-byte device address of the packed
// argument blob + 12-byte dispatch size. Kernel arguments are read from the blob
// at the offsets the host packs them at (mem_offset over VulkanComputeDevice
// size/alignment), so correctness never depends on Slang's struct layout rules.

void AstToSlangSource::_emit_kernel_params(const Function &f) noexcept {
    current_scratch() << "struct OCPushConstants {";
    _emit_newline();
    indent_inc();
    _emit_indent();
    current_scratch() << "uint64_t params;";
    _emit_newline();
    // Scalars, not uint3: std430 would align a uint3 member to 16 bytes.
    _emit_indent();
    current_scratch() << "uint dim_x;";
    _emit_newline();
    _emit_indent();
    current_scratch() << "uint dim_y;";
    _emit_newline();
    _emit_indent();
    current_scratch() << "uint dim_z;";
    _emit_newline();
    indent_dec();
    current_scratch() << "};";
    _emit_newline();
    current_scratch() << "[[vk::push_constant]] OCPushConstants oc_push;";
    _emit_newline();
    current_scratch() << "T oc_param<T>(uint offset) { return *(T *)(oc_push.params + offset); }";
    _emit_newline();
    current_scratch() << "uint oc_param_bits(uint word_offset, uint shift) { return oc_param<uint>(word_offset) >> shift; }";
    _emit_newline();
    // CUDA blockIdx/threadIdx are visible in callables too; mirror them as statics.
    current_scratch() << "static uint3 oc_group_id;";
    _emit_newline();
    current_scratch() << "static uint3 oc_group_thread_id;";
    _emit_newline();
    // WorkgroupSize() is only referenced when THREAD_ID is used, so other kernels
    // do not depend on it.
    bool thread_id_used = uses_thread_id_;
    for (const Variable &v : f.builtin_vars()) {
        thread_id_used |= v.tag() == Variable::Tag::THREAD_ID;
    }
    if (thread_id_used) {
        current_scratch() << "uint oc_thread_id(uint3 d_dim) {";
        _emit_newline();
        indent_inc();
        _emit_indent();
        current_scratch() << "uint3 block = uint3(WorkgroupSize());";
        _emit_newline();
        _emit_indent();
        current_scratch() << "uint3 grid = (d_dim + block - 1u) / block;";
        _emit_newline();
        _emit_indent();
        current_scratch() << "return (oc_group_id.x + oc_group_id.y * grid.x + grid.x * grid.y * oc_group_id.z)"
                             " * (block.x * block.y * block.z)"
                             " + oc_group_thread_id.z * (block.x * block.y)"
                             " + oc_group_thread_id.y * block.x + oc_group_thread_id.x;";
        _emit_newline();
        indent_dec();
        current_scratch() << "}";
        _emit_newline();
    }
}

void AstToSlangSource::_emit_raytracing_param(const Function &) noexcept {
    // Raytracing kernels share the push-constant interface emitted from
    // _emit_function(), which keeps the AST and IR paths identical.
}

void AstToSlangSource::_emit_function(const Function &f) noexcept {
    if (has_generated(&f)) {
        return;
    }
    // The IR path calls _emit_function() directly; builtin-var emission needs
    // current_function().
    FUNCTION_GUARD(f)
    if (f.is_callable()) {
        AstToCppSource::_emit_function(f);
        return;
    }
    _emit_kernel_params(f);
    const auto policy = f.storage_policy();
    _emit_comment(ocarina::format("compile policy: policy={}, allow_real_in_storage={}",
                                  precision_policy_name(policy.policy),
                                  policy.allow_real_in_storage ? "true" : "false"));
    _emit_newline();
    if (f.is_raytracing_kernel()) {
        current_scratch() << "[shader(\"raygeneration\")]";
    } else {
        // SlangShaderCompiler::compile() patches this exact string with the real
        // workgroup size.
        current_scratch() << "[shader(\"compute\")] [numthreads(1, 1, 1)]";
    }
    _emit_newline();
    // SlangShaderCompiler looks the entry point up as "main".
    current_scratch() << "void main";
    _emit_arguments(f);
    _emit_body(f);
    add_generated(&f);
    _emit_newline();
}

void AstToSlangSource::_emit_arguments(const Function &f) noexcept {
    current_scratch() << "(";
    if (f.is_general_kernel()) {
        // Kernel arguments come from the push-constant blob, not entry parameters.
        current_scratch() << "uint3 oc_dispatch_thread_id : SV_DispatchThreadID, "
                             "uint3 oc_group_id_in : SV_GroupID, "
                             "uint3 oc_group_thread_id_in : SV_GroupThreadID";
    } else if (f.is_callable()) {
        for (const auto &v : f.all_arguments()) {
            _emit_argument(v);
        }
        Variable dispatch_dim = const_cast<Function &>(f).create_variable(Type::of<uint3>(), Variable::Tag::LOCAL, "d_dim");
        _emit_argument(dispatch_dim);
        Variable dispatch_idx = const_cast<Function &>(f).create_variable(Type::of<uint3>(), Variable::Tag::LOCAL, "d_idx");
        _emit_variable_define(dispatch_idx);
    }
    current_scratch() << ")";
}

void AstToSlangSource::_emit_builtin_vars_define(const Function &f) noexcept {
    if (f.is_general_kernel()) {
        _emit_indent();
        current_scratch() << "oc_group_id = oc_group_id_in;";
        _emit_newline();
        _emit_indent();
        current_scratch() << "oc_group_thread_id = oc_group_thread_id_in;";
        _emit_newline();
        _emit_indent();
        current_scratch() << "uint3 d_dim = uint3(oc_push.dim_x, oc_push.dim_y, oc_push.dim_z);";
        _emit_newline();
        _emit_indent();
        current_scratch() << "uint3 d_idx = oc_dispatch_thread_id;";
        _emit_newline();
        _emit_indent();
        current_scratch() << "if (any(d_idx >= d_dim)) { return; }";
        _emit_newline();
        _emit_kernel_argument_loads(f);
    } else if (f.is_raytracing_kernel()) {
        _emit_indent();
        current_scratch() << "uint3 d_idx = DispatchRaysIndex();";
        _emit_newline();
        _emit_indent();
        current_scratch() << "uint3 d_dim = DispatchRaysDimensions();";
        _emit_newline();
        _emit_kernel_argument_loads(f);
    }
    for (const Variable &v : f.builtin_vars()) {
        uses_thread_id_ |= v.tag() == Variable::Tag::THREAD_ID;
    }
    AstToCppSource::_emit_builtin_vars_define(f);
}

void AstToSlangSource::_emit_kernel_argument_loads(const Function &f) noexcept {
    // Same order as the host argument blob: arguments, then captured resources.
    size_t offset = 0;
    auto load = [&](const Variable &arg) {
        const Type *type = arg.type();
        size_t size = VulkanComputeDevice::size(type);
        offset = mem_offset(offset, VulkanComputeDevice::alignment(type));
        _emit_indent();
        _emit_type_name(type);
        _emit_space();
        _emit_variable_name(arg);
        current_scratch() << " = ";
        const Type *element = type->is_vector() ? type->element() : type;
        bool narrow = !narrow_scalar_load(element, 0).empty();
        if (type->tag() == Type::Tag::ACCEL) {
            current_scratch() << ocarina::format("RaytracingAccelerationStructure(oc_param<uint64_t>({}u))", offset);
        } else if (narrow && type->is_vector()) {
            _emit_type_name(type);
            current_scratch() << "(";
            for (uint i = 0; i < type->dimension(); ++i) {
                current_scratch() << narrow_scalar_load(element, offset + i * element->size());
                if (i + 1 < type->dimension()) current_scratch() << ", ";
            }
            current_scratch() << ")";
        } else if (narrow) {
            current_scratch() << narrow_scalar_load(type, offset);
        } else {
            current_scratch() << "oc_param<";
            _emit_type_name(type);
            current_scratch() << " >(" << ocarina::format("{}u", offset) << ")";
        }
        current_scratch() << ";";
        _emit_comment(ocarina::format("{} bytes at offset {}", size, offset));
        _emit_newline();
        offset += size;
    };
    for (const Variable &arg : f.arguments()) {
        load(arg);
    }
    f.for_each_captured_resource([&](const CapturedResource &uniform) {
        load(uniform.expression()->variable());
    });
}

void AstToSlangSource::_emit_builtin_var(Variable v) noexcept {
    using Tag = Variable::Tag;
    bool group_scoped = v.tag() == Tag::BLOCK_IDX || v.tag() == Tag::THREAD_IDX || v.tag() == Tag::THREAD_ID;
    if (group_scoped && current_function().is_raytracing_kernel()) {
        OC_ERROR("Slang raygen shaders have no workgroup: block_idx/thread_idx/thread_id are unavailable");
    }
    _emit_type_name(v.type());
    _emit_space();
    _emit_variable_name(v);
    current_scratch() << " = ";
    switch (v.tag()) {
        case Tag::BLOCK_IDX:
            current_scratch() << "oc_group_id";
            break;
        case Tag::THREAD_IDX:
            current_scratch() << "oc_group_thread_id";
            break;
        case Tag::THREAD_ID:
            current_scratch() << "oc_thread_id(d_dim)";
            break;
        case Tag::DISPATCH_IDX:
            current_scratch() << "d_idx";
            break;
        case Tag::DISPATCH_ID:
            current_scratch() << "d_idx.z * d_dim.x * d_dim.y + d_dim.x * d_idx.y + d_idx.x";
            break;
        case Tag::DISPATCH_DIM:
            current_scratch() << "d_dim";
            break;
        default:
            OC_ASSERT(0);
            break;
    }
}

}// namespace ocarina
