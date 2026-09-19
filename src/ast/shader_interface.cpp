#include "shader_interface.h"
#include "function.h"
#include "raster_validation.h"

namespace horizon::ast::detail
{
namespace
{

void flatten(const Type *type, uint32_t root, vector<uint32_t> &path, bool varying, vector<ShaderInterfaceSlot> &slots,
             vector<RasterDiagnostic> &diagnostics, const string &context)
{
    if (type && type->is_structure() && !type->members().empty())
    {
        for (uint32_t i = 0; i < type->members().size(); ++i)
        {
            path.push_back(i);
            flatten(type->members()[i], root, path, varying, slots, diagnostics, context);
            path.pop_back();
        }
        return;
    }
    const Type *scalar = type;
    if (type && type->is_vector() && type->dimension() >= 2 && type->dimension() <= 4)
    {
        scalar = type->element();
    }
    bool floating = scalar && scalar->tag() == Type::Tag::Float;
    bool integer = scalar && (scalar->tag() == Type::Tag::Int || scalar->tag() == Type::Tag::Uint);
    if (!floating && !integer)
    {
        string location = context + " root " + std::to_string(root);
        for (auto member : path)
        {
            location += "." + std::to_string(member);
        }
        diagnostics.push_back(
            {RasterDiagnosticCode::InvalidInterfaceType,
             location + ": unsupported interface type " + (type ? string(type->description()) : "void")});
        return;
    }
    auto interpolation = varying ? (floating ? Interpolation::Smooth : Interpolation::Flat) : Interpolation::None;
    slots.push_back({type, root, path, static_cast<uint32_t>(slots.size()), interpolation});
}

}  // namespace

ShaderInterface build_shader_interface(const Function &function, vector<RasterDiagnostic> &diagnostics)
{
    ShaderInterface result;
    vector<uint32_t> path;
    string context = (function.is_vertex() ? "vertex " : "fragment ") + function.description();
    for (uint32_t i = 0; i < function.arguments().size(); ++i)
    {
        flatten(function.arguments()[i].type(), i, path, function.is_fragment(), result.inputs, diagnostics,
                context + " input");
    }
    if (function.return_type())
    {
        flatten(function.return_type(), 0, path, function.is_vertex(), result.outputs, diagnostics,
                context + " output");
    }
    return result;
}

}  // namespace horizon::ast::detail
