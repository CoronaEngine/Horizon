#pragma once
#include <Codegen/VariateProxy.h>
#include <Codegen/Generator/SlangGenerator.hpp>

namespace EmbeddedShader::BuiltinVariate
{
    inline VariateProxy<ktm::fvec4> position()
    {
        return VariateProxy<ktm::fvec4>{Ast::AST::getPositionOutput()};
    }

    inline VariateProxy<bool> isFrontFace()
    {
        return VariateProxy<bool>{Ast::AST::getIsFrontFaceOutput()};
    }

    inline VariateProxy<ktm::uvec3> dispatchThreadID()
    {
        return VariateProxy<ktm::uvec3>{Ast::AST::getDispatchThreadIDInput()};
    }

    inline VariateProxy<uint32_t> drawIndex()
    {
        return VariateProxy<uint32_t>{Ast::AST::getDrawIndexInput()};
    }
}

namespace EmbeddedShader
{
    using namespace BuiltinVariate;
}