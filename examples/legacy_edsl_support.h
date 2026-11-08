#pragma once

#include "legacy_common.h"
#include "Codegen/BuiltinVariate.h"
#include "Codegen/ControlFlows.h"
#include "Codegen/CustomLibrary.h"
#include "Codegen/TypeAlias.h"
#include <cstring>

// Example-local EDSL helpers. Match the legacy handwritten shaders' six-layer
// DDS addressing and explicit bilinear/trilinear filtering, including edge clamp.
namespace legacy_example::edsl {
using namespace EmbeddedShader;
using namespace ktm;

inline fmat4x4 value(const glm::mat4& m) {
    static_assert(sizeof(fmat4x4) == sizeof(m));
    fmat4x4 result;
    std::memcpy(&result, &m, sizeof(result));
    return result;
}
inline fvec4 value(const glm::vec4& v) { return {v.x, v.y, v.z, v.w}; }

template<class T, class... Args>
VariateProxy<T> intrinsic(const char* name, const Args&... args) {
    return {Ast::AST::callFunc(name, Ast::AST::createType<T>(),
        Ast::Node::accessAll({proxy_wrap(args)...}, Ast::AccessPermissions::ReadOnly))};
}

inline Float3 cube_array_coord(Float3 d) {
    Float3 a = abs(d);
    Float2 uv;
    Float face, major;
    $IF ((a->x >= a->y) && (a->x >= a->z)) {
        major = a->x;
        $IF (d->x >= Float(0)) { uv = Float2(-d->z, -d->y); face = Float(0); }
        $ELSE { uv = Float2(d->z, -d->y); face = Float(1); };
    } $ELSE {
        $IF (a->y >= a->z) {
            major = a->y;
            $IF (d->y >= Float(0)) { uv = Float2(d->x, d->z); face = Float(2); }
            $ELSE { uv = Float2(d->x, -d->z); face = Float(3); };
        } $ELSE {
            major = a->z;
            $IF (d->z >= Float(0)) { uv = Float2(d->x, -d->y); face = Float(4); }
            $ELSE { uv = Float2(-d->x, -d->y); face = Float(5); };
        };
    };
    return Float3(uv / max(major, Float(1e-8)) * Float(0.5) + Float2(0.5f), face);
}

struct TextureDimensions { Uint width, height, layers, levels; };
inline TextureDimensions dimensions(Texture2D<fvec4>& tex, Int level) {
    TextureDimensions result;
    auto node = proxy_wrap(tex);
    node->access(Ast::AccessPermissions::ReadOnly);
    Uint mip = intrinsic<uint32_t>("uint", level);
    const auto outputs = Ast::Node::accessAll({proxy_wrap(result.width), proxy_wrap(result.height),
        proxy_wrap(result.layers), proxy_wrap(result.levels)}, Ast::AccessPermissions::WriteOnly);
    std::vector<std::shared_ptr<Ast::Value>> args{proxy_wrap(mip)};
    args.insert(args.end(), outputs.begin(), outputs.end());
    auto call = Ast::AST::callFunc("GetDimensions", nullptr, std::move(args));
    Ast::AST::addLocalUniversalStatement(Ast::AST::access(node, call->parse(), nullptr));
    return result;
}

inline Float4 fetch(Texture2D<fvec4>& tex, Vector3<int32_t> coord, Int level) {
    auto node = proxy_wrap(tex);
    node->access(Ast::AccessPermissions::ReadOnly);
    Vector4<int32_t> location(coord, level);
    auto call = Ast::AST::callFunc("Load", Ast::AST::createType<fvec4>(),
        Ast::Node::accessAll({proxy_wrap(location)}, Ast::AccessPermissions::ReadOnly));
    return {Ast::AST::access(node, call->parse(), call->type)};
}

inline Float4 bilinear(Texture2D<fvec4>& tex, Float3 coord, Int level) {
    // Explicit signed vectors: the current public Int2/Int3 aliases are unsigned.
    using Signed2 = VariateProxy<ktm::vec<2, int32_t>>;
    using Signed3 = VariateProxy<ktm::vec<3, int32_t>>;
    auto dims = dimensions(tex, level);
    Signed2 size(intrinsic<int32_t>("int", dims.width), intrinsic<int32_t>("int", dims.height));
    Float2 p = coord->xy() * intrinsic<fvec2>("float2", size) - Float2(0.5f);
    Signed2 base = intrinsic<ktm::vec<2, int32_t>>("int2", intrinsic<fvec2>("floor", p));
    Float2 f = fract(p);
    Signed2 zero(Int(0), Int(0)), one(Int(1), Int(1));
    Signed2 p0 = clamp(base, zero, size - one);
    Signed2 p1 = clamp(base + one, zero, size - one);
    Int layer = intrinsic<int32_t>("int", Float(coord->z));
    return mix(mix(fetch(tex, Signed3(p0->x, p0->y, layer), level),
                   fetch(tex, Signed3(p1->x, p0->y, layer), level), Float(f->x)),
               mix(fetch(tex, Signed3(p0->x, p1->y, layer), level),
                   fetch(tex, Signed3(p1->x, p1->y, layer), level), Float(f->x)), Float(f->y));
}

inline Float4 sample_cube(Texture2D<fvec4>& tex, Float3 direction, Float lod) {
    // Helicon's texture AST supports the Slang combined-sampler type directly.
    // Retain Texture2DProxy's resource back-pointer for the legacy auto-binder.
    auto type = std::dynamic_pointer_cast<Ast::TextureType>(proxy_wrap(tex)->type);
    type->name = "Sampler2DArray";
    Float3 coord = cube_array_coord(direction);
    Int levels = intrinsic<int32_t>("int", dimensions(tex, Int(0)).levels);
    Float level = clamp(lod, Float(0), intrinsic<float>("float", levels - Int(1)));
    Int lo = intrinsic<int32_t>("int", intrinsic<float>("floor", level));
    Int hi = intrinsic<int32_t>("int", intrinsic<float>("ceil", level));
    return mix(bilinear(tex, coord, lo), bilinear(tex, coord, hi), fract(level));
}

inline EmbeddedShader::CompilerOption options() {
    EmbeddedShader::CompilerOption result;
    result.enableMatrixColumnMajor = true;
    return result;
}
} // namespace legacy_example::edsl
